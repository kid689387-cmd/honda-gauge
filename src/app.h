// Shared state between the UI (screens.cpp) and the hardware side (main.cpp).
#pragma once
#include <stdint.h>

#define BRAND_NAME "อู่ช่างวัตร ฟรีดอม"
#define FW_VERSION "0.3.2"

struct EcuData {
  bool     valid = false;
  uint16_t rpm = 0;
  float    tpsVolt = 0, tpsPct = 0;
  int      ectC = 0, iatC = 0, mapKpa = 0, speedKmh = 0;
  float    battV = 0;
  // fields not yet confirmed for real bikes (phase 2): shown only when has* is set
  bool     hasInj = false, hasIgn = false, hasO2 = false, hasTrim = false;
  float    injMs = 0, ignDeg = 0, o2V = 0, stft = 1, ltft = 1;
  uint32_t updatedMs = 0;
};

struct Settings {
  bool     demo = true, invTx = false, invColor = false, flipTouch = false, night = false;
  uint8_t  bright = 100;          // percent
  uint16_t maxRpm = 10000;
};

struct Stats {
  uint16_t peakRpm = 0;
  uint16_t peakSpeed = 0;
  int16_t  maxEct = -40;
  float    minBatt = 99, maxBatt = 0;
  uint32_t engineSec = 0, oilSec = 0, starts = 0;
};

enum Screen { SCR_GAUGE, SCR_MENU, SCR_ALL, SCR_CHECK, SCR_DTC, SCR_STATS, SCR_GRAPH, SCR_RAW, SCR_SET, SCR_OTA };
enum Link { LINK_DEMO, LINK_OK, LINK_WAKING, LINK_NONE };

#define HIST_LEN 150

struct App {
  Screen   screen = SCR_GAUGE;
  bool     needFull = true;
  EcuData  ecu;
  Settings set;
  Stats    stats;
  Link     link = LINK_DEMO;
  // DTC
  char     dtc[24][8];
  int      dtcCount = -2;          // -2 never read, -1 error
  bool     dtcPast = false;
  uint32_t clearArmedMs = 0;
  char     dtcMsg[192] = "";
  // RAW
  uint8_t  rawTable = 0x11;
  char     rawMsg[192] = "";
  uint8_t  rawFrame[64];
  uint8_t  rawLen = 0;
  // settings tab, stats confirm
  int      setTab = 0;
  uint32_t resetArmedMs = 0;
  int      resetArmedWhat = 0;
  // history for the graph
  uint16_t histRpm[HIST_LEN];
  int8_t   histEct[HIST_LEN];
  int      histLen = 0;
  int      touchX = -1, touchY = -1;
  char     lastError[128] = "";
  uint32_t holdUntil = 0;     // keep a test screen up until this time
};

extern App app;

// UI entry points (screens.cpp)
void uiDrawFull();          // redraw the whole current screen
void uiUpdate();            // refresh live values on the current screen
void uiTouch(int x, int y); // handle a tap
void uiColorTest();

// Hardware actions (main.cpp, stubbed in the simulator)
void actReadDtc();
void actClearDtc();
void actReadRaw();
void actStartOta();
void actApplyBrightness();
void actApplyInvert();
void actApplyInvTx();
void actSaveSettings();
void actSaveStats();
uint32_t nowMs();
