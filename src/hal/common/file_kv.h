// LoopMaxPro HAL - key/value store kept in files, for the boards without one of their own.
// One file for each namespace. The namespace in use stays in memory: reading its values
// costs nothing and a group of changes is written once, by commit().
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "hal/api/istorage.h"

namespace lmp::hal {

class FileKv : public IKv {
 public:
  // Room for the keys and the values of one namespace, in bytes.
  static constexpr size_t kCapacity = 1024;
  static constexpr size_t kMaxName = 15;   // the same limit of the NVS of the ESP32

  // dir must be a text that stays valid.
  explicit FileKv(IFs& fs, const char* dir = "/kv") : fs_(fs), dir_(dir) {}

  // Mounts the filesystem when nobody did it yet.
  Status begin() override;
  Status get(const char* ns, const char* key, void* out, size_t cap, size_t& len) override;
  // The change stays in memory until commit(), or until another namespace is used.
  Status set(const char* ns, const char* key, const void* data, size_t len) override;
  Status remove(const char* ns, const char* key) override;
  Status eraseNamespace(const char* ns) override;
  Status commit() override;

 private:
  // The store works whoever mounted the filesystem, and also after somebody unmounted it:
  // the settings are written when the system shuts down too.
  bool usable();
  // Makes ns the namespace in memory. What the previous one had pending is written first.
  Status select(const char* ns);
  Status load(const char* ns);
  // Offset of the record of a key, or -1.
  int find(const char* key) const;
  size_t recordSize(size_t offset) const;
  void erase(size_t offset);
  bool path(char* out, size_t cap, const char* ns, const char* extension) const;

  IFs& fs_;
  const char* dir_;
  char ns_[kMaxName + 1] = {0};
  uint8_t data_[kCapacity];
  size_t used_ = 0;
  bool ready_ = false;
  bool dirty_ = false;
};

}  // namespace lmp::hal
