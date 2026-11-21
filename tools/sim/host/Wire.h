// I2C for the host build: the bus carries the two chips of the board, the
// AXS5106L touch controller (0x63) and the QMI8658 motion sensor (0x6B),
// emulated register by register in sim_board.cpp.
#pragma once
#include <Arduino.h>
class TwoWire {
 public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
  bool end();
  bool setClock(uint32_t hz);
  void beginTransmission(uint8_t address);
  uint8_t endTransmission(bool stop = true);
  size_t write(uint8_t value);
  uint8_t requestFrom(uint8_t address, uint8_t count, uint8_t stop = 1);
  int available();
  int read();
 private:
  bool started = false;
  uint32_t hz = 100000;
  uint8_t address = 0;
  uint8_t tx[64]; size_t txLen = 0;
  uint8_t rx[64]; size_t rxLen = 0, rxAt = 0;
};
extern TwoWire Wire;
