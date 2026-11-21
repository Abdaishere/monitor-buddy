// Files for the host build: LittleFS and the TF card are folders under the
// simulator's data folder.
#pragma once
#include <Arduino.h>
#include <memory>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
struct FileImpl;
class File : public Stream {
 public:
  File() {}
  explicit File(std::shared_ptr<FileImpl> impl) : p(impl) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t size) override;
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t read(uint8_t *buf, size_t size);
  size_t readBytes(char *buffer, size_t length) override { return read((uint8_t *)buffer, length); }
  bool seek(uint32_t pos);
  size_t position() const;
  size_t size() const;
  void close();
  operator bool() const;
  const char *name() const;
  const char *path() const;
  bool isDirectory();
  File openNextFile(const char *mode = FILE_READ);
  void rewindDirectory();
 private:
  std::shared_ptr<FileImpl> p;
};
class FS {
 public:
  virtual ~FS() {}
  File open(const char *path, const char *mode = FILE_READ, bool create = false);
  File open(const String &path, const char *mode = FILE_READ, bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char *path);
  bool exists(const String &path) { return exists(path.c_str()); }
  bool remove(const char *path);
  bool remove(const String &path) { return remove(path.c_str()); }
  bool rename(const char *from, const char *to);
  bool rename(const String &from, const String &to) { return rename(from.c_str(), to.c_str()); }
  bool mkdir(const char *path);
  bool mkdir(const String &path) { return mkdir(path.c_str()); }
  bool rmdir(const char *path);
  size_t usedBytes();
 protected:
  virtual bool mounted() = 0;
  virtual std::string root() = 0;
  virtual size_t blockSize() { return 4096; }
  virtual uint32_t writeUsPerKb() { return 1500; }
  std::string host(const char *path);
};
}  // namespace fs
using fs::File;
using fs::FS;
