// Firmware updates for the host build: the image goes into the file of the
// app slot that is not running (slot-app0.bin / slot-app1.bin in the data
// folder), and ota.json plays the otadata partition.
#pragma once
#include <Arduino.h>
class UpdateClass {
 public:
  bool begin(size_t size);
  size_t write(uint8_t *data, size_t len);
  bool end(bool evenIfRemaining = false);
  void abort();
  bool hasError() { return error != 0; }
  const char *errorString();
  bool isRunning() { return file != nullptr; }
 private:
  FILE *file = nullptr;
  size_t size = 0, written = 0;
  int error = 0;
  std::string slot;
};
extern UpdateClass Update;
