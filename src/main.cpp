// อู่ช่างวัตร ฟรีดอม - Honda K-Line smart gauge for ESP32-2432S028 (CYD 2.8")
// Hardware side: display, touch, backlight, K-Line polling, demo data,
// statistics storage and WiFi update. Screens live in screens.cpp.
#include <Arduino.h>
#include <SPI.h>
#include <strings.h>   // strcasecmp
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <FS.h>
#include <SD.h>
#include "app.h"
#include "gfx.h"
#include "kline.h"
#include "models.h"
#include "touch.h"

#define BL_CH      0

// microSD on the CYD: hardware VSPI, default pins. The touch panel used to
// own VSPI, so it was moved to software SPI (touch.cpp) to free the bus.
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS     5

TFT_eSPI tft;
App app;
static SPIClass sdSpi(VSPI);
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
  s.showLogo  = prefs.getBool("logo", true);
  s.model     = prefs.getUChar("model", 255);
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
  app.calDone  = prefs.getBool("calDone", false);
  app.calHoriz = prefs.getBool("calHor", true);
  app.calHx0   = prefs.getInt("calHx0", 200);
  app.calHx1   = prefs.getInt("calHx1", 3700);
  app.calVy0   = prefs.getInt("calVy0", 240);
  app.calVy1   = prefs.getInt("calVy1", 3800);
  prefs.end();
}

void actSaveCal() {
  prefs.begin("gauge", false);
  prefs.putBool("calDone", app.calDone);
  prefs.putBool("calHor", app.calHoriz);
  prefs.putInt("calHx0", app.calHx0);
  prefs.putInt("calHx1", app.calHx1);
  prefs.putInt("calVy0", app.calVy0);
  prefs.putInt("calVy1", app.calVy1);
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
  prefs.putBool("logo", s.showLogo);
  prefs.putUChar("model", s.model);
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
  if (ok && !kline.readEcmId(app.ecmId, sizeof(app.ecmId))) app.ecmId[0] = 0;
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

// ECU TOOL > CONNECT: wake the ECU and read its ID. Read-only, nothing is written.
void actConnectEcu() {
  if (app.set.demo) { strcpy(app.ecuMsg, "ปิดโหมดสาธิต (ตั้งค่า > ECU) ก่อน"); return; }
  kline.connect();   // fresh wake-up even if a link already exists
  app.link = kline.state() == K_CONNECTED ? LINK_OK : LINK_NONE;
  if (app.link == LINK_OK) {
    if (!kline.readEcmId(app.ecmId, sizeof(app.ecmId))) app.ecmId[0] = 0;
    strcpy(app.ecuMsg, app.ecmId[0] ? "เชื่อมต่อสำเร็จ" : "เชื่อมต่อแล้ว แต่อ่านรหัสกล่องไม่ได้");
  } else {
    snprintf(app.ecuMsg, sizeof(app.ecuMsg), "ไม่สำเร็จ: %s", kline.lastError());
  }
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

// ---------------------------------------------------------------- WiFi update + logo
// The logo is converted in the phone's browser (resize to 320x240, RGB565),
// so the board only stores raw pixels: no JPG/PNG decoder needed here.
#define LOGO_FILE  "/logo.raw"
#define LOGO_BYTES (320 * 240 * 2)

// ---------------------------------------------------------------- SD card
// Card lives on VSPI (18/19/23/5). Touch was moved to software SPI so the two
// no longer fight over the bus.
static fs::File logFile;

static void streamLogo(fs::File &f) {
  static uint16_t row[320];
  tft.setSwapBytes(true);
  for (int y = 0; y < 240; y++) {
    f.read((uint8_t *)row, sizeof(row));
    tft.pushImage(0, y, 320, 1, row);
  }
  tft.setSwapBytes(false);
}

static bool sdHasLogo() {
  if (!app.sdReady) return false;
  fs::File f = SD.open("/logo.raw", FILE_READ);
  bool ok = f && f.size() == LOGO_BYTES;
  if (f) f.close();
  return ok;
}

static bool sdShowLogo() {         // draw /logo.raw from the card, if valid
  if (!app.sdReady) return false;
  fs::File f = SD.open("/logo.raw", FILE_READ);
  if (!f || f.size() != LOGO_BYTES) { if (f) f.close(); return false; }
  streamLogo(f);
  f.close();
  return true;
}

void actSdInit() {
  if (app.sdReady) SD.end();          // allow a clean re-mount
  app.sdReady = false;
  app.sdSizeMB = 0;
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSpi, 20000000)) { strcpy(app.sdMsg, "ไม่พบการ์ด"); return; }
  if (SD.cardType() == CARD_NONE) { SD.end(); strcpy(app.sdMsg, "ไม่พบการ์ด"); return; }
  app.sdReady = true;
  app.sdSizeMB = (uint16_t)(SD.cardSize() / (1024ULL * 1024ULL));
  snprintf(app.sdMsg, sizeof(app.sdMsg), "พร้อม %u MB", app.sdSizeMB);
}

void actSdListFw() {
  app.sdFileCount = -1;
  if (!app.sdReady) { strcpy(app.sdMsg, "ไม่พบการ์ด"); return; }
  fs::File dir = SD.open("/");
  if (!dir) return;
  int n = 0;
  for (fs::File e = dir.openNextFile(); e && n < 8; e = dir.openNextFile()) {
    if (!e.isDirectory()) {
      const char *nm = e.name();
      const char *slash = strrchr(nm, '/');
      if (slash) nm = slash + 1;
      size_t L = strlen(nm);
      if (L > 4 && strcasecmp(nm + L - 4, ".bin") == 0 && nm[0] != '.') {
        strlcpy(app.sdFiles[n], nm, sizeof(app.sdFiles[n]));
        n++;
      }
    }
    e.close();
  }
  dir.close();
  app.sdFileCount = n ? n : -1;
}

static const char *logHeader =
  "ms,rpm,tps_pct,ect_c,iat_c,map_kpa,speed,batt_v,inj_ms,ign_deg,o2_v,stft,ltft\n";

void actSdLogToggle() {
  if (app.logging) {                 // stop
    if (logFile) logFile.close();
    app.logging = false;
    snprintf(app.sdMsg, sizeof(app.sdMsg), "หยุด log แล้ว (%lu แถว)", (unsigned long)app.logRows);
    return;
  }
  if (!app.sdReady) { strcpy(app.sdMsg, "ใส่การ์ดก่อน"); return; }
  // pick a non-clashing name log000.csv..log999.csv
  char path[24];
  int i = 0;
  for (; i < 1000; i++) { snprintf(path, sizeof(path), "/log%03d.csv", i); if (!SD.exists(path)) break; }
  logFile = SD.open(path, FILE_WRITE);
  if (!logFile) { strcpy(app.sdMsg, "เปิดไฟล์ไม่ได้"); return; }
  logFile.print(logHeader);
  app.logRows = 0;
  app.logging = true;
  strlcpy(app.logName, path + 1, sizeof(app.logName));   // drop leading '/'
  snprintf(app.sdMsg, sizeof(app.sdMsg), "บันทึกลง %s", app.logName);
}

static void sdLogRow() {
  if (!app.logging || !logFile) return;
  const EcuData &e = app.ecu;
  char line[160];
  int n = snprintf(line, sizeof(line),
    "%lu,%u,%.0f,%d,%d,%d,%d,%.2f,%.2f,%.1f,%.2f,%.3f,%.3f\n",
    (unsigned long)millis(), e.rpm, e.tpsPct, e.ectC, e.iatC, e.mapKpa, e.speedKmh,
    e.battV, e.injMs, e.ignDeg, e.o2V, e.stft, e.ltft);
  logFile.write((const uint8_t *)line, n);
  app.logRows++;
  if ((app.logRows & 0x1F) == 0) logFile.flush();        // persist every 32 rows
}

// Blocking firmware write from a .bin on the card, with a progress screen.
void actSdFlash(int idx) {
  if (!app.sdReady || idx < 0 || idx >= 8) return;
  char path[48]; snprintf(path, sizeof(path), "/%s", app.sdFiles[idx]);
  fs::File f = SD.open(path, FILE_READ);
  if (!f) { strcpy(app.sdMsg, "เปิดไฟล์ไม่ได้"); app.flashPct = -1; return; }
  size_t total = f.size();
  if (total < 0x10000 || !Update.begin(total)) {
    f.close(); strcpy(app.sdMsg, "ไฟล์เล็กเกินไป/พื้นที่ไม่พอ"); app.flashPct = -1; return;
  }
  tft.fillScreen(C_BG);
  text("กำลังเขียนเฟิร์มแวร์", 160, 70, MC_DATUM, C_TEXT, C_BG, F_TH);
  text("ห้ามดับไฟ / ถอดการ์ด", 160, 96, MC_DATUM, C_WARN, C_BG, F_TH);
  uint8_t buf[2048];
  size_t done = 0;
  int lastPct = -1;
  while (done < total) {
    size_t got = f.read(buf, sizeof(buf));
    if (got == 0) break;
    if (Update.write(buf, got) != got) break;
    done += got;
    int pct = (int)(done * 100 / total);
    if (pct != lastPct) {
      lastPct = pct; app.flashPct = pct;
      tft.fillRect(40, 130, 240, 16, C_PANEL2);
      tft.fillRect(40, 130, 240 * pct / 100, 16, C_OK);
      char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
      text(b, 160, 162, MC_DATUM, C_TEXT, C_BG, F_S, 80);
    }
  }
  f.close();
  bool ok = (done == total) && Update.end(true) && !Update.hasError();
  app.flashPct = -1;
  if (ok) {
    text("สำเร็จ กำลังรีสตาร์ต...", 160, 196, MC_DATUM, C_OK, C_BG, F_TH);
    delay(1200); ESP.restart();
  } else {
    Update.abort();
    strcpy(app.sdMsg, "เขียนไม่สำเร็จ ไฟล์อาจไม่ใช่ -full.bin");
    text("ไม่สำเร็จ แตะเพื่อกลับ", 160, 196, MC_DATUM, C_BAD, C_BG, F_TH);
    app.needFull = true;
  }
}

static const char otaPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Gauge</title>
<style>body{font-family:sans-serif;background:#0b1119;color:#e8eff6;margin:0;padding:16px}
h2{margin:0 0 4px}section{background:#18233a;border:1px solid #425c7a;border-radius:10px;padding:14px;margin:14px 0}
button,input[type=submit]{background:#5c92c4;color:#0b1119;border:0;border-radius:8px;padding:10px 16px;font-size:16px;margin:6px 6px 0 0}
.red{background:#e8484e;color:#fff}canvas{width:100%;max-width:320px;border:1px solid #425c7a;border-radius:6px;display:block;margin:10px 0}
select,input{font-size:15px;margin:6px 0}small{color:#889eb6}</style></head><body>
<h2>)HTML" BRAND_NAME R"HTML(</h2><small>Honda K-Line smart gauge</small>
<section><b>เปลี่ยนโลโก้ตอนเปิดเครื่อง</b><br>
<input type="file" id="img" accept="image/*"><br>
<select id="fit"><option value="contain">พอดีจอ (เห็นทั้งรูป)</option><option value="cover">เต็มจอ (ตัดขอบ)</option></select>
<input type="color" id="bg" value="#0b1119"> สีพื้นหลัง
<canvas id="cv" width="320" height="240"></canvas>
<button id="up">บันทึกโลโก้</button><button id="del" class="red">ลบโลโก้</button>
<p id="st"></p></section>
<section><b>อัปเดตเฟิร์มแวร์</b><br><small>ใช้ไฟล์ที่ลงท้ายด้วย -ota.bin</small>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin"><br><input type="submit" value="อัปโหลดเฟิร์มแวร์"></form></section>
<script>
var cv=document.getElementById('cv'),cx=cv.getContext('2d'),im=null,st=document.getElementById('st');
function draw(){cx.fillStyle=document.getElementById('bg').value;cx.fillRect(0,0,320,240);if(!im)return;
var f=document.getElementById('fit').value,s=f=='cover'?Math.max(320/im.width,240/im.height):Math.min(320/im.width,240/im.height);
var w=im.width*s,h=im.height*s;cx.drawImage(im,(320-w)/2,(240-h)/2,w,h);}
document.getElementById('img').onchange=function(e){var r=new FileReader();r.onload=function(){im=new Image();im.onload=draw;im.src=r.result;};r.readAsDataURL(e.target.files[0]);};
document.getElementById('fit').onchange=draw;document.getElementById('bg').oninput=draw;draw();
document.getElementById('up').onclick=function(){if(!im){st.textContent='เลือกรูปก่อน';return;}
var d=cx.getImageData(0,0,320,240).data,b=new Uint8Array(320*240*2);
for(var i=0,j=0;i<d.length;i+=4,j+=2){var v=((d[i]&248)<<8)|((d[i+1]&252)<<3)|(d[i+2]>>3);b[j]=v&255;b[j+1]=v>>8;}
var fd=new FormData();fd.append('logo',new Blob([b]),'logo.raw');st.textContent='กำลังส่ง...';
fetch('/logo',{method:'POST',body:fd}).then(function(r){return r.text();}).then(function(t){st.textContent=t;}).catch(function(){st.textContent='ส่งไม่สำเร็จ';});};
document.getElementById('del').onclick=function(){fetch('/logo/delete',{method:'POST'}).then(function(r){return r.text();}).then(function(t){st.textContent=t;});};
</script></body></html>)HTML";

static fs::File logoUpload;
static size_t logoBytes = 0;

void actShowLogo() {
  if (sdShowLogo()) return;           // card takes priority
  fs::File f = LittleFS.open(LOGO_FILE, "r");
  if (!f || f.size() != LOGO_BYTES) { if (f) f.close(); return; }
  streamLogo(f);
  f.close();
}

void actStartOta() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HondaGauge", "12345678");
  server = new WebServer(80);
  server->on("/", HTTP_GET, []() { server->send_P(200, "text/html; charset=utf-8", otaPage); });
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
  server->on("/logo", HTTP_POST,
    []() {
      bool ok = logoBytes == LOGO_BYTES;
      if (ok) { LittleFS.remove(LOGO_FILE); LittleFS.rename("/logo.tmp", LOGO_FILE); app.hasLogo = true; }
      else LittleFS.remove("/logo.tmp");
      server->send(200, "text/plain; charset=utf-8", ok ? "บันทึกโลโก้แล้ว กด ออก บนจอเพื่อดู" : "ไฟล์ไม่ครบ ลองใหม่");
    },
    []() {
      HTTPUpload &up = server->upload();
      if (up.status == UPLOAD_FILE_START) { logoBytes = 0; logoUpload = LittleFS.open("/logo.tmp", "w"); }
      else if (up.status == UPLOAD_FILE_WRITE && logoUpload) logoBytes += logoUpload.write(up.buf, up.currentSize);
      else if (up.status == UPLOAD_FILE_END && logoUpload) logoUpload.close();
    });
  server->on("/logo/delete", HTTP_POST, []() {
    LittleFS.remove(LOGO_FILE);
    app.hasLogo = false;
    server->send(200, "text/plain; charset=utf-8", "ลบโลโก้แล้ว");
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
  int ra, rb;                         // ra = 0x91 channel, rb = 0xD1 channel
  if (!touchGetRaw(ra, rb)) return false;
  if (millis() - lastMs < 250) return false;
  lastMs = millis();
  if (app.calDone) {
    int h = app.calHoriz ? ra : rb;   // horizontal-axis channel
    int v = app.calHoriz ? rb : ra;   // vertical-axis channel
    x = map(h, app.calHx0, app.calHx1, 16, 303);
    y = map(v, app.calVy0, app.calVy1, 16, 223);
  } else {
    x = map(ra, 200, 3700, 0, 320);   // rough default until calibrated
    y = map(rb, 240, 3800, 0, 240);
    if (app.set.flipTouch) { x = 320 - x; y = 240 - y; }
  }
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
  return true;
}

// ---- touch calibration: tap 3 corners, derive axis order + scale ----------
static void computeCal() {
  int dA = abs(app.calA[1] - app.calA[0]);   // a-channel change TL->TR (horizontal)
  int dB = abs(app.calB[1] - app.calB[0]);   // b-channel change TL->TR
  app.calHoriz = (dA >= dB);                 // whichever moves more is horizontal
  if (app.calHoriz) {
    app.calHx0 = app.calA[0]; app.calHx1 = app.calA[1];   // a at x=16, x=303
    app.calVy0 = app.calB[0]; app.calVy1 = app.calB[2];   // b at y=16, y=223
  } else {
    app.calHx0 = app.calB[0]; app.calHx1 = app.calB[1];
    app.calVy0 = app.calA[0]; app.calVy1 = app.calA[2];
  }
  if (app.calHx1 == app.calHx0) app.calHx1 += 1;          // guard map() divide-by-zero
  if (app.calVy1 == app.calVy0) app.calVy1 += 1;
}

static void handleCalib() {
  static uint32_t lastTap = 0;
  int ra, rb;
  if (!touchGetRaw(ra, rb)) return;
  if (millis() - lastTap < 600) return;       // debounce between corners
  lastTap = millis();
  app.calA[app.calStep] = ra;
  app.calB[app.calStep] = rb;
  app.calStep++;
  if (app.calStep >= 3) {
    computeCal();
    app.calDone = true;
    actSaveCal();
    app.calStep = 0;
    app.screen = SCR_SET; app.setTab = 0;
  }
  app.needFull = true;                         // draw next crosshair, or the settings page
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
  if (app.set.model < BIKE_COUNT) {
    char m[64];
    snprintf(m, sizeof(m), "%s  (%s)", bikeModels[app.set.model].name, bikeModels[app.set.model].code);
    text(m, 160, 200, MC_DATUM, C_TEAL, C_BG, F_TH);
  } else text("HONDA K-LINE SMART GAUGE", 160, 200, MC_DATUM, C_DIM, C_BG, F_S);
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
  touchBegin();
  kline.begin(app.set.invTx);
  actSdInit();                       // mount the card (VSPI) if present
  bool liteLogo = false;
  if (LittleFS.begin(true)) {
    fs::File f = LittleFS.open(LOGO_FILE, "r");
    liteLogo = f && f.size() == LOGO_BYTES;
    if (f) f.close();
  }
  app.hasLogo = liteLogo || sdHasLogo();
  if (app.hasLogo && app.set.showLogo) { actShowLogo(); delay(2500); }
  else { splash(); delay(1500); }
  app.link = app.set.demo ? LINK_DEMO : LINK_NONE;
  if (!app.calDone) { app.screen = SCR_CALIB; app.calStep = 0; }   // first run: calibrate touch
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
  if (app.screen == SCR_CALIB) {
    if (app.needFull) { app.needFull = false; uiDrawFull(); }
    handleCalib();                     // reads raw touch, advances corners
    return;
  }

  static uint32_t lastPoll = 0;
  if (millis() - lastPoll > (app.set.demo ? 80 : 100)) {
    lastPoll = millis();
    pollEcu();
    trackStats();
    sampleHistory();
    if (app.logging && app.ecu.valid && !app.set.demo) {
      static uint32_t lastLog = 0;
      if (millis() - lastLog >= 200) { lastLog = millis(); sdLogRow(); }  // ~5 Hz
    }
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
