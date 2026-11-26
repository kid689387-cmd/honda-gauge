#include "kline.h"
#include "driver/uart.h"

// Frame layout (Honda): [cmd][len][data...][checksum]
// len counts every byte of the frame, checksum makes the byte sum 0 (mod 256).

uint8_t HondaKLine::checksum(const uint8_t *buf, uint8_t len) {
  uint8_t s = 0;
  for (uint8_t i = 0; i < len; i++) s += buf[i];
  return (uint8_t)(0x100 - s);
}

void HondaKLine::begin(bool invertTx) {
  invTx = invertTx;
  pinMode(KLINE_TX_PIN, OUTPUT);
  setIdle();
}

void HondaKLine::setInvertTx(bool inv) {
  invTx = inv;
  Serial2.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  setIdle();
  st = K_IDLE;
}

// Idle = K-Line high (recessive). Inverting transistor circuit needs GPIO low.
void HondaKLine::setIdle()   { digitalWrite(KLINE_TX_PIN, invTx ? LOW : HIGH); }
void HondaKLine::setActive() { digitalWrite(KLINE_TX_PIN, invTx ? HIGH : LOW); }

void HondaKLine::startUart() {
  Serial2.begin(KLINE_BAUD, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN);
  Serial2.setTimeout(50);
  uart_set_line_inverse(UART_NUM_2, invTx ? UART_SIGNAL_TXD_INV : UART_SIGNAL_INV_DISABLE);
  while (Serial2.available()) Serial2.read();
}

bool HondaKLine::sendFrame(const uint8_t *buf, uint8_t len) {
  while (Serial2.available()) Serial2.read();
  Serial2.write(buf, len);
  Serial2.flush();
  // single wire: every byte we send comes back on RX, drop the echo
  uint32_t t0 = millis();
  uint8_t got = 0;
  while (got < len && millis() - t0 < 30 + len * 2) {
    if (Serial2.available()) { Serial2.read(); got++; }
  }
  if (got < len) { err = "no echo (check wiring / TX invert)"; return false; }
  return true;
}

bool HondaKLine::readFrame(uint8_t *buf, uint8_t &len, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  uint8_t n = 0, want = 2;
  while (millis() - t0 < timeoutMs) {
    if (!Serial2.available()) { delay(1); continue; }
    buf[n++] = Serial2.read();
    if (n == 2) {
      want = buf[1];
      if (want < 3 || want > 64) { err = "bad length"; return false; }
    }
    if (n >= 2 && n >= want) {
      len = n;
      memcpy(lastFrame, buf, n);
      lastFrameLen = n;
      if (checksum(buf, n - 1) != buf[n - 1]) { err = "bad checksum"; return false; }
      return true;
    }
  }
  err = "ECU timeout";
  return false;
}

bool HondaKLine::command(const uint8_t *req, uint8_t reqLen, uint8_t *resp, uint8_t &respLen) {
  uint8_t frame[16];
  memcpy(frame, req, reqLen);
  frame[1] = reqLen + 1;
  frame[reqLen] = checksum(frame, reqLen);
  if (!sendFrame(frame, reqLen + 1)) return false;
  return readFrame(resp, respLen, 300);
}

bool HondaKLine::connect() {
  st = K_WAKING;
  Serial2.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  setIdle();   delay(300);
  setActive(); delay(70);
  setIdle();   delay(130);
  startUart();

  const uint8_t wake[] = {0xFE, 0x04, 0xFF, 0xFF};
  sendFrame(wake, sizeof(wake));          // no answer expected
  delay(200);

  const uint8_t init[] = {0x72, 0x00, 0x00, 0xF0};
  uint8_t resp[64], rlen = 0;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (command(init, sizeof(init), resp, rlen) && resp[0] == 0x02) {
      st = K_CONNECTED;
      err = "";
      return true;
    }
    delay(100);
  }
  st = K_ERROR;
  return false;
}

bool HondaKLine::readTable(uint8_t table, uint8_t *out, uint8_t &outLen) {
  const uint8_t req[] = {0x72, 0x00, 0x71, table};
  uint8_t resp[64], rlen = 0;
  if (!command(req, sizeof(req), resp, rlen)) { st = K_ERROR; return false; }
  // resp: 02 LEN 71 TABLE data... CS
  if (rlen < 5 || resp[2] != 0x71) { err = "unexpected reply"; return false; }
  outLen = rlen - 5;
  memcpy(out, resp + 4, outLen);
  return true;
}

bool HondaKLine::readLiveData(EcuData &d) {
  uint8_t b[64], n = 0;
  if (!readTable(0x11, b, n) || n < 14) { d.valid = false; return false; }
  d.rpm      = (uint16_t)(b[0] << 8 | b[1]);
  d.tpsVolt  = b[2] * 5.0f / 256.0f;
  d.tpsPct   = min(100.0f, b[3] / 1.6f);
  d.ectC     = (int)b[5] - 40;
  d.iatC     = (int)b[7] - 40;
  d.mapKpa   = b[9];
  d.battV    = b[12] / 10.0f;
  d.speedKmh = b[13];
  d.valid    = true;
  d.updatedMs = millis();
  return true;
}

// Fault codes come as main-sub pairs, shown like "12-01". Experimental: check RAW.
int HondaKLine::readDtc(char codes[][8], int maxCodes, bool past) {
  int count = 0;
  uint8_t tbl = past ? 0x73 : 0x74;
  for (uint8_t page = 1; page <= 0x0C && count < maxCodes; page++) {
    const uint8_t req[] = {0x72, 0x00, tbl, page};
    uint8_t resp[64], rlen = 0;
    if (!command(req, sizeof(req), resp, rlen)) return count > 0 ? count : -1;
    // payload starts at resp[2]: [tbl][page][...]
    const uint8_t *p = resp + 2;
    int plen = rlen - 3;
    for (int j = 3; j + 1 < plen && j <= 7 && count < maxCodes; j += 2) {
      if (p[j] != 0) {
        snprintf(codes[count], 8, "%02d-%02d", p[j], p[j + 1]);
        count++;
      }
    }
  }
  return count;
}

bool HondaKLine::clearDtc() {
  const uint8_t req[] = {0x72, 0x00, 0x60, 0x03};
  uint8_t resp[64], rlen = 0;
  if (!command(req, sizeof(req), resp, rlen)) return false;
  return resp[0] == 0x02;
}
