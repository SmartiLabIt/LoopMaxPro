// LoopMaxPro HAL - ESP-IDF: filesystem and key/value store.
#include <dirent.h>
#include <errno.h>
#include <esp_littlefs.h>
#include <fcntl.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal/esp32/esp32_paths.h"
#include "hal/esp32/esp32_board.h"

namespace lmp::hal::esp32 {
namespace {

constexpr const char* kMountPoint = kFsMountPoint;
constexpr const char* kPartition = kFsPartition;
constexpr size_t kMaxOpenFiles = 4;
constexpr size_t kMaxPath = 160;

// ---------------------------------------------------------------- files

class File : public IFile {
 public:
  bool inUse() const { return fd_ >= 0; }
  void attach(int fd) { fd_ = fd; }

  size_t read(uint8_t* out, size_t cap) override {
    const ssize_t n = ::read(fd_, out, cap);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
  size_t write(const uint8_t* data, size_t len) override {
    const ssize_t n = ::write(fd_, data, len);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
  bool seek(size_t position) override {
    return ::lseek(fd_, static_cast<off_t>(position), SEEK_SET) >= 0;
  }
  size_t position() const override {
    const off_t p = ::lseek(fd_, 0, SEEK_CUR);
    return p > 0 ? static_cast<size_t>(p) : 0;
  }
  size_t size() const override {
    struct stat info;
    return ::fstat(fd_, &info) == 0 ? static_cast<size_t>(info.st_size) : 0;
  }
  void close() override {
    ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
};

File g_files[kMaxOpenFiles];

// Path of the device, seen from the root of the system. False when it does not fit.
bool fullPath(const char* path, char* out, size_t cap) {
  if (!path) return false;
  const int n = snprintf(out, cap, "%s%s%s", kMountPoint, path[0] == '/' ? "" : "/", path);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool pathExists(const char* path) {
  char full[kMaxPath];
  struct stat info;
  return fullPath(path, full, sizeof(full)) && ::stat(full, &info) == 0;
}

// Creates the folders that lead to a file.
void makeParents(char* full) {
  for (char* p = full + strlen(kMountPoint) + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    ::mkdir(full, 0775);
    *p = '/';
  }
}

Status fromEsp(esp_err_t err) {
  switch (err) {
    case ESP_OK:                        return Status::Ok;
    case ESP_ERR_NVS_NOT_FOUND:         return Status::NotFound;
    case ESP_ERR_NVS_INVALID_LENGTH:    return Status::NoMem;
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
    case ESP_ERR_NVS_NO_FREE_PAGES:     return Status::NoMem;
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_KEY_TOO_LONG:      return Status::Invalid;
    default:                            return Status::Io;
  }
}

}  // namespace

// ---------------------------------------------------------------- filesystem

Status Fs::mount(bool formatOnFail) {
  if (mounted_) return Status::Ok;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kMountPoint;
  conf.partition_label = kPartition;
  conf.format_if_mount_failed = formatOnFail;
  mounted_ = esp_vfs_littlefs_register(&conf) == ESP_OK;
  return mounted_ ? Status::Ok : Status::Io;
}

void Fs::unmount() {
  if (!mounted_) return;
  esp_vfs_littlefs_unregister(kPartition);
  mounted_ = false;
}

Status Fs::info(FsInfo& out) const {
  if (!mounted_) return Status::Io;
  size_t total = 0;
  size_t used = 0;
  if (esp_littlefs_info(kPartition, &total, &used) != ESP_OK) return Status::Io;
  out.totalBytes = total;
  out.usedBytes = used;
  return Status::Ok;
}

IFile* Fs::open(const char* path, OpenMode mode) {
  char full[kMaxPath];
  if (!mounted_ || !fullPath(path, full, sizeof(full))) return nullptr;
  File* slot = nullptr;
  for (File& f : g_files) {
    if (!f.inUse()) {
      slot = &f;
      break;
    }
  }
  if (!slot) return nullptr;

  struct stat info;
  const bool found = ::stat(full, &info) == 0;
  if (found && S_ISDIR(info.st_mode)) return nullptr;

  int flags = O_RDONLY;
  if (mode == OpenMode::Read) {
    if (!found) return nullptr;
  } else {
    flags = O_WRONLY | O_CREAT | (mode == OpenMode::Write ? O_TRUNC : O_APPEND);
    if (!found) makeParents(full);
  }
  const int fd = ::open(full, flags, 0664);
  if (fd < 0) return nullptr;
  slot->attach(fd);
  return slot;
}

bool Fs::exists(const char* path) const { return mounted_ && pathExists(path); }

Status Fs::remove(const char* path) {
  if (!mounted_) return Status::Io;
  char full[kMaxPath];
  if (!fullPath(path, full, sizeof(full))) return Status::Invalid;
  struct stat info;
  if (::stat(full, &info) != 0) return Status::NotFound;
  const int result = S_ISDIR(info.st_mode) ? ::rmdir(full) : ::unlink(full);
  return result == 0 ? Status::Ok : Status::Io;
}

Status Fs::rename(const char* from, const char* to) {
  if (!mounted_) return Status::Io;
  char fullFrom[kMaxPath];
  char fullTo[kMaxPath];
  if (!fullPath(from, fullFrom, sizeof(fullFrom)) || !fullPath(to, fullTo, sizeof(fullTo))) {
    return Status::Invalid;
  }
  return ::rename(fullFrom, fullTo) == 0 ? Status::Ok : Status::Io;
}

Status Fs::mkdir(const char* path) {
  if (!mounted_) return Status::Io;
  char full[kMaxPath];
  if (!fullPath(path, full, sizeof(full))) return Status::Invalid;
  struct stat info;
  if (::stat(full, &info) == 0) return S_ISDIR(info.st_mode) ? Status::Ok : Status::Io;
  makeParents(full);
  return ::mkdir(full, 0775) == 0 ? Status::Ok : Status::Io;
}

Status Fs::list(const char* dir, DirVisitor visitor) {
  if (!mounted_) return Status::Io;
  char full[kMaxPath];
  if (!visitor || !fullPath(dir, full, sizeof(full))) return Status::Invalid;
  DIR* root = ::opendir(full);
  if (!root) return Status::NotFound;

  const size_t base = strlen(full);
  for (struct dirent* entry = ::readdir(root); entry; entry = ::readdir(root)) {
    DirEntry e;
    snprintf(e.name, sizeof(e.name), "%.*s", static_cast<int>(sizeof(e.name) - 1), entry->d_name);
    e.isDir = entry->d_type == DT_DIR;
    if (!e.isDir) {
      // The size is not part of the listing: it is asked for each file.
      const int n = snprintf(full + base, sizeof(full) - base, "%s%s",
                             base > 0 && full[base - 1] == '/' ? "" : "/", entry->d_name);
      struct stat info;
      if (n > 0 && static_cast<size_t>(n) < sizeof(full) - base && ::stat(full, &info) == 0) {
        e.size = static_cast<uint32_t>(info.st_size);
      }
      full[base] = '\0';
    }
    if (!visitor(e)) break;
  }
  ::closedir(root);
  return Status::Ok;
}

// ---------------------------------------------------------------- key/value

Status Kv::begin() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // The partition cannot be read by this version: it is erased and created again.
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  ready_ = err == ESP_OK;
  return ready_ ? Status::Ok : Status::Io;
}

Status Kv::get(const char* ns, const char* key, void* out, size_t cap, size_t& len) {
  len = 0;
  if (!ready_) return Status::Io;
  if (!ns || !key || !out) return Status::Invalid;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
  if (err != ESP_OK) return fromEsp(err);

  size_t size = 0;
  err = nvs_get_blob(handle, key, nullptr, &size);
  if (err == ESP_OK) {
    len = size;
    if (size > cap) {
      err = ESP_ERR_NVS_INVALID_LENGTH;
    } else {
      err = nvs_get_blob(handle, key, out, &size);
    }
  }
  nvs_close(handle);
  return fromEsp(err);
}

Status Kv::set(const char* ns, const char* key, const void* data, size_t len) {
  if (!ready_) return Status::Io;
  if (!ns || !key || !data || len == 0) return Status::Invalid;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) return fromEsp(err);
  err = nvs_set_blob(handle, key, data, len);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return fromEsp(err);
}

Status Kv::remove(const char* ns, const char* key) {
  if (!ready_) return Status::Io;
  if (!ns || !key) return Status::Invalid;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) return fromEsp(err);
  err = nvs_erase_key(handle, key);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return fromEsp(err);
}

Status Kv::eraseNamespace(const char* ns) {
  if (!ready_) return Status::Io;
  if (!ns) return Status::Invalid;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) return fromEsp(err);
  err = nvs_erase_all(handle);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return fromEsp(err);
}

}  // namespace lmp::hal::esp32
