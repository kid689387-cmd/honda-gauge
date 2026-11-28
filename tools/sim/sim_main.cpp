// Renders every screen to PPM images: tools/sim/run.sh
#include "app.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

TFT_eSPI tft;
App app;
uint32_t simNow = 100000;
uint32_t nowMs() { return simNow; }
void actReadDtc() {}
void actClearDtc() {}
void actReadRaw() {}
void actStartOta() {}
void actApplyBrightness() {}
void actApplyInvert() {}
void actApplyInvTx() {}
void actSaveSettings() {}
void actSaveStats() {}
void actSaveCal() {}
void actShowLogo() {}
void actSdInit() {}
void actSdLogToggle() {}
void actSdListFw() {}
void actSdFlash(int) {}
void actConnectEcu() {}

static void save(const char *name) {
  char p[128]; snprintf(p, sizeof(p), "%s/%s.ppm", getenv("OUT") ? getenv("OUT") : ".", name);
  FILE *f = fopen(p, "wb"); fprintf(f, "P6\n320 240\n255\n");
  for (uint16_t c : tft.fb) { uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static void shot(Screen s, const char *name) { app.screen = s; uiDrawFull(); save(name); }

int main() {
  EcuData &e = app.ecu;
  e.valid = true; e.rpm = 4450; e.speedKmh = 52; e.ectC = 86; e.iatC = 34; e.tpsPct = 38; e.tpsVolt = 1.92f;
  e.mapKpa = 62; e.battV = 13.8f; e.hasInj = true; e.injMs = 2.41f; e.hasIgn = true; e.ignDeg = 18.5f;
  e.hasO2 = true; e.o2V = 0.62f; e.hasTrim = true; e.stft = 0.982f; e.ltft = 0.996f;
  app.link = LINK_DEMO;
  app.stats.peakRpm = 9047; app.stats.peakSpeed = 103; app.stats.maxEct = 98; app.stats.minBatt = 12.6f; app.stats.maxBatt = 14.4f;
  app.stats.engineSec = 45 * 3600 + 1200; app.stats.starts = 312; app.stats.oilSec = 7 * 3600;
  for (int i = 0; i < HIST_LEN; i++) { app.histRpm[i] = 1500 + (uint16_t)(3000 * (0.5 + 0.5 * sin(i / 9.0))); app.histEct[i] = 80 + i / 30; }
  app.histLen = HIST_LEN;
  shot(SCR_GAUGE, "01_gauge");
  // live update on top of the previous frame: old digits must not remain
  e.rpm = 1180; e.speedKmh = 7; e.ectC = 101; e.battV = 12.4f; e.tpsPct = 0; e.mapKpa = 31; e.iatC = 29;
  uiUpdate(); save("01b_gauge_update");
  e.rpm = 4450; e.speedKmh = 52; e.ectC = 86; e.battV = 13.8f; e.tpsPct = 38; e.mapKpa = 62; e.iatC = 34;
  shot(SCR_MENU, "02_menu");
  shot(SCR_ALL, "03_all");
  e.rpm = 1520; e.tpsPct = 0; e.mapKpa = 33; e.injMs = 2.35f; e.battV = 14.1f;
  shot(SCR_CHECK, "04_check");
  app.dtcCount = 6; const char *c[] = {"01-01", "07-01", "08-01", "09-01", "11-01", "54-02"};
  for (int i = 0; i < 6; i++) strcpy(app.dtc[i], c[i]);
  shot(SCR_DTC, "05_dtc");
  shot(SCR_STATS, "06_stats");
  shot(SCR_GRAPH, "07_graph");
  uint8_t fr[] = {0x02,0x1A,0x71,0x11,0x05,0xF0,0x18,0x00,0x62,0x57,0xEF,0x10,0x91,0x63,0xFF,0xFF,0x8A,0x00,0x00,0x00,0x80,0x63,0x1A,0x51,0xDA};
  memcpy(app.rawFrame, fr, sizeof(fr)); app.rawLen = sizeof(fr); strcpy(app.rawMsg, "สำเร็จ 20 ไบต์");
  shot(SCR_RAW, "08_raw");
  for (int t = 0; t < 3; t++) { app.setTab = t; char n[16]; snprintf(n, sizeof(n), "09_set%d", t); shot(SCR_SET, n); }
  shot(SCR_OTA, "10_ota");
  app.set.model = 7; strcpy(app.ecmId, "01 02 32 0F 01"); app.setTab = 1; shot(SCR_SET, "13_set_ecu_model");
  for (int g = 0; g < 3; g++) { app.modelGroup = g; app.modelPage = g == 1 ? 1 : 0; char n[20]; snprintf(n, sizeof(n), "14_model%d", g); shot(SCR_MODEL, n); }
  app.set.maxRpm = 14000; e.rpm = 11250; shot(SCR_GAUGE, "11_gauge_14k"); app.set.maxRpm = 10000;
  e.valid = false; app.link = LINK_NONE; shot(SCR_GAUGE, "12_gauge_noecu");
  // SD card tab + firmware picker
  app.sdReady = true; app.sdSizeMB = 32; app.setTab = 3; app.sdMsg[0] = 0;
  shot(SCR_SET, "15_set_sd");
  app.logging = true; strcpy(app.logName, "log000.csv"); app.logRows = 1842;
  shot(SCR_SET, "16_set_sd_rec"); app.logging = false;
  app.sdFileCount = 3;
  strcpy(app.sdFiles[0], "honda-gauge-ili9341-full.bin");
  strcpy(app.sdFiles[1], "honda-gauge-st7789-full.bin");
  strcpy(app.sdFiles[2], "backup_0.5.1.bin");
  shot(SCR_SDFW, "17_sdfw");
  app.flashArmed = 0; app.flashArmedMs = simNow;
  shot(SCR_SDFW, "17b_sdfw_armed");
  app.calStep = 0; shot(SCR_CALIB, "18_calib_1");
  app.calStep = 1; shot(SCR_CALIB, "18_calib_2");
  app.calStep = 2; shot(SCR_CALIB, "18_calib_3");
  // show the display tab with the new calibrate entry
  app.setTab = 0; shot(SCR_SET, "19_set_display");
  return 0;
}
