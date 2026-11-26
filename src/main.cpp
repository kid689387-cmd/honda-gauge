// Honda K-Line Smart Gauge - ESP32-2432S028 (CYD 2.8")
// Screens: GAUGE / DTC / RAW / SET.  Landscape 320x240.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "kline.h"

#define FW_VERSION "0.1.0"
#ifndef PANEL_NAME
#define PANEL_NAME "?"
#endif

// ---- touch (XPT2046 on its own SPI bus) ----
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;
HondaKLine kline;
EcuData ecu;
WebServer *server = nullptr;

// ---- settings ----
bool setInvTx = false, setInvColor = false, setDemo = true, setFlipTouch = false;

// ---- ui state ----
enum Screen { SCR_GAUGE, SCR_DTC, SCR_RAW, SCR_SET, SCR_OTA };
Screen screen = SCR_GAUGE;
bool needFull = true;
const int TAB_Y = 204, TAB_H = 36;
const char *tabNames[] = {"GAUGE", "DTC", "RAW", "SET"};

#define C_BG     0x0841
#define C_PANEL  0x18E3
#define C_LINE   0x39E7
#define C_TEXT   TFT_WHITE
#define C_DIM    0x8410
#define C_ACCENT 0xFD20   // orange
#define C_OK     0x07E0
#define C_BAD    0xF800

char dtcList[24][8];
int  dtcCount = -2;          // -2 never read, -1 error
bool dtcPast = false;
uint32_t clearArmedMs = 0;
String dtcMsg = "";
uint8_t rawTable = 0x11;
String rawMsg = "";
uint32_t lastPoll = 0, lastConnectTry = 0;
int failCount = 0;
int lastTouchX = -1, lastTouchY = -1;

// ---------------------------------------------------------------- helpers
void loadSettings() {
  prefs.begin("gauge", true);
  setInvTx      = prefs.getBool("invTx", false);
  setInvColor   = prefs.getBool("invCol", false);
  setDemo       = prefs.getBool("demo", true);
  setFlipTouch  = prefs.getBool("flipT", false);
  prefs.end();
}
void saveSettings() {
  prefs.begin("gauge", false);
  prefs.putBool("invTx", setInvTx);
  prefs.putBool("invCol", setInvColor);
  prefs.putBool("demo", setDemo);
  prefs.putBool("flipT", setFlipTouch);
  prefs.end();
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void button(int x, int y, int w, int h, const char *label, uint16_t col) {
  tft.fillRoundRect(x, y, w, h, 6, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 6, col);
  tft.setTextColor(col, C_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(label, x + w / 2, y + h / 2, 2);
}

void drawTabs() {
  for (int i = 0; i < 4; i++) {
    int x = i * 80;
    bool on = (int)screen == i;
    tft.fillRect(x, TAB_Y, 80, TAB_H, on ? C_ACCENT : C_PANEL);
    tft.drawRect(x, TAB_Y, 80, TAB_H, C_LINE);
    tft.setTextColor(on ? TFT_BLACK : C_TEXT, on ? C_ACCENT : C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(0);
    tft.drawString(tabNames[i], x + 40, TAB_Y + TAB_H / 2, 2);
  }
}

void drawStatus() {
  const char *s; uint16_t c;
  if (setDemo)                          { s = "DEMO";      c = C_ACCENT; }
  else if (kline.state() == K_CONNECTED){ s = "ECU OK";    c = C_OK; }
  else if (kline.state() == K_WAKING)   { s = "CONNECTING";c = C_ACCENT; }
  else                                  { s = "NO ECU";    c = C_BAD; }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c, C_BG);
  tft.setTextPadding(90);
  tft.drawString(s, 4, 2, 2);
  tft.setTextPadding(0);
}

// ---------------------------------------------------------------- GAUGE
const char *tileLabel[] = {"ENGINE C", "AIR C", "BATT V", "TPS %", "MAP kPa", "SPEED km/h"};

void tilePos(int i, int &x, int &y) { x = 6 + (i % 3) * 104; y = 96 + (i / 3) * 54; }

void drawGaugeStatic() {
  tft.fillRect(0, 0, 320, TAB_Y, C_BG);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("RPM", 314, 2, 2);
  tft.drawRect(9, 71, 302, 16, C_LINE);
  for (int i = 0; i < 6; i++) {
    int x, y; tilePos(i, x, y);
    tft.fillRoundRect(x, y, 100, 50, 6, C_PANEL);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString(tileLabel[i], x + 6, y + 4, 2);
  }
}

void drawTileValue(int i, const String &v, uint16_t col) {
  int x, y; tilePos(i, x, y);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(col, C_PANEL);
  tft.setTextPadding(86);
  tft.drawString(v, x + 94, y + 34, 4);
  tft.setTextPadding(0);
}

void drawGaugeValues() {
  drawStatus();
  bool ok = ecu.valid && (setDemo || millis() - ecu.updatedMs < 2000);
  // RPM number
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(ok ? C_TEXT : C_DIM, C_BG);
  tft.setTextPadding(200);
  tft.drawString(ok ? String(ecu.rpm) : String("----"), 160, 16, 7);
  tft.setTextPadding(0);
  // RPM bar (0-10000)
  int w = ok ? constrain((int)ecu.rpm * 300 / 10000, 0, 300) : 0;
  uint16_t bc = ecu.rpm > 8500 ? C_BAD : (ecu.rpm > 6500 ? C_ACCENT : C_OK);
  tft.fillRect(10, 72, w, 14, bc);
  tft.fillRect(10 + w, 72, 300 - w, 14, C_BG);
  if (!ok) {
    for (int i = 0; i < 6; i++) drawTileValue(i, "--", C_DIM);
    return;
  }
  drawTileValue(0, String(ecu.ectC), ecu.ectC > 105 ? C_BAD : C_TEXT);
  drawTileValue(1, String(ecu.iatC), C_TEXT);
  drawTileValue(2, String(ecu.battV, 1), (ecu.battV < 11.8 || ecu.battV > 15.0) ? C_BAD : C_TEXT);
  drawTileValue(3, String((int)ecu.tpsPct), C_TEXT);
  drawTileValue(4, String(ecu.mapKpa), C_TEXT);
  drawTileValue(5, String(ecu.speedKmh), C_TEXT);
}

// ---------------------------------------------------------------- DTC
void drawDtc() {
  tft.fillRect(0, 0, 320, TAB_Y, C_BG);
  drawStatus();
  button(6, 24, 98, 40, "READ", C_OK);
  button(111, 24, 98, 40, dtcPast ? "PAST" : "CURRENT", C_TEXT);
  bool armed = clearArmedMs && millis() - clearArmedMs < 4000;
  button(216, 24, 98, 40, armed ? "SURE?" : "CLEAR", C_BAD);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(dtcMsg, 6, 70, 2);

  tft.setTextColor(C_TEXT, C_BG);
  if (dtcCount == -2) tft.drawString("Press READ (ignition ON)", 6, 92, 2);
  else if (dtcCount == -1) { tft.setTextColor(C_BAD, C_BG); tft.drawString("Read failed", 6, 92, 2); }
  else if (dtcCount == 0) { tft.setTextColor(C_OK, C_BG); tft.drawString("No fault codes", 6, 92, 4); }
  else {
    for (int i = 0; i < dtcCount && i < 15; i++) {
      int x = 6 + (i % 3) * 104, y = 92 + (i / 3) * 22;
      tft.drawString(dtcList[i], x, y, 4);
    }
  }
}

void doReadDtc() {
  if (setDemo) {
    dtcCount = 2; strcpy(dtcList[0], "07-01"); strcpy(dtcList[1], "12-01");
    dtcMsg = "Demo codes (not real)";
    return;
  }
  if (kline.state() != K_CONNECTED && !kline.connect()) {
    dtcCount = -1; dtcMsg = kline.lastError(); return;
  }
  dtcCount = kline.readDtc(dtcList, 24, dtcPast);
  dtcMsg = dtcCount < 0 ? String(kline.lastError()) : String(dtcPast ? "Past codes" : "Current codes");
}

void doClearDtc() {
  if (setDemo) { dtcCount = 0; dtcMsg = "Demo: cleared"; return; }
  if (kline.state() != K_CONNECTED && !kline.connect()) { dtcMsg = kline.lastError(); return; }
  bool ok = kline.clearDtc();
  dtcMsg = ok ? "Cleared. Turn key OFF/ON, read again" : String("Clear failed: ") + kline.lastError();
  if (ok) dtcCount = -2;
}

// ---------------------------------------------------------------- RAW
void drawRaw() {
  tft.fillRect(0, 0, 320, TAB_Y, C_BG);
  drawStatus();
  char t[24]; snprintf(t, sizeof(t), "TABLE 0x%02X", rawTable);
  button(6, 24, 150, 36, t, C_TEXT);
  button(164, 24, 150, 36, "READ", C_OK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(rawMsg, 6, 66, 2);
  tft.setTextColor(C_TEXT, C_BG);
  char line[64];
  for (int row = 0; row * 8 < kline.lastFrameLen && row < 8; row++) {
    int p = snprintf(line, sizeof(line), "%02X:", row * 8);
    for (int i = 0; i < 8 && row * 8 + i < kline.lastFrameLen; i++)
      p += snprintf(line + p, sizeof(line) - p, " %02X", kline.lastFrame[row * 8 + i]);
    tft.drawString(line, 6, 86 + row * 14, 2);
  }
}

void doReadRaw() {
  if (setDemo) { rawMsg = "Turn DEMO off in SET first"; return; }
  if (kline.state() != K_CONNECTED && !kline.connect()) { rawMsg = kline.lastError(); return; }
  uint8_t b[64], n = 0;
  rawMsg = kline.readTable(rawTable, b, n) ? String("OK, ") + n + " data bytes" : String(kline.lastError());
}

// ---------------------------------------------------------------- SET
void drawSet() {
  tft.fillRect(0, 0, 320, TAB_Y, C_BG);
  drawStatus();
  button(6, 24, 150, 36, setDemo ? "DEMO: ON" : "DEMO: OFF", setDemo ? C_ACCENT : C_TEXT);
  button(164, 24, 150, 36, setInvTx ? "TX INVERT: ON" : "TX INVERT: OFF", C_TEXT);
  button(6, 66, 150, 36, setInvColor ? "COLOR INV: ON" : "COLOR INV: OFF", C_TEXT);
  button(164, 66, 150, 36, setFlipTouch ? "TOUCH FLIP: ON" : "TOUCH FLIP: OFF", C_TEXT);
  button(6, 108, 150, 36, "WIFI UPDATE", C_OK);
  button(164, 108, 150, 36, "COLOR TEST", C_TEXT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(String("FW ") + FW_VERSION + "  panel " + PANEL_NAME, 6, 152, 2);
  tft.drawString("TX INVERT ON = transistor circuit", 6, 170, 2);
  tft.drawString(String("touch ") + lastTouchX + "," + lastTouchY, 6, 186, 2);
}

void colorTest() {
  const uint16_t c[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char *n[] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    tft.fillRect(i * 80, 0, 80, TAB_Y, c[i]);
    tft.setTextColor(TFT_BLACK, c[i]);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(n[i], i * 80 + 40, TAB_Y / 2, 2);
  }
}

// ---------------------------------------------------------------- WIFI OTA
const char *otaPage =
  "<html><head><meta name='viewport' content='width=device-width'></head><body style='font-family:sans-serif'>"
  "<h3>Honda Gauge update</h3><form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='fw' accept='.bin'><br><br><input type='submit' value='Upload'></form>"
  "<p>Use the file ending in -ota.bin (not -full.bin)</p></body></html>";

void startOta() {
  screen = SCR_OTA;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HondaGauge", "12345678");
  server = new WebServer(80);
  server->on("/", HTTP_GET, []() { server->send(200, "text/html", otaPage); });
  server->on("/update", HTTP_POST,
    []() {
      server->send(200, "text/plain", Update.hasError() ? "FAILED" : "OK, rebooting...");
      delay(500);
      if (!Update.hasError()) ESP.restart();
    },
    []() {
      HTTPUpload &up = server->upload();
      if (up.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
      else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
      else if (up.status == UPLOAD_FILE_END) Update.end(true);
    });
  server->begin();
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("WIFI UPDATE MODE", 10, 10, 4);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("1. Phone WiFi: HondaGauge", 10, 50, 2);
  tft.drawString("   password: 12345678", 10, 70, 2);
  tft.drawString("2. Browser: 192.168.4.1", 10, 95, 2);
  tft.drawString("3. Upload ...-ota.bin", 10, 120, 2);
  button(90, 170, 140, 50, "EXIT", C_BAD);
}

// ---------------------------------------------------------------- touch
bool getTouch(int &x, int &y) {
  static uint32_t lastMs = 0;
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (millis() - lastMs < 250) return false;   // debounce
  lastMs = millis();
  x = map(p.x, 200, 3700, 0, 320);
  y = map(p.y, 240, 3800, 0, 240);
  if (setFlipTouch) { x = 320 - x; y = 240 - y; }
  x = constrain(x, 0, 319); y = constrain(y, 0, 239);
  lastTouchX = x; lastTouchY = y;
  return true;
}

void handleTouch(int x, int y) {
  if (screen == SCR_OTA) {
    if (inRect(x, y, 90, 170, 140, 50)) ESP.restart();
    return;
  }
  if (y >= TAB_Y) {
    screen = (Screen)constrain(x / 80, 0, 3);
    needFull = true;
    return;
  }
  switch (screen) {
    case SCR_DTC:
      if (inRect(x, y, 6, 24, 98, 40)) { dtcMsg = "Reading..."; drawDtc(); doReadDtc(); }
      else if (inRect(x, y, 111, 24, 98, 40)) { dtcPast = !dtcPast; dtcCount = -2; dtcMsg = ""; }
      else if (inRect(x, y, 216, 24, 98, 40)) {
        if (clearArmedMs && millis() - clearArmedMs < 4000) { clearArmedMs = 0; doClearDtc(); }
        else { clearArmedMs = millis(); dtcMsg = "Tap SURE? to clear codes"; }
      }
      needFull = true;
      break;
    case SCR_RAW:
      if (inRect(x, y, 6, 24, 150, 36)) {
        const uint8_t tables[] = {0x10, 0x11, 0x20, 0x21, 0x60, 0x61, 0xD0, 0xD1};
        int i = 0; while (i < 8 && tables[i] != rawTable) i++;
        rawTable = tables[(i + 1) % 8];
      } else if (inRect(x, y, 164, 24, 150, 36)) { rawMsg = "Reading..."; drawRaw(); doReadRaw(); }
      needFull = true;
      break;
    case SCR_SET:
      if (inRect(x, y, 6, 24, 150, 36)) setDemo = !setDemo;
      else if (inRect(x, y, 164, 24, 150, 36)) { setInvTx = !setInvTx; kline.setInvertTx(setInvTx); }
      else if (inRect(x, y, 6, 66, 150, 36)) { setInvColor = !setInvColor; tft.invertDisplay(setInvColor); }
      else if (inRect(x, y, 164, 66, 150, 36)) setFlipTouch = !setFlipTouch;
      else if (inRect(x, y, 6, 108, 150, 36)) { saveSettings(); startOta(); return; }
      else if (inRect(x, y, 164, 108, 150, 36)) { colorTest(); delay(3000); }
      saveSettings();
      needFull = true;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- data
void updateDemo() {
  float t = millis() / 1000.0f;
  ecu.rpm = 1500 + (uint16_t)(3500 * (0.5f + 0.5f * sinf(t * 0.7f)));
  ecu.tpsPct = 50 + 50 * sinf(t * 0.7f);
  ecu.ectC = 85 + (int)(5 * sinf(t * 0.1f));
  ecu.iatC = 36;
  ecu.battV = 13.8f + 0.2f * sinf(t);
  ecu.mapKpa = 40 + (int)(50 * (0.5f + 0.5f * sinf(t * 0.7f)));
  ecu.speedKmh = ecu.rpm / 90;
  ecu.valid = true;
  ecu.updatedMs = millis();
}

void updateEcu() {
  if (setDemo) { updateDemo(); return; }
  if (kline.state() != K_CONNECTED) {
    if (millis() - lastConnectTry > 3000) {
      lastConnectTry = millis();
      drawStatus();
      kline.connect();
      failCount = 0;
    }
    return;
  }
  if (!kline.readLiveData(ecu)) {
    if (++failCount > 3) { kline.setInvertTx(setInvTx); failCount = 0; }
  } else failCount = 0;
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  loadSettings();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(setInvColor);
  tft.fillScreen(C_BG);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  kline.begin(setInvTx);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("HONDA K-LINE GAUGE", 160, 100, 4);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(String("v") + FW_VERSION + "  " + PANEL_NAME, 160, 135, 2);
  delay(1200);
}

void loop() {
  if (screen == SCR_OTA) {
    if (server) server->handleClient();
    int x, y;
    if (getTouch(x, y)) handleTouch(x, y);
    return;
  }

  int x, y;
  if (getTouch(x, y)) handleTouch(x, y);

  bool fresh = false;
  if (screen == SCR_GAUGE && millis() - lastPoll > (setDemo ? 100 : 120)) {
    lastPoll = millis();
    updateEcu();
    fresh = true;
  }

  if (needFull) {
    fresh = true;
    needFull = false;
    switch (screen) {
      case SCR_GAUGE: drawGaugeStatic(); break;
      case SCR_DTC:   drawDtc(); break;
      case SCR_RAW:   drawRaw(); break;
      case SCR_SET:   drawSet(); break;
      default: break;
    }
    drawTabs();
  }
  if (screen == SCR_GAUGE) { if (fresh) drawGaugeValues(); }
  else if (screen == SCR_DTC && clearArmedMs && millis() - clearArmedMs > 4000) {
    clearArmedMs = 0; dtcMsg = ""; drawDtc();
  }
}
