// ECU TOOL: เครื่องมืออ่าน / สำรองข้อมูล ECU (เป้าหมายแรก Wave 110i K1M)
// หน้าจอธีม "Racing Neon" (ดำ + เขียวมะนาว + กรอบตัดมุม) ใช้ฟังก์ชันวาดของ gfx.h
//
// ความปลอดภัย:
//  - ไม่มีโค้ดเขียน ECU (WRITE / FLASH) อยู่ในเฟิร์มแวร์นี้
//  - ปุ่มที่ยังไม่มีโปรโตคอลยืนยันจะแสดงว่า "ยังไม่เปิดใช้งาน" ไม่สร้างข้อมูลปลอม
//  - หน้าจอการทำงาน (SCR_ECU_OP) ทดสอบได้ด้วยปุ่ม UI TEST ซึ่งเขียนกำกับว่าเป็นการทดสอบ
#include "app.h"
#include "gfx.h"
#include "models.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- neon colours
static constexpr uint16_t N_LIME  = RGB(200, 255, 46);
static constexpr uint16_t N_GREEN = RGB(76, 255, 26);
static constexpr uint16_t N_GLOW  = RGB(28, 70, 14);    // dark halo next to the lines
static constexpr uint16_t N_OLIVE = RGB(79, 106, 16);
static constexpr uint16_t N_PANEL = RGB(9, 16, 12);
static constexpr uint16_t N_BAND  = RGB(12, 26, 14);    // diagonal background stripes
static constexpr uint16_t N_HEX   = RGB(24, 52, 20);
static constexpr uint16_t N_SEGOFF = RGB(22, 32, 15);
static constexpr uint16_t N_RED   = RGB(255, 59, 48);

static uint16_t mix565(uint16_t a, uint16_t b, int t) {   // t = 0..255 (255 = a)
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
  int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return ((rb + (ra - rb) * t / 255) << 11) | ((gb + (ga - gb) * t / 255) << 5) | (bb + (ba - bb) * t / 255);
}

// ---------------------------------------------------------------- drawing helpers
// Panel with the top-left and bottom-right corners cut, a 2 px neon border and a dark halo.
static void chamfer(int x, int y, int w, int h, int c, uint16_t col, uint16_t fill = N_PANEL) {
  int x2 = x + w - 1, y2 = y + h - 1;
  tft.fillRect(x, y, w, h, fill);
  tft.fillTriangle(x, y, x + c, y, x, y + c, C_BG);            // cut corners
  tft.fillTriangle(x2, y2, x2 - c, y2, x2, y2 - c, C_BG);
  uint16_t glow = (col == N_RED) ? RGB(70, 16, 14) : (col == C_WARN ? RGB(70, 50, 10) : N_GLOW);
  for (int k = 0; k < 3; k++) {
    uint16_t lc = k == 0 ? glow : col;      // k=0: halo just outside, k=1,2: the 2 px line
    int o = 1 - k;                          // offset: +1 outside, 0, -1 inside
    int ax = x - o, ay = y - o, bx = x2 + o, by = y2 + o;
    tft.drawLine(ax, ay + c, ax + c, ay, lc);          // TL diagonal
    tft.drawLine(ax + c, ay, bx, ay, lc);              // top
    tft.drawLine(bx, ay, bx, by - c, lc);              // right
    tft.drawLine(bx, by - c, bx - c, by, lc);          // BR diagonal
    tft.drawLine(bx - c, by, ax, by, lc);              // bottom
    tft.drawLine(ax, by, ax, ay + c, lc);              // left
  }
}

static void neonButton(int x, int y, int w, int h, const char *label, uint16_t col, Font f = F_TH) {
  chamfer(x, y, w, h, 7, col);
  text(label, x + w / 2, y + h / 2, MC_DATUM, C_TEXT, N_PANEL, f);
}

static void hexagon(int cx, int cy, int r, uint16_t col) {
  int dx = r * 866 / 1000;
  int px[6] = {cx, cx + dx, cx + dx, cx, cx - dx, cx - dx};
  int py[6] = {cy - r, cy - r / 2, cy + r / 2, cy + r, cy + r / 2, cy - r / 2};
  for (int i = 0; i < 6; i++) tft.drawLine(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], col);
}

static void neonBackground() {
  tft.fillScreen(C_BG);
  // two diagonal "brush" stripes rising to the right
  tft.fillTriangle(0, 150, 320, 40, 320, 80, N_BAND);
  tft.fillTriangle(0, 150, 320, 80, 0, 190, N_BAND);
  tft.fillTriangle(0, 200, 320, 110, 320, 122, N_BAND);
  tft.fillTriangle(0, 200, 320, 122, 0, 212, N_BAND);
  // honeycomb in the bottom-right corner
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 4; col++)
      if (row + col < 5) hexagon(312 - col * 22 - (row & 1) * 11, 228 - row * 18, 12, N_HEX);
}

static void checker(int x, int y, int cols, int rows, int cell) {
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      tft.fillRect(x + c * cell, y + r * cell, cell, cell, ((r + c) & 1) ? RGB(48, 53, 47) : RGB(18, 22, 18));
}

static void neonHeader(const char *title) {
  // title tab on the left, live status (DEMO / ECU, battery) on the right from gfx.cpp
  tft.fillRect(0, 0, 320, 26, C_BG);
  chamfer(2, 2, 178, 21, 6, N_GREEN);
  checker(10, 7, 3, 2, 5);
  checker(155, 7, 3, 2, 5);
  text(title, 91, 13, MC_DATUM, N_LIME, N_PANEL, F_S);
  drawStatus();
}

// segmented progress bar, slanted pieces, colour ramps from olive to `on`
static void segBar(int x, int y, int w, int h, int pct, uint16_t on) {
  const int N = 24, slant = 5, gap = 3;
  int seg = (w - slant) / N;
  int lit = (pct * N + 50) / 100;
  tft.fillRect(x, y, w, h, N_PANEL);
  for (int i = 0; i < N; i++) {
    int sx = x + i * seg;
    uint16_t c = i < lit ? mix565(on, N_OLIVE, 90 + i * 165 / N) : N_SEGOFF;
    tft.fillTriangle(sx + slant, y, sx + seg - gap + slant, y, sx, y + h - 1, c);
    tft.fillTriangle(sx + seg - gap + slant, y, sx + seg - gap, y + h - 1, sx, y + h - 1, c);
  }
}

// ring + ECU chip icon; `phase` turns the gap of the ring while working
static void chipIcon(int cx, int cy, uint16_t col, int phase) {
  tft.fillCircle(cx, cy, 24, C_BG);
  int a0 = (60 + phase) % 360, a1 = (340 + phase) % 360;
  if (a1 > a0) tft.drawArc(cx, cy, 23, 19, a0, a1, col, C_BG, true);
  else { tft.drawArc(cx, cy, 23, 19, a0, 360, col, C_BG, true); tft.drawArc(cx, cy, 23, 19, 0, a1, col, C_BG, true); }
  tft.fillRoundRect(cx - 6, cy - 10, 13, 21, 2, col);
  for (int i = 0; i < 4; i++) {
    int py = cy - 7 + i * 5;
    tft.fillRect(cx - 10, py, 3, 2, col);
    tft.fillRect(cx + 8, py, 3, 2, col);
  }
}

static void infoRow(int y, const char *key, const char *val, uint16_t vc) {
  chamfer(4, y, 312, 26, 5, RGB(44, 74, 24));
  text(key, 14, y + 2, TL_DATUM, C_DIM, N_PANEL, F_TH);
  text(val, 306, y + 2, TR_DATUM, vc, N_PANEL, F_TH);
}

// ================================================================ ECU TOOL home
struct EcuTile { const char *en; const char *th; };
static const EcuTile tiles[8] = {
  {"LINK", "เชื่อมต่อ"}, {"INFO", "ข้อมูล"}, {"READ", "อ่าน ECU"}, {"BACKUP", "สำรอง"},
  {"FILES", "ไฟล์"}, {"WIFI", "ส่งไฟล์"}, {"BT", "ส่งไฟล์"}, {"SETUP", "ตั้งค่า"},
};
static void tileXY(int i, int &x, int &y) { x = 4 + (i % 4) * 79; y = 56 + (i / 4) * 74; }

// Thai tile label. If it is too wide for the tile, the English word after the last
// space ("อ่าน ECU") is drawn in the smaller F_S font so the whole label fits.
static void tileLabel(const char *s, int cx, int cy) {
  const int maxW = 66;
  if (textWidth(s, F_TH) <= maxW) { text(s, cx, cy, MC_DATUM, C_TEXT, N_PANEL, F_TH); return; }
  const char *sp = strrchr(s, ' ');
  if (!sp) { text(s, cx, cy, MC_DATUM, C_TEXT, N_PANEL, F_TH); return; }
  char th[48];
  size_t n = (size_t)(sp - s) < sizeof(th) - 1 ? (size_t)(sp - s) : sizeof(th) - 1;
  memcpy(th, s, n); th[n] = 0;
  const char *en = sp + 1;
  int wTh = textWidth(th, F_TH), wEn = textWidth(en, F_S), gap = 4;
  int x0 = cx - (wTh + gap + wEn) / 2;
  text(th, x0, cy, ML_DATUM, C_TEXT, N_PANEL, F_TH);
  text(en, x0 + wTh + gap, cy - 1, ML_DATUM, C_TEXT, N_PANEL, F_S);
}

static bool modelIsTarget() {
  return app.set.model < BIKE_COUNT && strstr(bikeModels[app.set.model].code, "K1M");
}

static void drawEcuHome() {
  neonBackground();
  neonHeader("ECU TOOL");
  char m[64];
  if (app.set.model < BIKE_COUNT) snprintf(m, sizeof(m), "%s  %s", bikeModels[app.set.model].name, bikeModels[app.set.model].code);
  else strcpy(m, "ยังไม่เลือกรุ่นรถ");
  text(m, 6, 28, TL_DATUM, modelIsTarget() ? N_LIME : C_DIM, C_BG, F_TH);
  tft.fillRoundRect(214, 30, 102, 20, 4, N_RED);
  text("WRITE LOCK", 265, 40, MC_DATUM, C_TEXT, N_RED, F_S);
  for (int i = 0; i < 8; i++) {
    int x, y; tileXY(i, x, y);
    chamfer(x, y, 75, 68, 8, i == 7 ? C_DIM : N_GREEN);
    text(tiles[i].en, x + 38, y + 22, MC_DATUM, i == 7 ? C_DIM : N_LIME, N_PANEL, F_S);
    tileLabel(tiles[i].th, x + 38, y + 46);
  }
  neonButton(4, 208, 104, 30, "< เมนู", N_LIME);
  neonButton(212, 208, 104, 30, "UI TEST", C_DIM, F_S);
}

// ================================================================ ECU INFO / CONNECT
static void drawEcuInfo() {
  neonBackground();
  neonHeader("ECU INFO");
  const char *ls; uint16_t lc;
  switch (app.link) {
    case LINK_DEMO:   ls = "โหมดสาธิต (ไม่ได้ต่อ ECU)"; lc = C_WARN; break;
    case LINK_OK:     ls = "เชื่อมต่อแล้ว"; lc = N_LIME; break;
    case LINK_WAKING: ls = "กำลังปลุก ECU..."; lc = C_ACCENT2; break;
    default:          ls = "ไม่พบ ECU"; lc = N_RED; break;
  }
  infoRow(30, "สถานะ", ls, lc);
  infoRow(58, "รหัสกล่อง", app.ecmId[0] ? app.ecmId : "ยังไม่ได้อ่าน", app.ecmId[0] ? N_LIME : C_DIM);
  char m[64];
  if (app.set.model < BIKE_COUNT) snprintf(m, sizeof(m), "%s (%s)", bikeModels[app.set.model].name, bikeModels[app.set.model].code);
  else strcpy(m, "ยังไม่เลือก");
  infoRow(86, "รุ่นรถ", m, app.set.model < BIKE_COUNT ? C_TEXT : C_DIM);
  infoRow(114, "เป้าหมาย", modelIsTarget() ? "ตรงกับ K1M" : "ยังไม่รองรับรุ่นนี้", modelIsTarget() ? N_LIME : C_WARN);
  char v[24];
  if (app.ecu.valid && app.link == LINK_OK) snprintf(v, sizeof(v), "%.1f V", app.ecu.battV);
  else strcpy(v, "--");
  infoRow(142, "แรงดันไฟ", v, C_TEXT);
  if (app.ecuMsg[0]) text(app.ecuMsg, 6, 174, TL_DATUM, C_WARN, C_BG, F_TH);
  else text("อ่านอย่างเดียว ไม่เขียนลง ECU", 6, 174, TL_DATUM, C_DIM, C_BG, F_TH);
  neonButton(4, 208, 104, 30, "< กลับ", N_LIME);
  neonButton(212, 208, 104, 30, "เชื่อมต่อ", N_GREEN);
}

// ================================================================ locked pages
struct LockedPage { const char *title; const char *what; const char *why1; const char *why2; };
static const LockedPage locked[5] = {
  {"READ ECU",  "อ่านข้อมูล ECU (อ่านอย่างเดียว)",
                "ยังไม่มีเอกสารยืนยัน", "คำสั่งอ่านหน่วยความจำของ K1M"},
  {"BACKUP",    "บันทึก .bin + ขนาด + CRC32",
                "ต้องอ่าน ECU ได้ก่อน", "และต้องเพิ่มการ์ด SD"},
  {"FILES",     "ดูไฟล์สำรองและตรวจ CRC32",
                "จะมาพร้อมระบบสำรองไฟล์", ""},
  {"WIFI",      "ส่งไฟล์สำรองไปมือถือ",
                "ตอนนี้ WiFi ใช้อัปเดต", "เฟิร์มแวร์และเปลี่ยนโลโก้"},
  {"BLUETOOTH", "ส่งไฟล์สำรองผ่านบลูทูธ",
                "จะเพิ่มหลังระบบสำรองไฟล์", ""},
};

static void drawEcuPage() {
  const LockedPage &p = locked[app.ecuPage];
  neonBackground();
  neonHeader(p.title);
  chamfer(4, 32, 312, 56, 8, N_GREEN);
  text("หน้าที่", 14, 36, TL_DATUM, C_DIM, N_PANEL, F_TH);
  text(p.what, 14, 60, TL_DATUM, C_TEXT, N_PANEL, F_TH);
  chamfer(4, 96, 312, 86, 8, C_WARN);
  text("ยังไม่เปิดใช้งาน", 14, 104, TL_DATUM, C_WARN, N_PANEL, F_THB);
  text(p.why1, 14, 132, TL_DATUM, C_TEXT, N_PANEL, F_TH);
  if (p.why2[0]) text(p.why2, 14, 154, TL_DATUM, C_TEXT, N_PANEL, F_TH);
  neonButton(4, 208, 104, 30, "< กลับ", N_LIME);
}

// ================================================================ operation screen
static const int OP_PCT_X = 304;

static void opColors(uint16_t &c) { c = app.opState == 2 ? N_RED : N_LIME; }

static void drawOpProgress() {
  uint16_t c; opColors(c);
  char b[8]; snprintf(b, sizeof(b), "%d%%", app.opPct);
  text(b, OP_PCT_X, 101, TR_DATUM, c, N_PANEL, F_TITLE, 110);
  segBar(18, 139, 286, 12, app.opPct, c);
}

static void drawOpTime() {
  uint32_t end = app.opState == 0 ? nowMs() : app.opT1;
  uint32_t s = (end - app.opT0) / 1000;
  char b[12]; snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  text(b, 204, 203, BR_DATUM, C_TEXT, N_PANEL, F_M, 70);
}

static void drawOp() {
  uint16_t c; opColors(c);
  neonBackground();
  neonHeader("ECU TOOL");
  chipIcon(30, 54, c, 0);
  text(app.opTitle, 62, 33, TL_DATUM, c, C_BG, F_TITLE);
  text(app.opSub, 62, 71, TL_DATUM, C_DIM, C_BG, F_TH);

  chamfer(6, 97, 308, 60, 10, c);
  text(app.opLabel, 18, 100, TL_DATUM, c, N_PANEL, F_THB);
  drawOpProgress();

  const char *name[3] = {"ขนาดไฟล์", "เวลาที่ใช้", "แรงดันไฟ"};
  for (int i = 0; i < 3; i++) {
    int x = 6 + i * 104;
    chamfer(x, 161, 100, 46, 7, N_GREEN);
    text(name[i], x + 8, 162, TL_DATUM, C_TEXT, N_PANEL, F_TH);
  }
  text(app.opSize, 100, 203, BR_DATUM, C_TEXT, N_PANEL, F_M);
  drawOpTime();
  char v[12];
  if (app.ecu.valid && app.link == LINK_OK) snprintf(v, sizeof(v), "%.1fV", app.ecu.battV);
  else strcpy(v, "--");
  text(v, 308, 203, BR_DATUM, C_TEXT, N_PANEL, F_M);

  bool busy = app.opState == 0;
  neonButton(110, 211, 100, 28, busy ? "รอ..." : "กลับ", busy ? C_DIM : N_LIME);
}

static void opStartTest(bool toFail) {
  app.opTest = toFail ? 2 : 1;
  app.opState = 0;
  app.opPct = 0;
  app.opT0 = nowMs();
  strcpy(app.opTitle, "UI TEST");
  strcpy(app.opLabel, "กำลังทดสอบ");
  strcpy(app.opSub, "ทดสอบหน้าจอ ไม่ได้ต่อ ECU");
  strcpy(app.opSize, "--");
}

static void opFinish(bool ok) {
  app.opState = ok ? 1 : 2;
  app.opT1 = nowMs();
  strcpy(app.opTitle, ok ? "SUCCESS" : "FAILED");
  strcpy(app.opLabel, ok ? "ทดสอบสำเร็จ" : "ทดสอบล้มเหลว");
  strcpy(app.opSub, ok ? "ตัวอย่าง ไม่มีข้อมูลจาก ECU" : "ตัวอย่างกรณีผิดพลาด");
  app.opTest = 0;
  app.needFull = true;
}

static void updateOp() {
  drawStatus();
  if (app.opState != 0) return;
  if (app.opTest) {
    int pct = (int)((nowMs() - app.opT0) / 30);        // about 3 s to 100 %
    int stop = app.opTest == 2 ? 64 : 100;
    if (pct >= stop) { app.opPct = stop; opFinish(app.opTest == 1); return; }
    app.opPct = pct;
  }
  static int phase = 0;
  phase = (phase + 30) % 360;
  chipIcon(30, 54, N_LIME, phase);
  drawOpProgress();
  drawOpTime();
}

// ================================================================ dispatch
void ecuDrawFull() {
  switch (app.screen) {
    case SCR_ECU:      drawEcuHome(); break;
    case SCR_ECU_INFO: drawEcuInfo(); break;
    case SCR_ECU_PAGE: drawEcuPage(); break;
    case SCR_ECU_OP:   drawOp(); break;
    default: break;
  }
}

void ecuUpdate() {
  if (app.screen == SCR_ECU_OP) updateOp();
  else drawStatus();
}

static void go(Screen s) { app.screen = s; app.needFull = true; }

void ecuTouch(int x, int y) {
  bool back = hit(x, y, 4, 208, 104, 30);
  switch (app.screen) {
    case SCR_ECU:
      if (back) { go(SCR_MENU); return; }
      if (hit(x, y, 212, 208, 104, 30)) {          // alternate success / failure demo
        static bool fail = false;
        opStartTest(fail); fail = !fail;
        go(SCR_ECU_OP); return;
      }
      for (int i = 0; i < 8; i++) {
        int tx, ty; tileXY(i, tx, ty);
        if (!hit(x, y, tx, ty, 75, 68)) continue;
        app.ecuMsg[0] = 0;
        if (i <= 1) go(SCR_ECU_INFO);
        else if (i == 7) { app.setTab = 1; go(SCR_SET); }
        else { app.ecuPage = i - 2; go(SCR_ECU_PAGE); }
        return;
      }
      break;
    case SCR_ECU_INFO:
      if (back) { go(SCR_ECU); return; }
      if (hit(x, y, 212, 208, 104, 30)) {
        strcpy(app.ecuMsg, "กำลังเชื่อมต่อ...");
        drawEcuInfo();
        actConnectEcu();
        app.needFull = true;
      }
      break;
    case SCR_ECU_PAGE:
      if (back) go(SCR_ECU);
      break;
    case SCR_ECU_OP:
      if (app.opState != 0 && hit(x, y, 100, 205, 120, 35)) go(SCR_ECU);
      break;
    default: break;
  }
}
