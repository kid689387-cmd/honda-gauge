// XPT2046 resistive touch over software (bit-bang) SPI.
//
// Why bit-bang: on the CYD the microSD card is wired to the hardware VSPI
// pins (18/19/23/5) and the display owns HSPI, so the only way to run the
// touch panel at the same time as the card is to drive the touch controller
// on its own GPIOs in software. This mirrors the read sequence and the
// rotation(1) result of PaulStoffregen's XPT2046_Touchscreen, so the existing
// calibration in getTouch() stays valid.
#pragma once

void touchBegin();
// Returns true when pressed; ox/oy are raw 12-bit values (0..4095) for
// rotation 1, i.e. the same scale the old library produced.
bool touchGetRaw(int &ox, int &oy);
