// Honda K-Line (single wire, 10400 baud) driver
#pragma once
#include <Arduino.h>
#include "app.h"

#define KLINE_RX_PIN 22
#define KLINE_TX_PIN 27
#define KLINE_BAUD   10400

// EcuData lives in app.h

enum KState { K_IDLE, K_WAKING, K_CONNECTED, K_ERROR };

class HondaKLine {
public:
  void begin(bool invertTx);
  void setInvertTx(bool inv);
  bool connect();                         // wake-up + init, true if ECU answered
  bool readTable(uint8_t table, uint8_t *out, uint8_t &outLen); // payload after table id
  bool readLiveData(EcuData &d);          // table 0x11
  int  readDtc(char codes[][8], int maxCodes, bool past); // returns count, -1 on error
  bool clearDtc();
  bool readEcmId(char *out, size_t n);    // "01 02 32 0F 01"
  KState state() const { return st; }
  const char *lastError() const { return err; }

  // last raw frame for the RAW screen
  uint8_t lastFrame[64];
  uint8_t lastFrameLen = 0;

private:
  bool invTx = false;
  KState st = K_IDLE;
  const char *err = "";
  void setIdle();
  void setActive();
  void startUart();
  bool sendFrame(const uint8_t *buf, uint8_t len);
  bool readFrame(uint8_t *buf, uint8_t &len, uint32_t timeoutMs);
  bool command(const uint8_t *req, uint8_t reqLen, uint8_t *resp, uint8_t &respLen);
  static uint8_t checksum(const uint8_t *buf, uint8_t len);
};
