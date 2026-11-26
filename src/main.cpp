// อู่ช่างวัตร ฟรีดอม - Honda K-Line smart gauge for ESP32-2432S028 (CYD 2.8")
// Hardware side: display, touch, backlight, K-Line polling, demo data,
// statistics storage and WiFi update. Screens live in screens.cpp.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "app.h"
#include "gfx.h"
#include "kline.h"

#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define BL_CH      0

TFT_eSPI tft;
App app;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
static Preferences prefs;
static HondaKLine kline;
static WebServer *server = nullptr;

uint32_t nowMs() { return millis(); }

// ---------------------------------------------------------------- storage
static void loadAll() {
  prefs.begin("gauge", true);
  Settings &s = app.set;
  s.demo      = prefs.getBool("demo", true);
  s.invTx     = prefs.getBool("invTx", false);
  s.invColor  = prefs.getBool("invCol", false);
  s.flipTouch = prefs.getBool("flipT", false);
  s.night     = prefs.getBool("night", false);
  s.bright    = prefs.getUChar("bright", 100);
  s.maxRpm    = prefs.getUShort("maxRpm", 10000);
  Stats &t = app.stats;
  t.peakRpm   = prefs.getUShort("pkRpm", 0);
  t.peakSpeed = prefs.getUShort("pkSpd", 0);
  t.maxEct    = prefs.getShort("pkEct", -40);
  t.minBatt   = prefs.getFloat("minBat", 99);
  t.maxBatt   = prefs.getFloat("maxBat", 0);
  t.engineSec = prefs.getULong("engSec", 0);
  t.oilSec    = prefs.getULong("oilSec", 0);
  t.starts    = prefs.getULong("starts", 0);
  prefs.end();
}

void actSaveSettings() {
  prefs.begin("gauge", false);
  const Settings &s = app.set;
  prefs.putBool("demo", s.demo);
  prefs.putBool("invTx", s.invTx);
  prefs.putBool("invCol", s.invColor);
  prefs.putBool("flipT", s.flipTouch);
  prefs.putBool("night", s.night);
  prefs.putUChar("bright", s.bright);
  prefs.putUShort("maxRpm", s.maxRpm);
  prefs.end();
}

void actSaveStats() {
  prefs.begin("gauge", false);
  const Stats &t = app.stats;
  prefs.putUShort("pkRpm", t.peakRpm);
  prefs.putUShort("pkSpd", t.peakSpeed);
  prefs.putShort("pkEct", t.maxEct);
  prefs.putFloat("minBat", t.minBatt);
  prefs.putFloat("maxBat", t.maxBatt);
  prefs.putULong("engSec", t.engineSec);
  prefs.putULong("oilSec", t.oilSec);
  prefs.putULong("starts", t.starts);
  prefs.end();
}

// ---------------------------------------------------------------- display helpers
void actApplyBrightness() { ledcWrite(BL_CH, map(app.set.bright, 0, 100, 0, 255)); }
void actApplyInvert()     { tft.invertDisplay(app.set.invColor); }
void actApplyInvTx()      { kline.setInvertTx(app.set.invTx); }

// ---------------------------------------------------------------- K-Line actions
static bool ensureLink() {
  if (app.set.demo) return false;
  if (kline.state() == K_CONNECTED) return true;
  app.link = LINK_WAKING;
  drawStatus();
  bool ok = kline.connect();
  app.link = ok ? LINK_OK : LINK_NONE;
  return ok;
}

void actReadDtc() {
  if (app.set.demo) {
    app.dtcCount = 2;
    strcpy(app.dtc[0], "07-01");
    strcpy(app.dtc[1], "11-01");
    strcpy(app.dtcMsg, "รหัสตัวอย่าง (ไม่ใช่ของจริง)");
    return;
  }
  if (!ensureLink()) { app.dtcCount = -1; strlcpy(app.dtcMsg, kline.lastError(), sizeof(app.dtcMsg)); return; }
  app.dtcCount = kline.readDtc(app.dtc, 24, app.dtcPast);
  if (app.dtcCount < 0) strlcpy(app.dtcMsg, kline.lastError(), sizeof(app.dtcMsg));
  else app.dtcMsg[0] = 0;
}

void actClearDtc() {
  if (app.set.demo) { app.dtcCount = 0; strcpy(app.dtcMsg, "สาธิต: ล้างแล้ว"); return; }
  if (!ensureLink()) { strlcpy(app.dtcMsg, kline.lastError(), sizeof(app.dtcMsg)); return; }
  if (kline.clearDtc()) { app.dtcCount = -2; strcpy(app.dtcMsg, "ล้างแล้ว ปิด-เปิดกุญแจ แล้วอ่านใหม่"); }
  else snprintf(app.dtcMsg, sizeof(app.dtcMsg), "ล้างไม่สำเร็จ: %s", kline.lastError());
}

void actReadRaw() {
  if (app.set.demo) { strcpy(app.rawMsg, "ปิดโหมดสาธิตในหน้าตั้งค่าก่อน"); return; }
  if (!ensureLink()) { strlcpy(app.rawMsg, kline.lastError(), sizeof(app.rawMsg)); return; }
  uint8_t b[64], n = 0;
  if (kline.readTable(app.rawTable, b, n)) snprintf(app.rawMsg, sizeof(app.rawMsg), "สำเร็จ %d ไบต์", n);
  else strlcpy(app.rawMsg, kline.lastError(), sizeof(app.rawMsg));
  memcpy(app.rawFrame, kline.lastFrame, kline.lastFrameLen);
  app.rawLen = kline.lastFrameLen;
}

// ---------------------------------------------------------------- WiFi update
static const char *otaPage =
  "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'></head>"
  "<body style='font-family:sans-serif;background:#0b1119;color:#e8eff6;padding:16px'>"
  "<h3>" BRAND_NAME "</h3><p>อัปเดตเฟิร์มแวร์</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='fw' accept='.bin'><br><br><input type='submit' value='อัปโหลด'></form>"
  "<p>ใช้ไฟล์ที่ลงท้ายด้วย -ota.bin (ไม่ใช่ -full.bin)</p></body></html>";

void actStartOta() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HondaGauge", "12345678");
  server = new WebServer(80);
  server->on("/", HTTP_GET, []() { server->send(200, "text/html; charset=utf-8", otaPage); });
  server->on("/update", HTTP_POST,
    []() {
      server->send(200, "text/plain; charset=utf-8", Update.hasError() ? "ไม่สำเร็จ" : "สำเร็จ กำลังรีสตาร์ต...");
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
}

// ---------------------------------------------------------------- data
static void makeDemo() {
  float t = millis() / 1000.0f;
  float ph = fmodf(t, 14.0f);
  EcuData &e = app.ecu;
  float rev;                                   // 0 = idle, 1 = full rev
  if (ph < 6) rev = 0;
  else if (ph < 10) rev = sinf((ph - 6) / 4 * 3.14159f / 2);
  else rev = 1 - (ph - 10) / 4;
  e.rpm = 1450 + (uint16_t)(rev * 7200) + (uint16_t)(30 + 30 * sinf(t * 7));
  e.tpsPct = (rev > 0.02f && ph < 10) ? 20 + rev * 75 : 0;
  e.tpsVolt = 0.45f + e.tpsPct * 0.04f;
  e.mapKpa = 32 + (int)(rev * 60);
  e.speedKmh = (int)(rev * 110);
  e.ectC = 86 + (int)(3 * sinf(t / 20));
  e.iatC = 34;
  e.battV = 13.9f + 0.15f * sinf(t / 3);
  e.hasInj = e.hasIgn = e.hasO2 = e.hasTrim = true;
  e.injMs = 2.4f + rev * 6.5f;
  e.ignDeg = 12.5f + rev * 22;
  e.o2V = 0.45f + 0.35f * sinf(t * 3);
  e.stft = 0.99f + 0.02f * sinf(t);
  e.ltft = 0.996f;
  e.valid = true;
  e.updatedMs = millis();
}

static void pollEcu() {
  if (app.set.demo) { app.link = LINK_DEMO; makeDemo(); return; }
  static uint32_t lastTry = 0;
  static int fails = 0;
  if (kline.state() != K_CONNECTED) {
    app.ecu.valid = false;
    app.ecu.hasInj = app.ecu.hasIgn = app.ecu.hasO2 = app.ecu.hasTrim = false;
    if (millis() - lastTry > 3000) { lastTry = millis(); ensureLink(); }
    else app.link = LINK_NONE;
    return;
  }
  if (kline.readLiveData(app.ecu)) { fails = 0; app.link = LINK_OK; }
  else if (++fails > 3) { fails = 0; kline.setInvertTx(app.set.invTx); app.link = LINK_NONE; app.ecu.valid = false; }
}

static void trackStats() {
  static uint32_t lastMs = 0, lastSave = 0, acc = 0;
  static bool running = false;
  uint32_t now = millis();
  uint32_t dt = now - lastMs;
  lastMs = now;
  const EcuData &e = app.ecu;
  if (!e.valid) return;
  Stats &s = app.stats;
  if (e.rpm > s.peakRpm) s.peakRpm = e.rpm;
  if (e.speedKmh > s.peakSpeed) s.peakSpeed = e.speedKmh;
  if (e.ectC > s.maxEct) s.maxEct = e.ectC;
  if (e.rpm > 600) {
    if (e.battV < s.minBatt) s.minBatt = e.battV;
    if (e.battV > s.maxBatt) s.maxBatt = e.battV;
  }
  if (app.set.demo) return;            // demo values are never saved
  bool nowRunning = e.rpm > 400;
  if (nowRunning) {
    acc += dt;
    while (acc >= 1000) { acc -= 1000; s.engineSec++; s.oilSec++; }
  }
  if (nowRunning && !running) s.starts++;
  if ((running && !nowRunning) || (nowRunning && now - lastSave > 60000)) { actSaveStats(); lastSave = now; }
  running = nowRunning;
}

static void sampleHistory() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();
  if (app.histLen == HIST_LEN) {
    memmove(app.histRpm, app.histRpm + 1, (HIST_LEN - 1) * sizeof(app.histRpm[0]));
    memmove(app.histEct, app.histEct + 1, (HIST_LEN - 1) * sizeof(app.histEct[0]));
    app.histLen--;
  }
  app.histRpm[app.histLen] = app.ecu.valid ? app.ecu.rpm : 0;
  app.histEct[app.histLen] = app.ecu.valid ? (int8_t)constrain(app.ecu.ectC, -40, 127) : 0;
  app.histLen++;
}

// ---------------------------------------------------------------- touch
static bool getTouch(int &x, int &y) {
  static uint32_t lastMs = 0;
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (millis() - lastMs < 250) return false;
  lastMs = millis();
  x = map(p.x, 200, 3700, 0, 320);
  y = map(p.y, 240, 3800, 0, 240);
  if (app.set.flipTouch) { x = 320 - x; y = 240 - y; }
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
  return true;
}

// ---------------------------------------------------------------- main
static void splash() {
  tft.fillScreen(C_BG);
  tft.drawArc(160, 96, 46, 40, 40, 320, C_ACCENT, C_BG, true);
  tft.drawArc(160, 96, 36, 34, 40, 320, C_LINE, C_BG, true);
  tft.drawWideLine(160, 96, 182, 74, 4, C_TEXT, C_BG);
  tft.fillCircle(160, 96, 5, C_TEXT);
  text(BRAND_NAME, 160, 166, MC_DATUM, C_TEXT, C_BG, F_THB);
  tft.drawFastHLine(70, 186, 180, C_ACCENT);
  text("HONDA K-LINE SMART GAUGE", 160, 200, MC_DATUM, C_DIM, C_BG, F_S);
  text("v" FW_VERSION, 160, 222, MC_DATUM, C_LINE, C_BG, F_S);
}

void setup() {
  Serial.begin(115200);
  loadAll();
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(app.set.invColor);
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CH);
  actApplyBrightness();
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);
  kline.begin(app.set.invTx);
  splash();
  delay(1500);
  app.link = app.set.demo ? LINK_DEMO : LINK_NONE;
  app.needFull = true;
}

void loop() {
  int x, y;
  if (app.screen == SCR_OTA) {
    if (app.needFull) { app.needFull = false; uiDrawFull(); }
    if (server) server->handleClient();
    if (getTouch(x, y) && x > 90 && x < 230 && y > 190) ESP.restart();
    return;
  }

  static uint32_t lastPoll = 0;
  if (millis() - lastPoll > (app.set.demo ? 80 : 100)) {
    lastPoll = millis();
    pollEcu();
    trackStats();
    sampleHistory();
    if (!app.needFull && millis() > app.holdUntil) uiUpdate();
  }

  if (getTouch(x, y)) {
    if (millis() < app.holdUntil) { app.holdUntil = 0; app.needFull = true; }
    else uiTouch(x, y);
  }

  if (app.needFull && millis() > app.holdUntil) {
    app.needFull = false;
    uiDrawFull();
  }
}
