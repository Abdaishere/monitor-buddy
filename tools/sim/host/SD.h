#pragma once
#include <FS.h>
#include <SPI.h>
class SDFS : public fs::FS {
 public:
  bool begin(uint8_t cs = 5, SPIClass &spi = SPI, uint32_t hz = 4000000, const char *mount = "/sd", uint8_t maxFiles = 5, bool formatIfEmpty = false);
  void end() { up = false; }
  uint64_t cardSize();
  uint64_t totalBytes() { return cardSize(); }
 protected:
  bool mounted() override { return up; }
  std::string root() override;
  size_t blockSize() override { return 32768; }
  uint32_t writeUsPerKb() override { return 900; }
 private:
  bool up = false;
};
extern SDFS SD;
