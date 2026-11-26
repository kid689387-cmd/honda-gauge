#include "thai_shape.h"
#include <vector>
#include <stdint.h>

static std::vector<uint32_t> decode(const std::string &s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i];
    if (c < 0x80) { out.push_back(c); i += 1; }
    else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
      out.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
      out.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)); i += 3;
    } else { i += 1; }
  }
  return out;
}

static void encode(uint32_t cp, std::string &out) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

static bool isConsonant(uint32_t c) { return c >= 0x0E01 && c <= 0x0E2E; }
static bool isTall(uint32_t c)      { return c == 0x0E1B || c == 0x0E1D || c == 0x0E1F || c == 0x0E2C; }
static bool isUpperVowel(uint32_t c){ return c == 0x0E31 || (c >= 0x0E34 && c <= 0x0E37) || c == 0x0E47 || c == 0x0E4D; }
static bool isTone(uint32_t c)      { return c >= 0x0E48 && c <= 0x0E4C; }

std::string thaiShape(const std::string &utf8) {
  std::vector<uint32_t> cps = decode(utf8);
  bool tall = false, hasUpper = false;
  std::string out;
  for (size_t i = 0; i < cps.size(); i++) {
    uint32_t c = cps[i];
    if (isConsonant(c)) { tall = isTall(c); hasUpper = false; }
    else if (isUpperVowel(c)) {
      hasUpper = true;
      if (tall) {
        if (c == 0x0E31) c = 0xF710;
        else if (c >= 0x0E34 && c <= 0x0E37) c = 0xF701 + (c - 0x0E34);
        else if (c == 0x0E47) c = 0xF717;
        else if (c == 0x0E4D) c = 0xF711;
      }
    } else if (isTone(c)) {
      bool upper = hasUpper || (i + 1 < cps.size() && cps[i + 1] == 0x0E33); // SARA AM
      if (!upper) c = (tall ? 0xF705 : 0xF70A) + (c - 0x0E48);
      else if (tall) c = 0xF712 + (c - 0x0E48);
    } else if (c < 0x0E00 || c > 0x0E7F) { tall = false; hasUpper = false; }
    encode(c, out);
  }
  return out;
}
