// All screens: drawing, live updates and touch handling.
#include "app.h"
#include "gfx.h"
#include "models.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ================================================================ GAUGE
static const int G_CX = 160, G_CY = 146, G_R = 104, G_IR = 86;
static const int G_A0 = 70, G_A1 = 290;          // sweep, 0 deg = 6 o'clock, clockwise
static const int G_SEG = 36;
static int gLit = -1;

static uint16_t segColor(int i) {
  float t = (float)i / (G_SEG - 1);
  if (t < 0.45f) return C_TEAL;
  if (t < 0.65f) return C_ACCENT;
  if (t < 0.82f) return C_PURPLE;
  return C_BAD;
}

static void drawSeg(int i, bool lit) {
  float span = (float)(G_A1 - G_A0) / G_SEG;
  int a = G_A0 + (int)(i * span) + 1;
  int b = G_A0 + (int)((i + 1) * span) - 1;
  tft.drawArc(G_CX, G_CY, G_R, G_IR, a, b, lit ? segColor(i) : C_PANEL2, C_BG, false);
}

static void gaugeXY(float ang, int r, int &x, int &y) {
  float rad = ang * 3.14159f / 180.0f;
  x = G_CX - (int)(r * sinf(rad));
  y = G_CY + (int)(r * cosf(rad));
}

static bool labelShown(int k) {
  int top = app.set.maxRpm / 1000;
  return k > 0 && k < top && (top <= 10 || k % 2 == 0);   // every other mark on long scales
}

static void drawScaleLabel(int k) {
  if (!labelShown(k)) return;
  int top = app.set.maxRpm / 1000;
  float ang = G_A0 + (float)(G_A1 - G_A0) * k / top;
  int x, y; gaugeXY(ang, G_IR - 12, x, y);
  char s[4]; snprintf(s, sizeof(s), "%d", k);
  text(s, x, y, MC_DATUM, k * 1000 >= app.set.maxRpm * 0.82f ? C_BAD : C_DIM, C_BG, F_S);
}

static const int TILE_Y = 190, TILE_H = 48;
static const char *tileName[] = {"BAT V", "TPS %", "MAP kPa", "IAT °C"};

static void drawGaugeFull() {
  tft.fillScreen(C_BG);
  header(BRAND_NAME, C_ACCENT);
  // thin outer ring
  tft.drawArc(G_CX, G_CY, G_R + 4, G_R + 3, G_A0, G_A1, C_LINE, C_BG, true);
  gLit = -1;
  for (int i = 0; i < G_SEG; i++) drawSeg(i, false);
  int top = app.set.maxRpm / 1000;
  for (int k = 1; k < top; k++) drawScaleLabel(k);
  text("x1000 RPM", G_CX, G_CY + 37, MC_DATUM, C_DIM, C_BG, F_S);
  // speed (left) and engine temp (right)
  text("ความเร็ว", 6, 30, TL_DATUM, C_WARN, C_BG, F_TH);
  text("km/h", 6, 80, TL_DATUM, C_DIM, C_BG, F_S);
  text("เครื่อง", 314, 30, TR_DATUM, C_WARN, C_BG, F_TH);
  text("°C", 314, 80, TR_DATUM, C_DIM, C_BG, F_S);
  for (int i = 0; i < 4; i++) {
    int x = 3 + i * 79;
    panel(x, TILE_Y, 76, TILE_H, i == 0 ? C_WARN : C_ACCENT);
    text(tileName[i], x + 7, TILE_Y + 6, TL_DATUM, C_DIM, C_PANEL, F_S);
  }
}

static void updateGauge() {
  const EcuData &e = app.ecu;
  bool ok = e.valid;
  int lit = ok ? (int)((float)e.rpm * G_SEG / app.set.maxRpm + 0.5f) : 0;
  if (lit > G_SEG) lit = G_SEG;
  if (gLit < 0) gLit = 0;
  for (int i = lit; i < gLit; i++) drawSeg(i, false);
  for (int i = gLit; i < lit; i++) drawSeg(i, true);
  gLit = lit;

  char b[16];
  if (ok) snprintf(b, sizeof(b), "%u", e.rpm); else strcpy(b, "----");
  text(b, G_CX, G_CY + 2, MC_DATUM, ok ? C_TEXT : C_DIM, C_BG, F_XL, 150);
  for (int k = 1; k < app.set.maxRpm / 1000; k++) {   // marks beside the number get
    float ang = G_A0 + (float)(G_A1 - G_A0) * k / (app.set.maxRpm / 1000);  // cleared with it
    if (ang < 125 || ang > 235) drawScaleLabel(k);
  }

  if (ok) snprintf(b, sizeof(b), "%d", e.speedKmh); else strcpy(b, "--");
  text(b, 6, 54, TL_DATUM, C_TEXT, C_BG, F_MD, 62);
  if (ok) snprintf(b, sizeof(b), "%d", e.ectC); else strcpy(b, "--");
  text(b, 314, 54, TR_DATUM, ok && e.ectC > 105 ? C_BAD : C_WARN, C_BG, F_MD, 62);

  for (int i = 0; i < 4; i++) {
    int x = 3 + i * 79;
    if (!ok) strcpy(b, "--");
    else if (i == 0) snprintf(b, sizeof(b), "%.1f", e.battV);
    else if (i == 1) snprintf(b, sizeof(b), "%d", (int)e.tpsPct);
    else if (i == 2) snprintf(b, sizeof(b), "%d", e.mapKpa);
    else snprintf(b, sizeof(b), "%d", e.iatC);
    uint16_t c = C_TEXT;
    if (ok && i == 0 && (e.battV < 12.0f || e.battV > 15.0f)) c = C_BAD;
    text(b, x + 71, TILE_Y + 34, MR_DATUM, c, C_PANEL, F_MD, 60);
  }
}

// ================================================================ MENU
struct MenuItem { const char *name; Icon ic; uint16_t col; Screen to; };
static const MenuItem menu[8] = {
  {"เกจ",       IC_GAUGE, C_TEAL,    SCR_GAUGE},
  {"ข้อมูล",     IC_LIST,  C_ACCENT,  SCR_ALL},
  {"ตรวจเช็ก",   IC_CHECK, C_OK,      SCR_CHECK},
  {"รหัสเสีย",   IC_WARN,  C_BAD,     SCR_DTC},
  {"สถิติ",      IC_PEAK,  C_WARN,    SCR_STATS},
  {"กราฟ",      IC_GRAPH, C_PURPLE,  SCR_GRAPH},
  {"K-LINE",    IC_WAVE,  C_ACCENT2, SCR_RAW},
  {"ตั้งค่า",     IC_GEAR,  C_DIM,     SCR_SET},
};

static void menuTile(int i, int &x, int &y) { x = 4 + (i % 4) * 79; y = 32 + (i / 4) * 86; }

static void drawMenu() {
  tft.fillScreen(C_BG);
  header("เมนู", C_ACCENT);
  for (int i = 0; i < 8; i++) {
    int x, y; menuTile(i, x, y);
    tft.fillRoundRect(x, y, 75, 80, 6, C_PANEL);
    tft.drawRoundRect(x, y, 75, 80, 6, menu[i].col);
    tft.drawRoundRect(x + 1, y + 1, 73, 78, 5, C_PANEL2);
    tft.fillRect(x + 8, y, 20, 2, menu[i].col);
    icon(menu[i].ic, x + 37, y + 30, menu[i].col);
    text(menu[i].name, x + 37, y + 64, MC_DATUM, C_TEXT, C_PANEL, F_TH);
  }
  button(4, 208, 312, 30, "< กลับหน้าเกจ", C_ACCENT);
}

// ================================================================ ALL DATA
struct Item { const char *name; const char *unit; };
static const Item allItems[14] = {
  {"RPM", "rpm"}, {"SPEED", "km/h"}, {"ECT", "C"}, {"IAT", "C"}, {"TPS", "%"}, {"VBAT", "V"}, {"MAP", "kPa"},
  {"INJ", "ms"}, {"IGN", "deg"}, {"O2", "V"}, {"STFT", "x"}, {"LTFT", "x"}, {"TPS V", "V"}, {"PEAK", "rpm"},
};
static const uint16_t allCol[14] = {C_BAD, C_ACCENT, C_WARN, C_TEAL, C_OK, C_ACCENT2, C_PURPLE,
                                    C_TEAL, C_ACCENT, C_PURPLE, C_TEAL, C_TEAL, C_OK, C_WARN};

static void allCell(int i, int &x, int &y) { x = (i < 7) ? 3 : 162; y = 30 + (i % 7) * 25; }

static void drawAll() {
  tft.fillScreen(C_BG);
  header("ข้อมูลทั้งหมด", C_ACCENT);
  for (int i = 0; i < 14; i++) {
    int x, y; allCell(i, x, y);
    tft.fillRect(x, y, 155, 23, C_PANEL);
    tft.fillRect(x, y + 3, 3, 17, allCol[i]);
    text(allItems[i].name, x + 8, y + 12, ML_DATUM, C_DIM, C_PANEL, F_S);
    text(allItems[i].unit, x + 152, y + 12, MR_DATUM, C_DIM, C_PANEL, F_S);
  }
  backButton();
  text("ค่า -- รอเทียบกับรถจริง", 116, 214, TL_DATUM, C_DIM, C_BG, F_TH);
}

static void fmtAll(int i, char *b, size_t n) {
  const EcuData &e = app.ecu;
  if (!e.valid && i != 13) { strcpy(b, "--"); return; }
  switch (i) {
    case 0: snprintf(b, n, "%u", e.rpm); break;
    case 1: snprintf(b, n, "%d", e.speedKmh); break;
    case 2: snprintf(b, n, "%d", e.ectC); break;
    case 3: snprintf(b, n, "%d", e.iatC); break;
    case 4: snprintf(b, n, "%d", (int)e.tpsPct); break;
    case 5: snprintf(b, n, "%.1f", e.battV); break;
    case 6: snprintf(b, n, "%d", e.mapKpa); break;
    case 7: if (e.hasInj) snprintf(b, n, "%.2f", e.injMs); else strcpy(b, "--"); break;
    case 8: if (e.hasIgn) snprintf(b, n, "%.1f", e.ignDeg); else strcpy(b, "--"); break;
    case 9: if (e.hasO2) snprintf(b, n, "%.3f", e.o2V); else strcpy(b, "--"); break;
    case 10: if (e.hasTrim) snprintf(b, n, "%.3f", e.stft); else strcpy(b, "--"); break;
    case 11: if (e.hasTrim) snprintf(b, n, "%.3f", e.ltft); else strcpy(b, "--"); break;
    case 12: snprintf(b, n, "%.2f", e.tpsVolt); break;
    case 13: snprintf(b, n, "%u", app.stats.peakRpm); break;
  }
}

static void updateAll() {
  drawStatus();
  char b[16];
  for (int i = 0; i < 14; i++) {
    int x, y; allCell(i, x, y);
    fmtAll(i, b, sizeof(b));
    text(b, x + 114, y + 12, MR_DATUM, i == 2 ? C_WARN : C_TEXT, C_PANEL, F_TH, 52);
  }
}

// ================================================================ CHECK
// General guide values for a warm engine at idle. Not model specific.
struct Check { const char *name; const char *hint; };
static const Check checks[7] = {
  {"BAT",  "ติดเครื่อง 13.0-14.8 V"},
  {"ECT",  "อุ่นแล้ว 80-105 °C"},
  {"IAT",  "ใกล้อุณหภูมิอากาศ"},
  {"TPS",  "0% ตอนปล่อยคันเร่ง"},
  {"RPM",  "เดินเบา 1300-1900"},
  {"MAP",  "เดินเบา 25-45 kPa"},
  {"INJ",  "เดินเบา 2.0-3.0 ms"},
};

static int checkState(int i, char *v, size_t n) {   // 0 ok, 1 warn, 2 bad, 3 n/a
  const EcuData &e = app.ecu;
  if (!e.valid) { strcpy(v, "--"); return 3; }
  switch (i) {
    case 0: snprintf(v, n, "%.1f", e.battV);
      return e.rpm < 500 ? (e.battV >= 12.3f ? 0 : 2) : (e.battV >= 13.0f && e.battV <= 14.8f ? 0 : 2);
    case 1: snprintf(v, n, "%d", e.ectC); return e.ectC > 110 ? 2 : (e.ectC < 70 ? 1 : 0);
    case 2: snprintf(v, n, "%d", e.iatC); return (e.iatC < -10 || e.iatC > 70) ? 2 : 0;
    case 3: snprintf(v, n, "%d%%", (int)e.tpsPct); return e.tpsPct <= 2 ? 0 : 1;
    case 4: snprintf(v, n, "%u", e.rpm); return (e.rpm >= 1300 && e.rpm <= 1900) ? 0 : 1;
    case 5: snprintf(v, n, "%d", e.mapKpa); return (e.mapKpa >= 25 && e.mapKpa <= 45) ? 0 : 1;
    case 6: if (!e.hasInj) { strcpy(v, "--"); return 3; }
      snprintf(v, n, "%.2f", e.injMs); return (e.injMs >= 2.0f && e.injMs <= 3.0f) ? 0 : 1;
  }
  return 3;
}

static void drawCheck() {
  tft.fillScreen(C_BG);
  header("ตรวจเช็ก", C_OK);
  text("ค่าแนะนำทั่วไป ตอนเครื่องเดินเบา", 6, 30, TL_DATUM, C_DIM, C_BG, F_TH);
  for (int i = 0; i < 7; i++) {
    int y = 52 + i * 22;
    tft.fillRect(3, y, 314, 20, C_PANEL);
    text(checks[i].name, 12, y + 10, ML_DATUM, C_DIM, C_PANEL, F_S);
    text(checks[i].hint, 116, y + 1, TL_DATUM, C_TEXT, C_PANEL, F_TH);
  }
  backButton();
}

static void updateCheck() {
  drawStatus();
  static const uint16_t sc[4] = {C_OK, C_WARN, C_BAD, C_LINE};
  char v[16];
  for (int i = 0; i < 7; i++) {
    int y = 52 + i * 22;
    int s = checkState(i, v, sizeof(v));
    tft.fillRect(3, y + 2, 3, 16, sc[s]);
    text(v, 108, y + 10, MR_DATUM, s == 2 ? C_BAD : (s == 1 ? C_WARN : C_TEXT), C_PANEL, F_S, 58);
    tft.fillCircle(306, y + 10, 5, sc[s]);
  }
}

// ================================================================ DTC
struct DtcText { int main, sub; const char *th; };
// Honda PGM-FI motorcycle codes (main-sub). Meaning can differ a little by model.
static const DtcText dtcText[] = {
  {1, 1, "MAP เซนเซอร์ แรงดันต่ำ"},       {1, 2, "MAP เซนเซอร์ แรงดันสูง"},
  {2, 1, "MAP เซนเซอร์ ผิดปกติ"},
  {7, 1, "อุณหภูมิเครื่อง แรงดันต่ำ"}, {7, 2, "อุณหภูมิเครื่อง แรงดันสูง"},
  {8, 1, "ลิ้นเร่ง TPS แรงดันต่ำ"},  {8, 2, "ลิ้นเร่ง TPS แรงดันสูง"},
  {9, 1, "อุณหภูมิอากาศ แรงดันต่ำ"},  {9, 2, "อุณหภูมิอากาศ แรงดันสูง"},
  {11, 1, "ไม่มีสัญญาณความเร็ว (VSS)"},
  {12, 1, "วงจรหัวฉีดผิดปกติ"},
  {21, 1, "เซนเซอร์ O2 ผิดปกติ"},
  {23, 1, "ฮีตเตอร์เซนเซอร์ O2 ผิดปกติ"},
  {29, 1, "วาล์วควบคุมรอบเดินเบา (IACV)"},
  {33, 2, "หน่วยความจำ ECU ผิดปกติ"},
  {52, 1, "เซนเซอร์ข้อเหวี่ยง (CKP)"},
  {54, 1, "เซนเซอร์ล้ม แรงดันต่ำ"},       {54, 2, "เซนเซอร์ล้ม แรงดันสูง"},
  {86, 1, "การสื่อสารภายในผิดปกติ"},
};

static const char *dtcDescribe(const char *code, char *shortCode) {
  int m = 0, s = 0;
  sscanf(code, "%d-%d", &m, &s);
  sprintf(shortCode, "%d-%d", m, s);
  for (const DtcText &d : dtcText)
    if (d.main == m && d.sub == s) return d.th;
  return "ไม่มีคำอธิบายในเครื่อง";
}

static void drawDtc() {
  tft.fillScreen(C_BG);
  header("รหัสเสีย", C_BAD);
  tft.fillRect(3, 29, 3, 22, C_BAD);
  char b[32];
  if (app.dtcCount >= 0) snprintf(b, sizeof(b), "%d", app.dtcCount); else strcpy(b, "-");
  text("รหัสเสียทั้งหมด", 12, 31, TL_DATUM, C_ACCENT2, C_BG, F_TH);
  text(b, 156, 41, MC_DATUM, C_BAD, C_BG, F_MD);
  button(206, 28, 110, 26, app.dtcPast ? "ย้อนหลัง" : "ปัจจุบัน", C_ACCENT);

  if (app.dtcCount == -2) {
    text("กด อ่าน เพื่ออ่านรหัสเสีย", 12, 70, TL_DATUM, C_TEXT, C_BG, F_TH);
    text("(เปิดสวิตช์กุญแจก่อน)", 12, 94, TL_DATUM, C_DIM, C_BG, F_TH);
  } else if (app.dtcCount == -1) {
    text("อ่านไม่สำเร็จ", 12, 70, TL_DATUM, C_BAD, C_BG, F_TH);
  } else if (app.dtcCount == 0) {
    text("ไม่มีรหัสเสีย", 12, 70, TL_DATUM, C_OK, C_BG, F_THB);
  } else {
    for (int i = 0; i < app.dtcCount && i < 5; i++) {
      int y = 60 + i * 27;
      tft.fillRect(3, y, 314, 25, C_PANEL);
      char sc[8];
      const char *d = dtcDescribe(app.dtc[i], sc);
      tft.fillRoundRect(6, y + 3, 44, 19, 9, C_BAD);
      text(sc, 28, y + 13, MC_DATUM, C_TEXT, C_BAD, F_S);
      text(d, 58, y + 3, TL_DATUM, C_TEXT, C_PANEL, F_TH);
    }
    if (app.dtcCount > 5) {
      snprintf(b, sizeof(b), "+%d MORE", app.dtcCount - 5);
      text(b, 160, 199, MC_DATUM, C_DIM, C_BG, F_S);
    }
  }
  if (app.dtcMsg[0]) text(app.dtcMsg, 12, 186, TL_DATUM, C_WARN, C_BG, F_TH);
  backButton();
  button(112, 208, 100, 30, "อ่าน", C_WARN);
  bool armed = app.clearArmedMs && nowMs() - app.clearArmedMs < 4000;
  button(216, 208, 100, 30, armed ? "ยืนยัน?" : "ล้าง", C_BAD, armed);
}

// ================================================================ STATS
static void drawStats() {
  tft.fillScreen(C_BG);
  header("สถิติ / PEAK", C_WARN);
  panel(3, 30, 314, 74, C_WARN);
  text("ค่าสูงสุด", 10, 34, TL_DATUM, C_WARN, C_PANEL, F_TH);
  const char *pk[] = {"RPM", "SPEED", "ECT MAX", "BATT"};
  for (int i = 0; i < 4; i++) text(pk[i], 12 + i * 78, 58, TL_DATUM, C_DIM, C_PANEL, F_S);
  panel(3, 108, 314, 94, C_ACCENT);
  text("เครื่องยนต์", 10, 112, TL_DATUM, C_ACCENT2, C_PANEL, F_TH);
  text("ชั่วโมงเครื่อง", 12, 136, TL_DATUM, C_DIM, C_PANEL, F_TH);
  text("จำนวนสตาร์ต", 12, 158, TL_DATUM, C_DIM, C_PANEL, F_TH);
  text("น้ำมันเครื่อง (ชม.)", 12, 180, TL_DATUM, C_DIM, C_PANEL, F_TH);
  backButton();
  bool a1 = app.resetArmedWhat == 1 && nowMs() - app.resetArmedMs < 4000;
  bool a2 = app.resetArmedWhat == 2 && nowMs() - app.resetArmedMs < 4000;
  button(112, 208, 100, 30, a1 ? "ยืนยัน?" : "ล้าง PEAK", C_WARN, a1);
  button(216, 208, 100, 30, a2 ? "ยืนยัน?" : "ล้างน้ำมัน", C_ACCENT, a2);
}

static void updateStats() {
  drawStatus();
  const Stats &s = app.stats;
  char b[24];
  snprintf(b, sizeof(b), "%u", s.peakRpm);
  text(b, 12, 76, TL_DATUM, C_TEXT, C_PANEL, F_MD, 74);
  snprintf(b, sizeof(b), "%u", s.peakSpeed);
  text(b, 90, 76, TL_DATUM, C_WARN, C_PANEL, F_MD, 74);
  if (s.maxEct > -40) snprintf(b, sizeof(b), "%d", s.maxEct); else strcpy(b, "--");
  text(b, 168, 76, TL_DATUM, C_WARN, C_PANEL, F_MD, 74);
  if (s.maxBatt > 0) snprintf(b, sizeof(b), "%.1f-%.1f", s.minBatt, s.maxBatt); else strcpy(b, "--");
  text(b, 246, 80, TL_DATUM, C_TEXT, C_PANEL, F_S, 68);
  snprintf(b, sizeof(b), "%.1f", s.engineSec / 3600.0f);
  text(b, 308, 136, TR_DATUM, C_TEXT, C_PANEL, F_TH, 90);
  snprintf(b, sizeof(b), "%lu", (unsigned long)s.starts);
  text(b, 308, 158, TR_DATUM, C_TEXT, C_PANEL, F_TH, 90);
  snprintf(b, sizeof(b), "%.1f", s.oilSec / 3600.0f);
  text(b, 308, 180, TR_DATUM, C_WARN, C_PANEL, F_TH, 90);
}

// ================================================================ GRAPH
static const int GR_X = 30, GR_Y = 52, GR_W = 284, GR_H = 146;

static void drawGraphFrame() {
  tft.fillScreen(C_BG);
  header("กราฟ", C_PURPLE);
  tft.fillRect(6, 30, 10, 3, C_ACCENT);
  text("รอบ", 20, 28, TL_DATUM, C_ACCENT, C_BG, F_TH);
  tft.fillRect(150, 30, 10, 3, C_WARN);
  text("เครื่อง °C", 164, 28, TL_DATUM, C_WARN, C_BG, F_TH);
  backButton();
  text("30 วินาทีล่าสุด", 120, 223, ML_DATUM, C_DIM, C_BG, F_TH);
}

static void updateGraph() {
  static uint32_t last = 0;
  if (nowMs() - last < 700 && !app.needFull) return;
  last = nowMs();
  drawStatus();
  char b[16];
  snprintf(b, sizeof(b), "%u", app.ecu.valid ? app.ecu.rpm : 0);
  text(b, 104, 28, TR_DATUM, C_TEXT, C_BG, F_TH, 50);
  snprintf(b, sizeof(b), "%d", app.ecu.valid ? app.ecu.ectC : 0);
  text(b, 290, 28, TR_DATUM, C_TEXT, C_BG, F_TH, 40);
  tft.fillRect(GR_X, GR_Y, GR_W + 1, GR_H + 1, C_PANEL);
  for (int g = 0; g <= 4; g++) {
    int y = GR_Y + g * GR_H / 4;
    tft.drawFastHLine(GR_X, y, GR_W, C_PANEL2);
    char l[12]; snprintf(l, sizeof(l), "%d", (int)(app.set.maxRpm / 1000 * (4 - g) / 4));
    text(l, GR_X - 4, y, MR_DATUM, C_DIM, C_BG, F_S, 20);
  }
  int n = app.histLen;
  for (int i = 1; i < n; i++) {
    int x0 = GR_X + (i - 1) * GR_W / (HIST_LEN - 1), x1 = GR_X + i * GR_W / (HIST_LEN - 1);
    int r0 = GR_Y + GR_H - (int)app.histRpm[i - 1] * GR_H / app.set.maxRpm;
    int r1 = GR_Y + GR_H - (int)app.histRpm[i] * GR_H / app.set.maxRpm;
    int e0 = GR_Y + GR_H - (app.histEct[i - 1] + 20) * GR_H / 150;
    int e1 = GR_Y + GR_H - (app.histEct[i] + 20) * GR_H / 150;
    r0 = r0 < GR_Y ? GR_Y : r0; r1 = r1 < GR_Y ? GR_Y : r1;
    tft.drawLine(x0, e0, x1, e1, C_WARN);
    tft.drawLine(x0, r0, x1, r1, C_ACCENT);
    tft.drawLine(x0, r0 - 1, x1, r1 - 1, C_ACCENT);
  }
}

// ================================================================ RAW
static void drawRaw() {
  tft.fillScreen(C_BG);
  header("K-LINE ข้อมูลดิบ", C_ACCENT2);
  char t[24]; snprintf(t, sizeof(t), "TABLE 0x%02X", app.rawTable);
  button(4, 30, 152, 30, t, C_ACCENT);
  button(164, 30, 152, 30, "อ่าน", C_OK);
  text(app.rawMsg, 6, 66, TL_DATUM, C_DIM, C_BG, F_TH);
  tft.fillRect(3, 88, 314, 116, C_PANEL);
  char line[64];
  for (int row = 0; row * 8 < app.rawLen && row < 7; row++) {
    int p = snprintf(line, sizeof(line), "%02X:", row * 8);
    for (int i = 0; i < 8 && row * 8 + i < app.rawLen; i++)
      p += snprintf(line + p, sizeof(line) - p, " %02X", app.rawFrame[row * 8 + i]);
    text(line, 10, 92 + row * 16, TL_DATUM, C_TEXT, C_PANEL, F_GLCD);
  }
  backButton();
}

// ================================================================ SETTINGS
static void setRow(int x, int y, int w, const char *label, const char *val, uint16_t vc, uint16_t bar) {
  tft.fillRect(x, y, w, 32, C_PANEL);
  tft.drawFastHLine(x, y + 31, w, C_PANEL2);
  tft.fillRect(x, y + 4, 3, 24, bar);
  text(label, x + 9, y + 7, TL_DATUM, C_TEXT, C_PANEL, F_TH);
  if (val) text(val, x + w - 8, y + 7, TR_DATUM, vc, C_PANEL, F_TH);
}

static const char *onOff(bool v) { return v ? "เปิด" : "ปิด"; }
static const char *setTabs[3] = {"แสดงผล", "ECU", "WiFi"};

static void drawSet() {
  tft.fillScreen(C_BG);
  header("ตั้งค่า", C_ACCENT);
  for (int i = 0; i < 3; i++) {
    bool on = app.setTab == i;
    int x = 4 + i * 105;
    tft.fillRoundRect(x, 29, 100, 26, 5, on ? C_ACCENT : C_PANEL2);
    tft.drawRoundRect(x, 29, 100, 26, 5, on ? C_ACCENT2 : C_LINE);
    text(setTabs[i], x + 50, 42, MC_DATUM, on ? C_BG : C_TEXT, on ? C_ACCENT : C_PANEL2, F_TH);
  }
  char b[40];
  if (app.setTab == 0) {
    setRow(4, 60, 312, "ความสว่าง", nullptr, 0, C_WARN);
    button(132, 63, 30, 26, "-", C_ACCENT);
    tft.fillRoundRect(168, 72, 66, 8, 4, C_PANEL2);
    tft.fillRoundRect(168, 72, 66 * app.set.bright / 100, 8, 4, C_TEAL);
    button(240, 63, 30, 26, "+", C_ACCENT);
    snprintf(b, sizeof(b), "%d", app.set.bright);
    text(b, 314, 76, MR_DATUM, C_DIM, C_PANEL, F_S);
    setRow(4, 94, 154, "โหมด", app.set.night ? "กลางคืน" : "กลางวัน", C_WARN, C_ACCENT);
    snprintf(b, sizeof(b), "%u", app.set.maxRpm);
    setRow(162, 94, 154, "รอบสูงสุด", b, C_WARN, C_ACCENT);
    setRow(4, 128, 154, "กลับสีจอ", onOff(app.set.invColor), C_WARN, C_ACCENT);
    setRow(162, 128, 154, "กลับทิศทัช", onOff(app.set.flipTouch), C_WARN, C_ACCENT);
    setRow(4, 162, 312, "ทดสอบสีจอ", ">", C_ACCENT2, C_PURPLE);
  } else if (app.setTab == 1) {
    char m[64];
    if (app.set.model < BIKE_COUNT) snprintf(m, sizeof(m), "%s  >", bikeModels[app.set.model].name);
    else strcpy(m, "ยังไม่เลือก  >");
    setRow(4, 60, 312, "รุ่นรถ", m, C_TEAL, C_TEAL);
    setRow(4, 94, 312, "โหมดสาธิต", onOff(app.set.demo), app.set.demo ? C_WARN : C_DIM, C_WARN);
    setRow(4, 128, 312, "TX กลับขั้ว", onOff(app.set.invTx), C_WARN, C_ACCENT);
    text("TX เปิด = ทรานซิสเตอร์  ปิด = L9637D", 10, 164, TL_DATUM, C_DIM, C_BG, F_TH);
    snprintf(b, sizeof(b), "เฟิร์มแวร์ %s", FW_VERSION);
    text(b, 312, 222, MR_DATUM, C_LINE, C_BG, F_TH);
  } else {
    setRow(4, 60, 312, "อัปเดต / เปลี่ยนโลโก้ ผ่าน WiFi", ">", C_ACCENT2, C_OK);
    setRow(4, 94, 312, "โชว์โลโก้ตอนเปิดเครื่อง", onOff(app.set.showLogo), C_WARN, C_ACCENT);
    setRow(4, 128, 312, "ดูโลโก้", app.hasLogo ? ">" : "ยังไม่มี", C_ACCENT2, C_PURPLE);
    text("WiFi: HondaGauge  รหัส 12345678", 10, 166, TL_DATUM, C_DIM, C_BG, F_TH);
    text("แล้วเปิดเว็บ 192.168.4.1", 10, 186, TL_DATUM, C_DIM, C_BG, F_TH);
  }
  backButton();
}

void uiColorTest() {
  const uint16_t c[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char *n[] = {"แดง", "เขียว", "น้ำเงิน", "ขาว"};
  for (int i = 0; i < 4; i++) {
    tft.fillRect(i * 80, 0, 80, 240, c[i]);
    text(n[i], i * 80 + 40, 120, MC_DATUM, TFT_BLACK, c[i], F_TH);
  }
}

// ================================================================ MODEL PICKER
static const int MODEL_ROWS = 4;

static int groupItems(int g, int *idx) {     // indexes of the models in group g
  int n = 0;
  for (int i = 0; i < BIKE_COUNT; i++) if (bikeModels[i].group == g) idx[n++] = i;
  return n;
}

static void drawModel() {
  tft.fillScreen(C_BG);
  header("เลือกรุ่นรถ", C_TEAL);
  for (int i = 0; i < GRP_COUNT; i++) {
    bool on = app.modelGroup == i;
    int x = 4 + i * 105;
    tft.fillRoundRect(x, 29, 100, 26, 5, on ? C_TEAL : C_PANEL2);
    tft.drawRoundRect(x, 29, 100, 26, 5, on ? C_ACCENT2 : C_LINE);
    text(groupNames[i], x + 50, 42, MC_DATUM, on ? C_BG : C_TEXT, on ? C_TEAL : C_PANEL2, F_TH);
  }
  int idx[BIKE_COUNT];
  int n = groupItems(app.modelGroup, idx);
  int pages = (n + MODEL_ROWS - 1) / MODEL_ROWS;
  if (app.modelPage >= pages) app.modelPage = pages - 1;
  for (int r = 0; r < MODEL_ROWS; r++) {
    int k = app.modelPage * MODEL_ROWS + r;
    if (k >= n) break;
    const BikeModel &m = bikeModels[idx[k]];
    bool sel = app.set.model == idx[k];
    int y = 58 + r * 37;
    tft.fillRoundRect(4, y, 312, 35, 4, sel ? C_PANEL2 : C_PANEL);
    if (sel) tft.drawRoundRect(4, y, 312, 35, 4, C_TEAL);
    tft.fillRect(4, y + 5, 3, 25, sel ? C_TEAL : C_LINE);
    text(m.name, 12, y + 1, TL_DATUM, sel ? C_TEAL : C_TEXT, sel ? C_PANEL2 : C_PANEL, F_TH);
    text(m.variant, 12, y + 18, TL_DATUM, C_DIM, sel ? C_PANEL2 : C_PANEL, F_TH);
    int pw = textWidth(m.code, F_S) + 14;
    tft.fillRoundRect(310 - pw, y + 8, pw, 19, 9, sel ? C_TEAL : C_LINE);
    text(m.code, 310 - pw / 2, y + 18, MC_DATUM, sel ? C_BG : C_TEXT, sel ? C_TEAL : C_LINE, F_S);
  }
  backButton();
  button(112, 208, 70, 30, "ขึ้น", C_ACCENT);
  button(186, 208, 70, 30, "ลง", C_ACCENT);
  char p[24]; snprintf(p, sizeof(p), "%d/%d", app.modelPage + 1, pages);
  text(p, 288, 223, MC_DATUM, C_DIM, C_BG, F_S);
}

static void touchModel(int x, int y) {
  if (hit(x, y, 4, 208, 104, 30)) { app.screen = SCR_SET; app.needFull = true; return; }
  for (int i = 0; i < GRP_COUNT; i++)
    if (hit(x, y, 4 + i * 105, 29, 100, 26)) { app.modelGroup = i; app.modelPage = 0; }
  int idx[BIKE_COUNT];
  int n = groupItems(app.modelGroup, idx);
  int pages = (n + MODEL_ROWS - 1) / MODEL_ROWS;
  if (hit(x, y, 112, 208, 70, 30) && app.modelPage > 0) app.modelPage--;
  if (hit(x, y, 186, 208, 70, 30) && app.modelPage < pages - 1) app.modelPage++;
  for (int r = 0; r < MODEL_ROWS; r++) {
    int k = app.modelPage * MODEL_ROWS + r;
    if (k < n && hit(x, y, 4, 58 + r * 37, 312, 35)) {
      app.set.model = (app.set.model == idx[k]) ? 255 : idx[k];   // tap again to clear
      actSaveSettings();
    }
  }
  app.needFull = true;
}

// ================================================================ OTA
static void drawOta() {
  tft.fillScreen(C_BG);
  header("อัปเดต WiFi", C_OK);
  panel(3, 32, 314, 150, C_OK);
  text("1. มือถือต่อ WiFi", 12, 40, TL_DATUM, C_TEXT, C_PANEL, F_TH);
  text("HondaGauge", 30, 62, TL_DATUM, C_TEAL, C_PANEL, F_M);
  text("รหัสผ่าน 12345678", 30, 92, TL_DATUM, C_DIM, C_PANEL, F_TH);
  text("2. เปิดเว็บ", 12, 116, TL_DATUM, C_TEXT, C_PANEL, F_TH);
  text("192.168.4.1", 120, 114, TL_DATUM, C_WARN, C_PANEL, F_M);
  text("3. อัปเดตเฟิร์มแวร์ หรือเลือกรูปโลโก้", 12, 140, TL_DATUM, C_TEXT, C_PANEL, F_TH);
  text("   เสร็จแล้วกด ออก เพื่อเริ่มใหม่", 12, 160, TL_DATUM, C_DIM, C_PANEL, F_TH);
  button(90, 196, 140, 38, "ออก", C_BAD);
}

// ================================================================ dispatch
void uiDrawFull() {
  switch (app.screen) {
    case SCR_GAUGE: drawGaugeFull(); break;
    case SCR_MENU:  drawMenu(); break;
    case SCR_ALL:   drawAll(); break;
    case SCR_CHECK: drawCheck(); break;
    case SCR_DTC:   drawDtc(); break;
    case SCR_STATS: drawStats(); break;
    case SCR_GRAPH: drawGraphFrame(); break;
    case SCR_RAW:   drawRaw(); break;
    case SCR_SET:   drawSet(); break;
    case SCR_OTA:   drawOta(); return;
    case SCR_MODEL: drawModel(); break;
  }
  uiUpdate();
}

void uiUpdate() {
  switch (app.screen) {
    case SCR_GAUGE: drawStatus(); updateGauge(); break;
    case SCR_ALL:   updateAll(); break;
    case SCR_CHECK: updateCheck(); break;
    case SCR_STATS: updateStats(); break;
    case SCR_GRAPH: updateGraph(); break;
    case SCR_MENU: case SCR_DTC: case SCR_RAW: case SCR_SET: case SCR_MODEL: drawStatus(); break;
    default: break;
  }
}

static void go(Screen s) { app.screen = s; app.needFull = true; }

void uiTouch(int x, int y) {
  app.touchX = x; app.touchY = y;
  bool back = hit(x, y, 4, 208, 104, 30);
  switch (app.screen) {
    case SCR_GAUGE: go(SCR_MENU); break;
    case SCR_MENU:
      if (y >= 204) { go(SCR_GAUGE); break; }
      for (int i = 0; i < 8; i++) {
        int tx, ty; menuTile(i, tx, ty);
        if (hit(x, y, tx, ty, 75, 80)) { go(menu[i].to); break; }
      }
      break;
    case SCR_ALL: case SCR_CHECK: case SCR_GRAPH:
      if (back) go(SCR_MENU);
      break;
    case SCR_DTC:
      if (back) { go(SCR_MENU); break; }
      if (hit(x, y, 206, 28, 110, 26)) { app.dtcPast = !app.dtcPast; app.dtcCount = -2; app.dtcMsg[0] = 0; }
      else if (hit(x, y, 112, 208, 100, 30)) {
        strcpy(app.dtcMsg, "กำลังอ่าน..."); drawDtc(); actReadDtc();
      } else if (hit(x, y, 216, 208, 100, 30)) {
        if (app.clearArmedMs && nowMs() - app.clearArmedMs < 4000) { app.clearArmedMs = 0; actClearDtc(); }
        else { app.clearArmedMs = nowMs(); strcpy(app.dtcMsg, "กด ยืนยัน? อีกครั้งเพื่อล้างรหัส"); }
      }
      app.needFull = true;
      break;
    case SCR_STATS:
      if (back) { go(SCR_MENU); break; }
      for (int w = 1; w <= 2; w++) {
        if (!hit(x, y, w == 1 ? 112 : 216, 208, 100, 30)) continue;
        if (app.resetArmedWhat == w && nowMs() - app.resetArmedMs < 4000) {
          if (w == 1) { app.stats.peakRpm = 0; app.stats.peakSpeed = 0; app.stats.maxEct = -40;
                        app.stats.minBatt = 99; app.stats.maxBatt = 0; }
          else app.stats.oilSec = 0;
          app.resetArmedWhat = 0;
          actSaveStats();
        } else { app.resetArmedWhat = w; app.resetArmedMs = nowMs(); }
      }
      app.needFull = true;
      break;
    case SCR_RAW:
      if (back) { go(SCR_MENU); break; }
      if (hit(x, y, 4, 30, 152, 30)) {
        const uint8_t tables[] = {0x10, 0x11, 0x20, 0x21, 0x60, 0x61, 0xD0, 0xD1};
        int i = 0; while (i < 8 && tables[i] != app.rawTable) i++;
        app.rawTable = tables[(i + 1) % 8];
      } else if (hit(x, y, 164, 30, 152, 30)) {
        strcpy(app.rawMsg, "กำลังอ่าน..."); drawRaw(); actReadRaw();
      }
      app.needFull = true;
      break;
    case SCR_SET:
      if (back) { go(SCR_MENU); break; }
      for (int i = 0; i < 3; i++) if (hit(x, y, 4 + i * 105, 29, 100, 26)) app.setTab = i;
      if (app.setTab == 0) {
        if (hit(x, y, 126, 60, 42, 32)) { app.set.bright = app.set.bright > 20 ? app.set.bright - 10 : 10; actApplyBrightness(); }
        else if (hit(x, y, 236, 60, 44, 32)) { app.set.bright = app.set.bright < 100 ? app.set.bright + 10 : 100; actApplyBrightness(); }
        else if (hit(x, y, 4, 94, 154, 32)) { app.set.night = !app.set.night; app.set.bright = app.set.night ? 30 : 100; actApplyBrightness(); }
        else if (hit(x, y, 162, 94, 154, 32)) { app.set.maxRpm = app.set.maxRpm >= 14000 ? 8000 : app.set.maxRpm + 2000; }
        else if (hit(x, y, 4, 128, 154, 32)) { app.set.invColor = !app.set.invColor; actApplyInvert(); }
        else if (hit(x, y, 162, 128, 154, 32)) app.set.flipTouch = !app.set.flipTouch;
        else if (hit(x, y, 4, 162, 312, 32)) { uiColorTest(); app.holdUntil = nowMs() + 3000; }
      } else if (app.setTab == 1) {
        if (hit(x, y, 4, 60, 312, 32)) {
          if (app.set.model < BIKE_COUNT) {          // open the list on the chosen bike's page
            app.modelGroup = bikeModels[app.set.model].group;
            int n = 0; for (int i = 0; i < app.set.model; i++) if (bikeModels[i].group == app.modelGroup) n++;
            app.modelPage = n / 4;
          }
          go(SCR_MODEL); return;
        }
        if (hit(x, y, 4, 94, 312, 32)) app.set.demo = !app.set.demo;
        else if (hit(x, y, 4, 128, 312, 32)) { app.set.invTx = !app.set.invTx; actApplyInvTx(); }
      } else {
        if (hit(x, y, 4, 60, 312, 32)) { actSaveSettings(); go(SCR_OTA); actStartOta(); return; }
        if (hit(x, y, 4, 94, 312, 32)) app.set.showLogo = !app.set.showLogo;
        else if (hit(x, y, 4, 128, 312, 32) && app.hasLogo) { actShowLogo(); app.holdUntil = nowMs() + 4000; }
      }
      actSaveSettings();
      app.needFull = true;
      break;
    case SCR_MODEL: touchModel(x, y); break;
    case SCR_OTA:
      break;   // handled in main.cpp (restart)
  }
}
