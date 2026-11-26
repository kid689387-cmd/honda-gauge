// Theme colours, fonts and small widgets shared by every screen.
#pragma once
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// blue-grey theme
constexpr uint16_t C_BG      = RGB(11, 17, 25);
constexpr uint16_t C_PANEL   = RGB(24, 35, 49);
constexpr uint16_t C_PANEL2  = RGB(34, 50, 70);
constexpr uint16_t C_LINE    = RGB(66, 92, 122);
constexpr uint16_t C_ACCENT  = RGB(92, 146, 196);
constexpr uint16_t C_ACCENT2 = RGB(150, 188, 222);
constexpr uint16_t C_TEXT    = RGB(232, 239, 246);
constexpr uint16_t C_DIM     = RGB(136, 158, 182);
constexpr uint16_t C_OK      = RGB(60, 200, 120);
constexpr uint16_t C_WARN    = RGB(242, 180, 60);
constexpr uint16_t C_BAD     = RGB(232, 72, 78);
constexpr uint16_t C_PURPLE  = RGB(168, 120, 232);
constexpr uint16_t C_TEAL    = RGB(64, 196, 200);

enum Font { F_TH, F_THB, F_XL, F_MD, F_M, F_S, F_GLCD };   // F_XL/F_MD = digits only

// Draw text. Thai fonts get mark positioning automatically.
// pad > 0 clears that many pixels of width behind the text (for live values).
int  text(const char *s, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, Font f, int pad = 0);
int  textWidth(const char *s, Font f);

void header(const char *title, uint16_t col);
void drawStatus();
void panel(int x, int y, int w, int h, uint16_t accent);
void button(int x, int y, int w, int h, const char *label, uint16_t col, bool filled = false);
void backButton();
bool hit(int x, int y, int rx, int ry, int rw, int rh);

enum Icon { IC_GAUGE, IC_LIST, IC_CHECK, IC_WARN, IC_PEAK, IC_GRAPH, IC_WAVE, IC_GEAR };
void icon(Icon ic, int cx, int cy, uint16_t col);
