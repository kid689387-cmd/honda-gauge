#include "gfx.h"
#include "app.h"
#include "thai_shape.h"
#include "font_thai.h"
#include "font_thai_big.h"
#include "font_num_xl.h"
#include "font_num_m.h"
#include "font_num_md.h"
#include "font_small.h"
#include "font_title.h"
#include <stdio.h>
#include <math.h>
#include <string>

static const uint8_t *loaded = nullptr;

static void useFont(Font f) {
  const uint8_t *want = nullptr;
  switch (f) {
    case F_TH:  want = fontThai; break;
    case F_THB: want = fontThaiBig; break;
    case F_XL:  want = fontNumXL; break;
    case F_MD:  want = fontNumMD; break;
    case F_M:   want = fontNumM; break;
    case F_S:   want = fontSmall; break;
    case F_TITLE: want = fontTitle; break;
    default:    want = nullptr; break;
  }
  if (want == loaded) return;
  if (loaded) tft.unloadFont();
  if (want) tft.loadFont(want);
  loaded = want;
}

// Glyphs are drawn without per-glyph background boxes: those boxes cut off the
// overhang of italic letters and Thai marks. Live values (pad > 0) clear their
// whole box first instead.
int text(const char *s, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, Font f, int pad) {
  useFont(f);
  if (pad > 0) {
    int h = (f == F_GLCD) ? 16 : tft.fontHeight();
    int x0 = x, y0 = y;
    if (datum == TC_DATUM || datum == MC_DATUM || datum == BC_DATUM) x0 = x - pad / 2;
    else if (datum == TR_DATUM || datum == MR_DATUM || datum == BR_DATUM) x0 = x - pad;
    if (datum == ML_DATUM || datum == MC_DATUM || datum == MR_DATUM) y0 = y - h / 2;
    else if (datum == BL_DATUM || datum == BC_DATUM || datum == BR_DATUM) y0 = y - h;
    tft.fillRect(x0 - 3, y0, pad + 3, h, bg);   // italic glyphs lean a few pixels left at the bottom
  }
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg, false);
  tft.setTextPadding(0);
  if (f == F_TH || f == F_THB) {
    std::string shaped = thaiShape(std::string(s));
    return tft.drawString(shaped.c_str(), x, y);
  }
  if (f == F_GLCD) return tft.drawString(s, x, y, 2);
  return tft.drawString(s, x, y);
}

int textWidth(const char *s, Font f) {
  useFont(f);
  if (f == F_TH || f == F_THB) return tft.textWidth(thaiShape(std::string(s)).c_str());
  if (f == F_GLCD) return tft.textWidth(s, 2);
  return tft.textWidth(s);
}

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// ---------------------------------------------------------------- header
void header(const char *title, uint16_t col) {
  tft.fillRect(0, 0, 320, 26, C_BG);
  int w = textWidth(title, F_TH) + 18;
  tft.fillRect(0, 1, w, 22, C_PANEL2);
  tft.fillTriangle(w, 1, w + 12, 1, w, 22, C_PANEL2);
  tft.fillRect(0, 1, 4, 22, col);
  text(title, 10, 3, TL_DATUM, C_TEXT, C_PANEL2, F_TH);
  // decorative stripes after the tab
  for (int i = 0; i < 3 && w + 30 + i * 7 < 182; i++) {
    int sx = w + 18 + i * 7;
    tft.fillTriangle(sx, 21, sx + 4, 4, sx + 8, 4, i == 0 ? col : C_LINE);
    tft.fillTriangle(sx, 21, sx + 8, 4, sx + 4, 21, i == 0 ? col : C_LINE);
  }
  tft.drawFastHLine(0, 24, 320, col);
  tft.drawFastHLine(0, 25, 320, C_BG);
  drawStatus();
}

void drawStatus() {
  const char *s; uint16_t c;
  switch (app.link) {
    case LINK_DEMO:  s = "DEMO";  c = C_WARN; break;
    case LINK_OK:    s = "ECU";   c = C_OK;   break;
    case LINK_WAKING:s = "WAIT";  c = C_ACCENT; break;
    default:         s = "NO ECU";c = C_BAD;  break;
  }
  tft.fillRect(184, 2, 136, 21, C_BG);
  tft.drawRoundRect(186, 3, 80, 19, 4, C_LINE);
  tft.fillCircle(195, 12, 4, c);
  text(s, 203, 13, ML_DATUM, c, C_BG, F_S);
  char v[12];
  if (app.ecu.valid) snprintf(v, sizeof(v), "%.1fV", app.ecu.battV);
  else snprintf(v, sizeof(v), "--.-V");
  tft.drawRoundRect(270, 3, 48, 19, 4, C_LINE);
  text(v, 294, 13, MC_DATUM, C_TEXT, C_BG, F_S);
}

// ---------------------------------------------------------------- widgets
void panel(int x, int y, int w, int h, uint16_t accent) {
  tft.fillRoundRect(x, y, w, h, 5, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 5, C_LINE);
  tft.fillRect(x + 6, y, 18, 2, accent);
  tft.fillRect(x + w - 14, y + 3, 3, 2, C_DIM);
  tft.fillRect(x + w - 9, y + 3, 3, 2, C_DIM);
}

void button(int x, int y, int w, int h, const char *label, uint16_t col, bool filled) {
  uint16_t bg = filled ? col : C_PANEL2;
  tft.fillRoundRect(x, y, w, h, 5, bg);
  tft.drawRoundRect(x, y, w, h, 5, col);
  tft.drawFastHLine(x + 4, y + h - 2, w - 8, filled ? C_BG : col);
  text(label, x + w / 2, y + h / 2, MC_DATUM, filled ? C_BG : C_TEXT, bg, F_TH);
}

void backButton() { button(4, 208, 104, 30, "< กลับ", C_ACCENT); }

// ---------------------------------------------------------------- icons (about 40x40)
void icon(Icon ic, int cx, int cy, uint16_t col) {
  switch (ic) {
    case IC_GAUGE:
      tft.drawArc(cx, cy + 4, 17, 13, 50, 310, col, C_PANEL, true);
      tft.drawWideLine(cx, cy + 4, cx + 9, cy - 6, 3, C_TEXT, C_PANEL);
      tft.fillCircle(cx, cy + 4, 3, C_TEXT);
      break;
    case IC_LIST:
      for (int i = 0; i < 4; i++) {
        tft.fillRect(cx - 16, cy - 13 + i * 8, 5, 5, col);
        tft.fillRect(cx - 8, cy - 12 + i * 8, 24, 3, C_TEXT);
      }
      break;
    case IC_CHECK:
      tft.drawCircle(cx, cy, 16, col);
      tft.drawCircle(cx, cy, 15, col);
      tft.drawWideLine(cx - 8, cy, cx - 2, cy + 7, 4, C_TEXT, C_PANEL);
      tft.drawWideLine(cx - 2, cy + 7, cx + 9, cy - 7, 4, C_TEXT, C_PANEL);
      break;
    case IC_WARN:
      tft.fillTriangle(cx, cy - 17, cx - 19, cy + 14, cx + 19, cy + 14, col);
      tft.fillTriangle(cx, cy - 10, cx - 13, cy + 10, cx + 13, cy + 10, C_PANEL);
      tft.fillRect(cx - 2, cy - 4, 4, 8, C_TEXT);
      tft.fillRect(cx - 2, cy + 6, 4, 3, C_TEXT);
      break;
    case IC_PEAK:
      tft.fillRect(cx - 16, cy + 4, 8, 12, C_DIM);
      tft.fillRect(cx - 4, cy - 4, 8, 20, C_ACCENT2);
      tft.fillRect(cx + 8, cy - 14, 8, 30, col);
      tft.fillTriangle(cx + 12, cy - 20, cx + 6, cy - 14, cx + 18, cy - 14, col);
      break;
    case IC_GRAPH:
      tft.drawFastVLine(cx - 16, cy - 15, 31, C_DIM);
      tft.drawFastHLine(cx - 16, cy + 15, 33, C_DIM);
      tft.drawWideLine(cx - 13, cy + 8, cx - 4, cy - 2, 3, col, C_PANEL);
      tft.drawWideLine(cx - 4, cy - 2, cx + 3, cy + 4, 3, col, C_PANEL);
      tft.drawWideLine(cx + 3, cy + 4, cx + 15, cy - 12, 3, col, C_PANEL);
      break;
    case IC_WAVE: {
      int xs[] = {-18, -12, -12, -4, -4, 4, 4, 10, 10, 18};
      int ys[] = {8, 8, -8, -8, 8, 8, -8, -8, 8, 8};
      for (int i = 0; i < 9; i++)
        tft.drawWideLine(cx + xs[i], cy + ys[i], cx + xs[i + 1], cy + ys[i + 1], 3, col, C_PANEL);
      break;
    }
    case IC_GEAR:
      for (int i = 0; i < 8; i++) {
        float a = i * 3.14159f / 4;
        tft.fillCircle(cx + (int)(15 * cosf(a)), cy + (int)(15 * sinf(a)), 4, col);
      }
      tft.fillCircle(cx, cy, 13, col);
      tft.fillCircle(cx, cy, 6, C_PANEL);
      break;
  }
}
