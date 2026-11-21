// Monitor Buddy simulator: storage. LittleFS and the TF card as folders, the
// two app slots and otadata as files, and what the tests read back.
#include "sim.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <Update.h>
#include <sd_diskio.h>
#include <diskio.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <AyresWiFiManager.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace sim;

LittleFSFS LittleFS;
SDFS SD;
UpdateClass Update;

// ── Files ───────────────────────────────────────────────────
namespace fs {
struct FileImpl {
  FILE *f = nullptr;
  bool dir = false, writing = false;
  std::vector<std::string> entries;
  size_t next = 0;
  std::string hostPath, path, name;
  FS *owner = nullptr;
  size_t room = SIZE_MAX;   // bytes the file system still has
  uint32_t writeUsPerKb = 0;
  ~FileImpl() { if (f) fclose(f); }
};

static size_t treeBytes(const std::string &dir, size_t block) {
  size_t total = 0;
  DIR *d = opendir(dir.c_str());
  if (!d) return 0;
  while (struct dirent *e = readdir(d)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    const std::string p = dir + "/" + e->d_name;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) total += block + treeBytes(p, block);
    else total += ((size_t)st.st_size + block - 1) / block * block + (block >= 32768 ? 0 : 256);
  }
  closedir(d);
  return total;
}

std::string FS::host(const char *path) {
  std::string p = path ? path : "";
  if (p.empty() || p[0] != '/') p = "/" + p;
  if (p.find("..") != std::string::npos) return std::string();
  return root() + p;
}
size_t FS::usedBytes() { return mounted() ? treeBytes(root(), blockSize()) : 0; }

File FS::open(const char *path, const char *mode, bool create) {
  if (!mounted()) return File();
  const std::string hostPath = host(path);
  if (hostPath.empty()) return File();
  auto impl = std::make_shared<FileImpl>();
  impl->owner = this;
  impl->hostPath = hostPath;
  impl->path = path;
  const size_t slash = impl->path.rfind('/');
  impl->name = slash == std::string::npos ? impl->path : impl->path.substr(slash + 1);
  struct stat st;
  const bool exists = stat(hostPath.c_str(), &st) == 0;
  if (exists && S_ISDIR(st.st_mode)) {
    impl->dir = true;
    DIR *d = opendir(hostPath.c_str());
    if (!d) return File();
    while (struct dirent *e = readdir(d)) {
      if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) impl->entries.push_back(e->d_name);
    }
    closedir(d);
    std::sort(impl->entries.begin(), impl->entries.end());
    return File(impl);
  }
  const char m = mode && mode[0] ? mode[0] : 'r';
  if (m == 'r') {
    if (!exists) return File();
    impl->f = fopen(hostPath.c_str(), "rb");
  } else {
    if (create) {
      for (size_t at = root().size() + 1; (at = hostPath.find('/', at)) != std::string::npos; at++) ::mkdir(hostPath.substr(0, at).c_str(), 0755);
    }
    impl->f = fopen(hostPath.c_str(), m == 'a' ? "ab" : "wb");
    impl->writing = true;
    impl->writeUsPerKb = writeUsPerKb();
    FS *self = this;
    const uint64_t total = self == (FS *)&LittleFS ? LittleFS.totalBytes() : SD.totalBytes();
    const size_t used = usedBytes();
    impl->room = total > used ? (size_t)(total - used) : 0;
    spendUs(4000);
  }
  if (!impl->f) return File();
  return File(impl);
}
bool FS::exists(const char *path) {
  struct stat st;
  return mounted() && !host(path).empty() && stat(host(path).c_str(), &st) == 0;
}
bool FS::remove(const char *path) { spendUs(3000); return mounted() && !host(path).empty() && ::unlink(host(path).c_str()) == 0; }
bool FS::rename(const char *from, const char *to) {
  spendUs(3000);
  return mounted() && !host(from).empty() && !host(to).empty() && ::rename(host(from).c_str(), host(to).c_str()) == 0;
}
bool FS::mkdir(const char *path) {
  if (!mounted() || host(path).empty()) return false;
  struct stat st;
  if (stat(host(path).c_str(), &st) == 0) return S_ISDIR(st.st_mode);
  return ::mkdir(host(path).c_str(), 0755) == 0;
}
bool FS::rmdir(const char *path) { return mounted() && !host(path).empty() && ::rmdir(host(path).c_str()) == 0; }

size_t File::write(const uint8_t *buf, size_t size) {
  if (!p || !p->f || !p->writing || !p->owner) return 0;
  if (size > p->room) size = p->room;   // the file system is full: a short write
  const size_t n = fwrite(buf, 1, size, p->f);
  p->room -= n;
  spendUs((uint64_t)n * p->writeUsPerKb / 1024ULL);
  return n;
}
int File::available() {
  if (!p || !p->f || p->writing) return 0;
  const long at = ftell(p->f);
  return (int)((long)size() - at);
}
int File::read() { if (!p || !p->f) return -1; const int c = fgetc(p->f); return c == EOF ? -1 : c; }
int File::peek() { if (!p || !p->f) return -1; const int c = fgetc(p->f); if (c != EOF) ungetc(c, p->f); return c == EOF ? -1 : c; }
void File::flush() { if (p && p->f) fflush(p->f); }
size_t File::read(uint8_t *buf, size_t size) {
  if (!p || !p->f) return 0;
  const size_t n = fread(buf, 1, size, p->f);
  spendUs((uint64_t)n * 250ULL / 1024ULL);
  return n;
}
bool File::seek(uint32_t pos) { return p && p->f && fseek(p->f, (long)pos, SEEK_SET) == 0; }
size_t File::position() const { return p && p->f ? (size_t)ftell(p->f) : 0; }
size_t File::size() const {
  if (!p || !p->f) return 0;
  fflush(p->f);
  struct stat st;
  return stat(p->hostPath.c_str(), &st) == 0 ? (size_t)st.st_size : 0;
}
void File::close() { if (p && p->f) { fclose(p->f); p->f = nullptr; } p.reset(); }
File::operator bool() const { return p && (p->dir || p->f); }
const char *File::name() const { return p ? p->name.c_str() : ""; }
const char *File::path() const { return p ? p->path.c_str() : ""; }
bool File::isDirectory() { return p && p->dir; }
File File::openNextFile(const char *mode) {
  if (!p || !p->dir || !p->owner) return File();
  while (p->next < p->entries.size()) {
    const std::string child = (p->path == "/" ? "" : p->path) + "/" + p->entries[p->next++];
    File f = p->owner->open(child.c_str(), mode);
    if (f) return f;
  }
  return File();
}
void File::rewindDirectory() { if (p) p->next = 0; }
}  // namespace fs

bool LittleFSFS::begin(bool, const char *, uint8_t, const char *) {
  ::mkdir(fsRoot("littlefs").c_str(), 0755);
  up = true;
  return true;
}
bool LittleFSFS::format() { return true; }
std::string LittleFSFS::root() { return fsRoot("littlefs"); }

// ── TF card ─────────────────────────────────────────────────
static const uint64_t CARD_BYTES = 31914983424ULL;   // a 32 GB card
bool SDFS::begin(uint8_t, SPIClass &, uint32_t, const char *, uint8_t, bool) {
  spendUs(board.card == "none" ? 1100000 : 180000);
  up = board.card == "ok";
  if (up) ::mkdir(fsRoot("sd").c_str(), 0755);
  return up;
}
uint64_t SDFS::cardSize() { return board.card == "none" ? 0 : CARD_BYTES; }
std::string SDFS::root() {
  if (board.card != "ok") up = false;   // pulled out while running
  return fsRoot("sd");
}

uint8_t sdcard_init(uint8_t, SPIClass *, int) { return 0; }
uint8_t sdcard_uninit(uint8_t) { return 0; }
uint32_t sdcard_num_sectors(uint8_t) { return (uint32_t)(CARD_BYTES / 512); }
uint32_t sdcard_sector_size(uint8_t) { return 512; }
uint8_t sdcard_type(uint8_t) { return board.card == "none" ? 0 : 3; }   // SDHC
DSTATUS disk_initialize(uint8_t) { spendUs(board.card == "none" ? 1000000 : 150000); return board.card == "none" ? STA_NOINIT : 0; }
DRESULT disk_read(uint8_t, uint8_t *buffer, uint32_t sector, unsigned count) {
  if (board.card == "none") return RES_NOTRDY;
  memset(buffer, 0, 512 * count);
  if (sector == 0) {            // the master boot record: one FAT32 partition at 8192
    buffer[0x1C2] = board.card == "ok" ? 0x0C : 0x83;
    buffer[0x1C6] = 0x00; buffer[0x1C7] = 0x20;
    buffer[0x1FE] = 0x55; buffer[0x1FF] = 0xAA;
  } else if (sector == 8192) {  // its boot sector
    memcpy(buffer + 3, board.card == "ok" ? "MSDOS5.0" : "\0\0\0\0\0\0\0\0", 8);
    buffer[0x1FE] = 0x55; buffer[0x1FF] = 0xAA;
  }
  return RES_OK;
}

// ── App slots and otadata ───────────────────────────────────
static std::string otaBoot = "app0", otaRunning = "app0";
static std::map<std::string, std::string> otaState;   // "valid", "new", "pending", "aborted", ""
static esp_partition_t runningPartition = {};
static esp_app_desc_t appDesc = {};
static bool buildIdGiven = false;
static const size_t SLOT_BYTES = 0x300000;

static std::string otaPath() { return board.dataDir + "/ota.state"; }
static std::string slotPath(const std::string &slot) { return board.dataDir + "/slot-" + slot + ".bin"; }
static void otaSave() {
  FILE *f = fopen(otaPath().c_str(), "w");
  if (!f) return;
  fprintf(f, "boot=%s\napp0=%s\napp1=%s\n", otaBoot.c_str(), otaState["app0"].c_str(), otaState["app1"].c_str());
  fclose(f);
}
static std::string other(const std::string &slot) { return slot == "app0" ? "app1" : "app0"; }

int simBuildIdSet(const char *hex) {
  for (int i = 0; i < 32 && hex[2 * i] && hex[2 * i + 1]; i++) {
    char byte[3] = {hex[2 * i], hex[2 * i + 1], 0};
    appDesc.app_elf_sha256[i] = (uint8_t)strtol(byte, nullptr, 16);
  }
  buildIdGiven = true;
  return 0;
}

// What the bootloader does before the app runs: pick the slot, start a trial,
// or go back to the slot before when a trial was never confirmed.
void simBoot() {
  otaState["app0"] = "valid";
  otaState["app1"] = "";
  if (FILE *f = fopen(otaPath().c_str(), "r")) {
    char key[32], value[32];
    char line[80];
    while (fgets(line, sizeof(line), f)) {
      value[0] = 0;
      if (sscanf(line, "%31[^=]=%31s", key, value) >= 1) {
        if (!strcmp(key, "boot")) otaBoot = value;
        else otaState[key] = value;
      }
    }
    fclose(f);
  }
  if (otaState[otaBoot] == "pending") {          // restarted during the trial: roll back
    otaState[otaBoot] = "aborted";
    otaBoot = other(otaBoot);
  } else if (otaState[otaBoot] == "new") {       // first boot of an update: on trial
    otaState[otaBoot] = "pending";
  }
  otaRunning = otaBoot;
  otaSave();
  strlcpy(runningPartition.label, otaRunning.c_str(), sizeof(runningPartition.label));
  runningPartition.address = otaRunning == "app0" ? 0x10000 : 0x400000;
  runningPartition.size = SLOT_BYTES;
  // The build id is the ELF hash at 0xB0 of the image in the running slot.
  if (FILE *f = fopen(slotPath(otaRunning).c_str(), "rb")) {
    uint8_t hash[32];
    if (fseek(f, 0xB0, SEEK_SET) == 0 && fread(hash, 1, 32, f) == 32) memcpy(appDesc.app_elf_sha256, hash, 32);
    fclose(f);
  } else if (!buildIdGiven) {
    for (int i = 0; i < 32; i++) appDesc.app_elf_sha256[i] = (uint8_t)(0x51 + i * 7);
  }
  strlcpy(appDesc.project_name, "monitor-buddy", sizeof(appDesc.project_name));
  strlcpy(appDesc.version, "sim", sizeof(appDesc.version));
}

const esp_partition_t *esp_ota_get_running_partition() { return &runningPartition; }
const esp_app_desc_t *esp_app_get_description() { return &appDesc; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state) {
  const std::string s = otaState[partition->label];
  if (s.empty()) return ESP_FAIL;
  *state = s == "pending" ? ESP_OTA_IMG_PENDING_VERIFY : s == "new" ? ESP_OTA_IMG_NEW : s == "aborted" ? ESP_OTA_IMG_ABORTED : ESP_OTA_IMG_VALID;
  return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
  otaState[otaRunning] = "valid";
  otaSave();
  return ESP_OK;
}

bool UpdateClass::begin(size_t wanted) {
  abort();
  error = 0;
  if (wanted == 0 || wanted > SLOT_BYTES) { error = 4; return false; }   // UPDATE_ERROR_SIZE
  slot = other(otaRunning);
  file = fopen((slotPath(slot) + ".part").c_str(), "wb");
  if (!file) { error = 1; return false; }
  size = wanted;
  written = 0;
  spendUs(900000);   // erasing the slot
  return true;
}
size_t UpdateClass::write(uint8_t *data, size_t len) {
  if (!file || written + len > size) { error = 1; return 0; }
  const size_t n = fwrite(data, 1, len, file);
  written += n;
  spendUs((uint64_t)n * 45ULL);   // about 22 KB/s of flash writes between the radio's turns
  if (n != len) error = 1;
  return n;
}
bool UpdateClass::end(bool evenIfRemaining) {
  if (!file) { error = 12; return false; }
  fclose(file);
  file = nullptr;
  if (written != size && !evenIfRemaining) { error = 4; ::unlink((slotPath(slot) + ".part").c_str()); return false; }
  // The real Update checks the image's first byte before it switches slots.
  uint8_t magic = 0;
  if (FILE *f = fopen((slotPath(slot) + ".part").c_str(), "rb")) { if (fread(&magic, 1, 1, f) != 1) magic = 0; fclose(f); }
  if (magic != 0xE9) { error = 8; ::unlink((slotPath(slot) + ".part").c_str()); return false; }   // UPDATE_ERROR_MAGIC_BYTE
  ::rename((slotPath(slot) + ".part").c_str(), slotPath(slot).c_str());
  otaBoot = slot;
  otaState[slot] = "new";
  otaSave();
  return true;
}
void UpdateClass::abort() {
  if (!file) return;
  fclose(file);
  file = nullptr;
  ::unlink((slotPath(slot) + ".part").c_str());
}
const char *UpdateClass::errorString() {
  switch (error) {
    case 0: return "No Error";
    case 1: return "Flash Write Failed";
    case 4: return "Bad Size Given";
    case 8: return "Wrong Magic Byte";
    case 12: return "Aborted";
    default: return "UNKNOWN";
  }
}

// ── What the tests ask about the board ──────────────────────
std::string simStateJson() {
  char b[1024];
  snprintf(b, sizeof(b),
           "\"backlight\":%d,\"panel_on\":%s,\"rotation\":%d,\"flushes\":%llu,\"flushed_pixels\":%llu,"
           "\"wifi\":%s,\"ssid\":\"%s\",\"portal\":%s,\"http_requests\":%llu,\"slot\":\"%s\",\"slot_state\":\"%s\","
           "\"card\":\"%s\",\"touch\":%s,\"task\":\"%s\"",
           board.backlight, board.panelOn ? "true" : "false", board.rotation,
           (unsigned long long)board.flushes, (unsigned long long)board.flushedPixels,
           wifiConnected() ? "true" : "false", wifiSsid().c_str(),
           AyresWiFiManager::instance && AyresWiFiManager::instance->isPortalActive() ? "true" : "false",
           (unsigned long long)board.httpRequests, otaRunning.c_str(), otaState[otaRunning].c_str(),
           board.card.c_str(), board.touchDown ? "true" : "false", taskName());
  std::string out = b;
  out += ",\"http_log\":[";
  for (size_t i = 0; i < board.httpLog.size(); i++) {
    std::string url = board.httpLog[i];
    for (auto &c : url) if (c == '"' || c == '\\') c = '_';
    out += (i ? ",\"" : "\"") + url + "\"";
  }
  board.httpLog.clear();
  return out + "]";
}
