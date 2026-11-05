// LoopMaxPro HAL - filesystem, key/value store, firmware update.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

enum class OpenMode : uint8_t { Read, Write, Append };  // Write truncates or creates

class IFile {
 public:
  virtual ~IFile() = default;
  virtual size_t read(uint8_t* out, size_t cap) = 0;
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  virtual bool seek(size_t position) = 0;
  virtual size_t position() const = 0;
  virtual size_t size() const = 0;
  // Releases the handle. The pointer must not be used afterwards.
  virtual void close() = 0;
};

struct FsInfo {
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
};

struct DirEntry {
  char name[64] = {0};
  uint32_t size = 0;
  bool isDir = false;
};

// Return false from the visitor to stop the listing.
using DirVisitor = Delegate<bool(const DirEntry&)>;

class IFs {
 public:
  virtual ~IFs() = default;

  virtual Status mount(bool formatOnFail) = 0;
  virtual void unmount() = 0;
  virtual bool mounted() const = 0;
  virtual Status info(FsInfo& out) const = 0;
  // True when a file written over an existing one keeps the old content until it is closed:
  // a power loss in the middle leaves the old file whole.
  virtual bool replacesWhole() const { return false; }

  // Handles come from a small fixed pool. Returns null when the file cannot be opened
  // or no handle is free. Always call close().
  virtual IFile* open(const char* path, OpenMode mode) = 0;
  virtual bool exists(const char* path) const = 0;
  virtual Status remove(const char* path) = 0;
  virtual Status rename(const char* from, const char* to) = 0;
  virtual Status mkdir(const char* path) = 0;
  virtual Status list(const char* dir, DirVisitor visitor) = 0;
};

// Small values addressed by namespace and key. NVS on ESP32, a file elsewhere.
class IKv {
 public:
  virtual ~IKv() = default;

  virtual Status begin() = 0;
  // len receives the stored size. Status::NoMem when the buffer is too small.
  virtual Status get(const char* ns, const char* key, void* out, size_t cap, size_t& len) = 0;
  virtual Status set(const char* ns, const char* key, const void* data, size_t len) = 0;
  virtual Status remove(const char* ns, const char* key) = 0;
  virtual Status eraseNamespace(const char* ns) = 0;
  virtual Status commit() = 0;
};

enum class OtaTarget : uint8_t { App, Filesystem };

class IOta {
 public:
  virtual ~IOta() = default;

  virtual Status begin(OtaTarget target, size_t totalSize) = 0;
  virtual Status write(const uint8_t* data, size_t len) = 0;
  // Verifies the image and selects it for the next boot.
  virtual Status end() = 0;
  virtual void abort() = 0;
  virtual bool inProgress() const = 0;
  virtual size_t written() const = 0;

  // After a reboot into a new image: confirm it, or go back to the previous one.
  virtual bool pendingVerify() const = 0;
  virtual Status markValid() = 0;
  virtual bool canRollback() const = 0;
  virtual Status rollback() = 0;
};

}  // namespace lmp::hal
