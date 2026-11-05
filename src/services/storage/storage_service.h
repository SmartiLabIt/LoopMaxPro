// LoopMaxPro - storage service: filesystem and key/value store.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

class IStorage {
 public:
  static constexpr ServiceId kId = ServiceId::Storage;
  virtual ~IStorage() = default;

  // Reads a whole text file. The buffer is NUL terminated. NoMem when it does not fit.
  virtual Status readFile(const char* path, char* out, size_t cap, size_t& len) = 0;
  // Writes to a temporary file, then renames it: the old content survives a power loss.
  virtual Status writeFile(const char* path, StringView data) = 0;
  virtual bool exists(const char* path) = 0;
  virtual Status remove(const char* path) = 0;
  // Removes every file of a folder. Sub folders are left alone.
  virtual Status removeFiles(const char* dir) = 0;
};

class StorageService : public Service, public IStorage {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  const char* reportPart() const override { return "storage"; }
  void* api() override { return static_cast<IStorage*>(this); }

  Status readFile(const char* path, char* out, size_t cap, size_t& len) override;
  Status writeFile(const char* path, StringView data) override;
  bool exists(const char* path) override;
  Status remove(const char* path) override;
  Status removeFiles(const char* dir) override;

 private:
  Kernel* kernel_ = nullptr;
  hal::IFs* fs_ = nullptr;
  bool kvOk_ = false;
};

}  // namespace lmp
