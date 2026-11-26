// Desktop stand-in for TFT_eSPI: draws into a 320x240 RGB565 framebuffer so
// screens can be checked as images. Only the calls the firmware uses.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <stdio.h>

enum { TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM };
#define TFT_RED   0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE  0x001F
#define TFT_WHITE 0xFFFF
#define TFT_BLACK 0x0000

class TFT_eSPI {
public:
  uint16_t fb[320 * 240];
  void init() {}
  void setRotation(int) {}
  void invertDisplay(bool) {}
  int width() { return 320; }
  int height() { return 240; }

  void drawPixel(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < 320 && y < 240) fb[y * 320 + x] = c; }
  void blend(int x, int y, uint16_t fg, uint16_t bg, uint8_t a) {
    if (a == 0) return;
    if (a >= 250) { drawPixel(x, y, fg); return; }
    auto ch = [&](int sh, int m) { int f = (fg >> sh) & m, b = (bg >> sh) & m; return (b + (f - b) * a / 255) & m; };
    drawPixel(x, y, (ch(11, 31) << 11) | (ch(5, 63) << 5) | ch(0, 31));
  }
  void fillScreen(uint16_t c) { for (auto &p : fb) p = c; }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) drawPixel(i, j, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c); drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c); }
  bool inRound(int px, int py, int x, int y, int w, int h, int r) {
    int cx = std::min(std::max(px, x + r), x + w - 1 - r), cy = std::min(std::max(py, y + r), y + h - 1 - r);
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (inRound(i, j, x, y, w, h, r)) drawPixel(i, j, c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
      if (inRound(i, j, x, y, w, h, r) && !inRound(i, j, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0)) drawPixel(i, j, c);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    while (true) { drawPixel(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e;
      if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
  }
  void drawWideLine(float x0, float y0, float x1, float y1, float w, uint16_t c, uint16_t = 0) {
    int n = (int)(hypotf(x1 - x0, y1 - y0) * 2) + 1;
    for (int i = 0; i <= n; i++) fillCircle((int)(x0 + (x1 - x0) * i / n), (int)(y0 + (y1 - y0) * i / n), (int)(w / 2), c);
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r + r) drawPixel(cx + i, cy + j, c); }
  void drawCircle(int cx, int cy, int r, uint16_t c) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) { int d = i * i + j * j; if (d <= r * r + r && d > (r - 1) * (r - 1) + (r - 1)) drawPixel(cx + i, cy + j, c); } }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto e = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
      int a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), d = e(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
    }
  }
  void drawArc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t, bool = true) {
    for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++) {
      float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
      if (d > r + 0.5f || d < ir - 0.5f) continue;
      float ang = atan2f(-dx, dy) * 180 / 3.14159265f; if (ang < 0) ang += 360;
      if (ang >= a0 && ang <= a1) drawPixel(x, y, fg);
    }
  }

  // ---- smooth fonts (.vlw arrays)
  struct G { uint32_t cp; int h, w, adv, dy, dx; const uint8_t *bmp; };
  std::vector<G> glyphs; int maxAscent = 0, maxDescent = 0, yAdvance = 0; bool fontLoaded = false;
  uint16_t fgc = 0xFFFF, bgc = 0; int datum = 0, padX = 0;
  static int32_t rd(const uint8_t *p) { return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
  void loadFont(const uint8_t *a) {
    glyphs.clear(); int n = rd(a); maxAscent = maxDescent = 0;
    const uint8_t *bm = a + 24 + 28 * n;
    for (int i = 0; i < n; i++) {
      const uint8_t *m = a + 24 + 28 * i;
      G g{(uint32_t)rd(m), rd(m + 4), rd(m + 8), rd(m + 12), rd(m + 16), rd(m + 20), bm};
      bm += g.w * g.h; glyphs.push_back(g);
      if (g.cp > 0x20 && g.cp < 0xFF && g.cp != 0xA0) { maxAscent = std::max(maxAscent, g.dy); maxDescent = std::max(maxDescent, g.h - g.dy); }
    }
    yAdvance = maxAscent + maxDescent; fontLoaded = true;
  }
  void unloadFont() { fontLoaded = false; glyphs.clear(); }
  void setTextColor(uint16_t f, uint16_t b, bool = false) { fgc = f; bgc = b; }
  void setTextDatum(int d) { datum = d; }
  void setTextPadding(int p) { padX = p; }
  static std::vector<uint32_t> utf8(const char *s) {
    std::vector<uint32_t> o; const uint8_t *p = (const uint8_t *)s;
    while (*p) { uint32_t c = *p;
      if (c < 0x80) { o.push_back(c); p++; }
      else if ((c & 0xE0) == 0xC0) { o.push_back(((c & 0x1F) << 6) | (p[1] & 0x3F)); p += 2; }
      else { o.push_back(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)); p += 3; } }
    return o;
  }
  const G *find(uint32_t c) { for (auto &g : glyphs) if (g.cp == c) return &g; return nullptr; }
  int textWidth(const char *s, int = 0) {
    if (!fontLoaded) return (int)strlen(s) * 7;
    int w = 0; for (uint32_t c : utf8(s)) { const G *g = find(c); w += g ? g->adv : 8; } return w;
  }
  int fontHeight() { return fontLoaded ? yAdvance : 16; }
  int drawString(const char *s, int x, int y, int font = 0) {
    int cw = textWidth(s), chh = fontHeight();
    int pad = 1;
    switch (datum) {
      case TC_DATUM: x -= cw / 2; pad = 2; break;
      case TR_DATUM: x -= cw; pad = 3; break;
      case ML_DATUM: y -= chh / 2; break;
      case MC_DATUM: x -= cw / 2; y -= chh / 2; pad = 2; break;
      case MR_DATUM: x -= cw; y -= chh / 2; pad = 3; break;
      case BL_DATUM: y -= chh; break;
      case BC_DATUM: x -= cw / 2; y -= chh; pad = 2; break;
      case BR_DATUM: x -= cw; y -= chh; pad = 3; break;
    }
    if (x < 0 || x + cw > 320) fprintf(stderr, "OFF-SCREEN x=%d w=%d: %s\n", x, cw, s);
    if (!fontLoaded) {   // built-in font 2: draw simple blocks so layout is visible
      fillRect(x, y, cw, chh, bgc);
      for (size_t i = 0; i < strlen(s); i++) if (s[i] != ' ') fillRect(x + i * 7 + 1, y + 4, 5, 8, fgc);
    } else {
      int cx = x;
      for (uint32_t c : utf8(s)) {
        const G *g = find(c);
        if (!g) { drawRect(cx, y, 7, chh, fgc); cx += 8; continue; }
        for (int r = 0; r < g->h; r++) for (int k = 0; k < g->w; k++)
          blend(cx + g->dx + k, y + maxAscent - g->dy + r, fgc, bgc, g->bmp[r * g->w + k]);
        cx += g->adv;
      }
    }
    if (padX > cw) {
      int extra = padX - cw;
      if (pad == 1) fillRect(x + cw, y, extra, chh, bgc);
      else if (pad == 2) { fillRect(x - extra / 2, y, extra / 2, chh, bgc); fillRect(x + cw, y, extra - extra / 2, chh, bgc); }
      else fillRect(x - extra, y, extra, chh, bgc);
    }
    return cw;
  }
};
