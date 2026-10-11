#include "services/storage/storage_service.h"

#include <stdio.h>
#include <string.h>

namespace lmp {
namespace {

constexpr const char* kModuleConfigDir = "/cfg/m";

}  // namespace

const ServiceInfo& StorageService::info() const {
  static const ServiceInfo kInfo{ServiceId::Storage, "storage", "\xF0\x9F\x92\xBE", Mode::Any,
                                 RestartPolicy::Critical, 0, DepList(ServiceId::Log)};
  return kInfo;
}

Status StorageService::start(Kernel& kernel) {
  kernel_ = &kernel;
  const hal::Board& board = kernel.board();
  const LogSource src = kernel.log().addSource(info().name, info().icon);

  if (board.kv) {
    const Status s = board.kv->begin();
    kvOk_ = s == Status::Ok;
    if (!kvOk_) LMP_LOGE(src, "key/value store not available: %s", statusName(s));
  }

  fs_ = board.fs;
  if (!fs_) {
    fail("no filesystem on this board");
    return Status::Unsupported;
  }
  // A filesystem that cannot be mounted is formatted: the console is lost, the device works.
  const Status mounted = fs_->mount(true);
  if (mounted != Status::Ok) {
    fs_ = nullptr;
    fail("filesystem mount failed");
    return mounted;
  }

  hal::FsInfo space;
  if (fs_->info(space) == Status::Ok) {
    LMP_LOGI(src, "filesystem %lu KB, used %lu KB",
             static_cast<unsigned long>(space.totalBytes / 1024),
             static_cast<unsigned long>(space.usedBytes / 1024));
  }
  if (!kvOk_) degrade("key/value store not available");
  return Status::Ok;
}

void StorageService::stop() {
  if (fs_) fs_->unmount();
  fs_ = nullptr;
}

void StorageService::onEvent(const Event& event) {
  // Factory reset: the settings of the modules go away with the system ones.
  if (event.id == EventId::ResetRequested && event.a == 1) removeFiles(kModuleConfigDir);
}

void StorageService::describe(JsonWriter& w) const {
  w.beginObject("fs");
  w.boolean("mounted", fs_ && fs_->mounted());
  hal::FsInfo space;
  if (fs_ && fs_->info(space) == Status::Ok) {
    w.u64("total", space.totalBytes);
    w.u64("used", space.usedBytes);
  }
  w.endObject();
  w.beginObject("kv");
  w.boolean("ok", kvOk_);
  w.endObject();
}

// ---------------------------------------------------------------- files

Status StorageService::readFile(const char* path, char* out, size_t cap, size_t& len) {
  len = 0;
  if (!fs_) return Status::Io;
  if (!path || !out || cap == 0) return Status::Invalid;
  out[0] = '\0';
  hal::IFile* f = fs_->open(path, hal::OpenMode::Read);
  if (!f) return Status::NotFound;
  const size_t size = f->size();
  Status result = Status::Ok;
  if (size >= cap) {
    len = size;
    result = Status::NoMem;
  } else {
    len = f->read(reinterpret_cast<uint8_t*>(out), size);
    out[len] = '\0';
    if (len != size) result = Status::Io;
  }
  f->close();
  return result;
}

Status StorageService::writeFile(const char* path, StringView data) {
  if (!fs_) return Status::Io;
  if (!path) return Status::Invalid;
  char tmp[96];
  const int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(tmp)) return Status::Invalid;

  hal::IFile* f = fs_->open(tmp, hal::OpenMode::Write);
  if (!f) return Status::Io;
  const size_t written = data.size ? f->write(reinterpret_cast<const uint8_t*>(data.data), data.size)
                                   : 0;
  f->close();
  if (written != data.size) {
    fs_->remove(tmp);
    return Status::Io;
  }
  if (fs_->exists(path)) fs_->remove(path);
  const Status renamed = fs_->rename(tmp, path);
  if (renamed != Status::Ok) fs_->remove(tmp);
  return renamed;
}

bool StorageService::exists(const char* path) { return fs_ && path && fs_->exists(path); }

Status StorageService::remove(const char* path) {
  if (!fs_) return Status::Io;
  return fs_->remove(path);
}

Status StorageService::removeFiles(const char* dir) {
  if (!fs_) return Status::Io;
  if (!dir) return Status::Invalid;
  // Removing while listing confuses some filesystems: take one name at a time.
  for (int guard = 0; guard < 256; ++guard) {
    char name[64] = {0};
    const Status listed = fs_->list(dir, [&name](const hal::DirEntry& e) -> bool {
      if (e.isDir) return true;
      snprintf(name, sizeof(name), "%s", e.name);
      return false;
    });
    if (listed == Status::NotFound) return Status::Ok;
    if (listed != Status::Ok) return listed;
    if (!name[0]) return Status::Ok;
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    LMP_TRY(fs_->remove(path));
  }
  return Status::Error;
}

}  // namespace lmp
