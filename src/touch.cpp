#include <Arduino.h>
#include "touch.h"

// CYD touch GPIOs (same pins the hardware-SPI version used).
#define T_CLK   25
#define T_MISO  39   // input-only pin: fine for MISO
#define T_MOSI  32
#define T_CS    33

#define Z_THRESHOLD 400

// --- bit-bang one SPI transfer, MODE0 (CPOL0/CPHA0), MSB first ------------
// Sends an 8-bit command, then clocks out a 16-bit result. Returns the top
// 12 bits (the XPT2046 result is left-aligned in the 16-bit word).
static uint16_t xfer(uint8_t cmd) {
  // command phase
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_MOSI, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH);
    digitalWrite(T_CLK, LOW);
  }
  // result phase: 16 clocks, sample MISO after the rising edge
  uint16_t v = 0;
  digitalWrite(T_MOSI, LOW);
  for (int i = 0; i < 16; i++) {
    digitalWrite(T_CLK, HIGH);
    v = (v << 1) | (digitalRead(T_MISO) & 1);
    digitalWrite(T_CLK, LOW);
  }
  return v >> 3;            // keep 12 significant bits
}

static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
  int16_t da = x > y ? x - y : y - x;
  int16_t db = x > z ? x - z : z - x;
  int16_t dc = z > y ? z - y : y - z;
  if (da <= db && da <= dc) return (x + y) >> 1;
  if (db <= da && db <= dc) return (x + z) >> 1;
  return (y + z) >> 1;
}

void touchBegin() {
  pinMode(T_CLK, OUTPUT);
  pinMode(T_MOSI, OUTPUT);
  pinMode(T_CS, OUTPUT);
  pinMode(T_MISO, INPUT);
  digitalWrite(T_CLK, LOW);
  digitalWrite(T_CS, HIGH);
}

bool touchGetRaw(int &ox, int &oy) {
  digitalWrite(T_CS, LOW);
  int z1 = xfer(0xB1);              // Z1
  int z2 = xfer(0xC1);             // Z2
  int z = z1 + 4095 - z2;
  if (z < Z_THRESHOLD) { digitalWrite(T_CS, HIGH); return false; }
  // Match XPT2046_Touchscreen at rotation 1: xraw comes from the 0x91 channel,
  // yraw from the 0xD1 channel. (The library reads them pipelined; done plainly
  // here, so the two channels must be grouped the same way or the axes swap.)
  xfer(0x91);                       // discard first (noisy) reading
  int a0 = xfer(0x91), b0 = xfer(0xD1);
  int a1 = xfer(0x91), b1 = xfer(0xD1);
  int a2 = xfer(0x90), b2 = xfer(0xD0);   // power-down variants on the last pair
  digitalWrite(T_CS, HIGH);
  ox = besttwoavg(a0, a1, a2);      // 0x91 channel -> xraw
  oy = besttwoavg(b0, b1, b2);      // 0xD1 channel -> yraw
  return true;
}
