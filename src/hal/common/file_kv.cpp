#include "hal/common/file_kv.h"

#include <stdio.h>
#include <string.h>

namespace lmp::hal {
namespace {

// File: the mark, then one record after the other.
// Record: length of the key, key, length of the value on two bytes, value.
constexpr uint8_t kMark[4] = {'L', 'K', 'V', '1'};
constexpr size_t kMaxValue = 0xFFFF;

bool validName(const char* name) {
  if (!name) return false;
  const size_t len = strlen(name);
  if (len == 0 || len > FileKv::kMaxName) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

bool FileKv::path(char* out, size_t cap, const char* ns, const char* extension) const {
  const int n = snprintf(out, cap, "%s/%s.%s", dir_, ns, extension);
  return n > 0 && static_cast<size_t>(n) < cap;
}

Status FileKv::begin() {
  ready_ = false;
  ns_[0] = '\0';
  used_ = 0;
  dirty_ = false;
  if (!fs_.mounted()) {
    const Status s = fs_.mount(true);
    if (s != Status::Ok) return s;
  }
  const Status made = fs_.mkdir(dir_);
  if (made != Status::Ok) return made;
  ready_ = true;
  return Status::Ok;
}

bool FileKv::usable() {
  if (!ready_) return false;
  return fs_.mounted() || fs_.mount(false) == Status::Ok;
}

size_t FileKv::recordSize(size_t offset) const {
  const size_t keyLen = data_[offset];
  const size_t valueLen = data_[offset + 1 + keyLen] | (data_[offset + 2 + keyLen] << 8);
  return 1 + keyLen + 2 + valueLen;
}

int FileKv::find(const char* key) const {
  const size_t keyLen = strlen(key);
  for (size_t offset = 0; offset < used_; offset += recordSize(offset)) {
    if (data_[offset] == keyLen && memcmp(&data_[offset + 1], key, keyLen) == 0) {
      return static_cast<int>(offset);
    }
  }
  return -1;
}

void FileKv::erase(size_t offset) {
  const size_t size = recordSize(offset);
  memmove(&data_[offset], &data_[offset + size], used_ - offset - size);
  used_ -= size;
}

Status FileKv::load(const char* ns) {
  used_ = 0;
  char name[48];
  if (!path(name, sizeof(name), ns, "bin")) return Status::Invalid;

  if (!fs_.exists(name)) {
    // A write interrupted between the two steps left the new content under the other name.
    char pending[48];
    if (!path(pending, sizeof(pending), ns, "tmp") || !fs_.exists(pending)) return Status::Ok;
    if (fs_.rename(pending, name) != Status::Ok) return Status::Ok;
  }

  IFile* f = fs_.open(name, OpenMode::Read);
  if (!f) return Status::Ok;
  uint8_t mark[sizeof(kMark)];
  const size_t size = f->size();
  bool good = size >= sizeof(kMark) && size - sizeof(kMark) <= kCapacity &&
              f->read(mark, sizeof(mark)) == sizeof(mark) &&
              memcmp(mark, kMark, sizeof(kMark)) == 0;
  if (good) {
    used_ = size - sizeof(kMark);
    good = used_ == 0 || f->read(data_, used_) == used_;
  }
  f->close();

  // Every record must end inside the content: a damaged file counts as empty.
  size_t offset = 0;
  while (good && offset < used_) {
    const size_t keyLen = data_[offset];
    if (keyLen == 0 || keyLen > kMaxName || offset + 1 + keyLen + 2 > used_) {
      good = false;
      break;
    }
    const size_t size1 = recordSize(offset);
    if (offset + size1 > used_) {
      good = false;
      break;
    }
    offset += size1;
  }
  if (!good) used_ = 0;
  return Status::Ok;
}

Status FileKv::select(const char* ns) {
  if (!usable()) return Status::Io;
  if (!validName(ns)) return Status::Invalid;
  if (strcmp(ns_, ns) == 0) return Status::Ok;
  LMP_TRY(commit());
  LMP_TRY(load(ns));
  snprintf(ns_, sizeof(ns_), "%s", ns);
  return Status::Ok;
}

Status FileKv::get(const char* ns, const char* key, void* out, size_t cap, size_t& len) {
  len = 0;
  if (!out || !validName(key)) return Status::Invalid;
  LMP_TRY(select(ns));
  const int offset = find(key);
  if (offset < 0) return Status::NotFound;
  const size_t keyLen = data_[offset];
  const size_t valueLen = data_[offset + 1 + keyLen] | (data_[offset + 2 + keyLen] << 8);
  len = valueLen;
  if (valueLen > cap) return Status::NoMem;
  memcpy(out, &data_[offset + 1 + keyLen + 2], valueLen);
  return Status::Ok;
}

Status FileKv::set(const char* ns, const char* key, const void* data, size_t len) {
  if (!data || len == 0 || len > kMaxValue || !validName(key)) return Status::Invalid;
  LMP_TRY(select(ns));
  const size_t keyLen = strlen(key);
  const size_t needed = 1 + keyLen + 2 + len;

  const int old = find(key);
  const size_t freed = old < 0 ? 0 : recordSize(static_cast<size_t>(old));
  if (used_ - freed + needed > kCapacity) return Status::NoMem;
  if (old >= 0) erase(static_cast<size_t>(old));

  uint8_t* record = &data_[used_];
  record[0] = static_cast<uint8_t>(keyLen);
  memcpy(&record[1], key, keyLen);
  record[1 + keyLen] = static_cast<uint8_t>(len & 0xFF);
  record[2 + keyLen] = static_cast<uint8_t>(len >> 8);
  memcpy(&record[3 + keyLen], data, len);
  used_ += needed;
  dirty_ = true;
  return Status::Ok;
}

Status FileKv::remove(const char* ns, const char* key) {
  if (!validName(key)) return Status::Invalid;
  LMP_TRY(select(ns));
  const int offset = find(key);
  if (offset < 0) return Status::NotFound;
  erase(static_cast<size_t>(offset));
  dirty_ = true;
  return Status::Ok;
}

Status FileKv::eraseNamespace(const char* ns) {
  if (!usable()) return Status::Io;
  if (!validName(ns)) return Status::Invalid;
  if (strcmp(ns_, ns) == 0) {
    used_ = 0;
    dirty_ = false;
  }
  char name[48];
  if (!path(name, sizeof(name), ns, "tmp")) return Status::Invalid;
  if (fs_.exists(name)) fs_.remove(name);
  if (!path(name, sizeof(name), ns, "bin")) return Status::Invalid;
  if (!fs_.exists(name)) return Status::Ok;
  return fs_.remove(name);
}

Status FileKv::commit() {
  if (!ready_) return Status::Io;
  if (!dirty_ || !ns_[0]) return Status::Ok;
  if (!usable()) return Status::Io;

  char name[48];
  char pending[48];
  if (!path(name, sizeof(name), ns_, "bin") || !path(pending, sizeof(pending), ns_, "tmp")) {
    return Status::Invalid;
  }

  // Where the filesystem keeps the old content until the new one is complete, one write is
  // enough. Elsewhere the new content goes to a second file, which then takes the place of
  // the first: a power loss in the middle leaves one of the two whole.
  const bool direct = fs_.replacesWhole();
  IFile* f = fs_.open(direct ? name : pending, OpenMode::Write);
  if (!f) return Status::Io;
  size_t written = f->write(kMark, sizeof(kMark));
  if (used_) written += f->write(data_, used_);
  f->close();
  if (written != sizeof(kMark) + used_) {
    if (!direct) fs_.remove(pending);
    return Status::Io;
  }
  if (direct) {
    dirty_ = false;
    return Status::Ok;
  }

  Status renamed = fs_.rename(pending, name);
  if (renamed != Status::Ok && fs_.exists(name)) {
    // This filesystem does not replace an existing file.
    fs_.remove(name);
    renamed = fs_.rename(pending, name);
  }
  if (renamed != Status::Ok) {
    fs_.remove(pending);
    return renamed;
  }
  dirty_ = false;
  return Status::Ok;
}

}  // namespace lmp::hal
