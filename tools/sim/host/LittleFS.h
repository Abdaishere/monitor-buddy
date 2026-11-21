#pragma once
#include <FS.h>
class LittleFSFS : public fs::FS {
 public:
  bool begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpenFiles = 10, const char *partitionLabel = "spiffs");
  void end() { up = false; }
  bool format();
  size_t totalBytes() { return 0xE0000; }   // the spiffs partition of partitions.csv
 protected:
  bool mounted() override { return up; }
  std::string root() override;
 private:
  bool up = false;
};
extern LittleFSFS LittleFS;
