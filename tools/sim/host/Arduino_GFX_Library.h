// The display stack for the host build. Arduino_GFX, Arduino_Canvas,
// Arduino_TFT and Arduino_ST7789 are the real library sources; only the SPI
// bus is replaced, by one that feeds an emulated panel (sim_board.cpp), so
// the firmware's own init sequence, address windows, offsets and rotation
// decide what ends up on the simulated glass.
#pragma once
#include <Arduino.h>
#include "Arduino_DataBus.h"
#include "Arduino_GFX.h"
#include "Arduino_TFT.h"
#include "canvas/Arduino_Canvas.h"
#include "display/Arduino_ST7789.h"

class Arduino_HWSPI : public Arduino_DataBus {
 public:
  Arduino_HWSPI(int8_t dc, int8_t cs = GFX_NOT_DEFINED, int8_t sck = GFX_NOT_DEFINED, int8_t mosi = GFX_NOT_DEFINED,
                int8_t miso = GFX_NOT_DEFINED, void *spi = nullptr, bool is_shared_interface = true) {
    (void)dc; (void)cs; (void)sck; (void)mosi; (void)miso; (void)spi; (void)is_shared_interface;
  }
  bool begin(int32_t speed = SPI_DEFAULT_FREQ, int8_t dataMode = GFX_NOT_DEFINED) override;
  void beginWrite() override;
  void endWrite() override;
  void writeCommand(uint8_t c) override;
  void writeCommand16(uint16_t c) override;
  void writeCommandBytes(uint8_t *data, uint32_t len) override;
  void write(uint8_t d) override;
  void write16(uint16_t d) override;
  void writeRepeat(uint16_t p, uint32_t len) override;
  void writeBytes(uint8_t *data, uint32_t len) override;
  void writePixels(uint16_t *data, uint32_t len) override;
};
