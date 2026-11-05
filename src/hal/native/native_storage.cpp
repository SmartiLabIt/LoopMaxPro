#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <system_error>

#include "hal/native/native_hal.h"

namespace fs = std::filesystem;

namespace lmp::hal::native {
namespace {

class StdFile : public IFile {
 public:
  explicit StdFile(FILE* f) : f_(f) {}

  size_t read(uint8_t* out, size_t cap) override { return fread(out, 1, cap, f_); }
  size_t write(const uint8_t* data, size_t len) override { return fwrite(data, 1, len, f_); }
  bool seek(size_t position) override {
    return fseek(f_, static_cast<long>(position), SEEK_SET) == 0;
  }
  size_t position() const override {
    const long p = ftell(f_);
    return p < 0 ? 0 : static_cast<size_t>(p);
  }
  size_t size() const override {
    const long here = ftell(f_);
    fseek(f_, 0, SEEK_END);
    const long end = ftell(f_);
    fseek(f_, here, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
  }
  void close() override {
    fclose(f_);
    delete this;
  }

 private:
  FILE* f_;
};

// Device paths always use '/': reject anything that tries to leave the root.
bool safePath(const char* path) { return path && !strstr(path, ".."); }

}  // namespace

// ---------------------------------------------------------------- filesystem

Fs::Fs(const char* rootDir) : root_(rootDir) {}

std::string Fs::resolve(const char* path) const {
  std::string full = root_;
  if (path && path[0] != '/') full += '/';
  if (path) full += path;
  return full;
}

Status Fs::mount(bool) {
  std::error_code ec;
  fs::create_directories(root_, ec);
  mounted_ = !ec;
  return mounted_ ? Status::Ok : Status::Io;
}

Status Fs::info(FsInfo& out) const {
  if (!mounted_) return Status::Io;
  std::error_code ec;
  uint64_t used = 0;
  for (auto it = fs::recursive_directory_iterator(root_, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code ec2;
    if (it->is_regular_file(ec2)) used += static_cast<uint64_t>(it->file_size(ec2));
  }
  out.totalBytes = 896u * 1024;  // same size as the 4 MB partition table
  out.usedBytes = used;
  return Status::Ok;
}

IFile* Fs::open(const char* path, OpenMode mode) {
  if (!mounted_ || !safePath(path)) return nullptr;
  const std::string full = resolve(path);
  if (mode != OpenMode::Read) {
    std::error_code ec;
    fs::create_directories(fs::path(full).parent_path(), ec);
  }
  const char* flags = mode == OpenMode::Read ? "rb" : (mode == OpenMode::Write ? "wb" : "ab");
  FILE* f = fopen(full.c_str(), flags);
  return f ? new StdFile(f) : nullptr;
}

bool Fs::exists(const char* path) const {
  if (!mounted_ || !safePath(path)) return false;
  std::error_code ec;
  return fs::exists(resolve(path), ec);
}

Status Fs::remove(const char* path) {
  if (!mounted_ || !safePath(path)) return Status::Invalid;
  std::error_code ec;
  if (!fs::remove(resolve(path), ec)) return ec ? Status::Io : Status::NotFound;
  return Status::Ok;
}

Status Fs::rename(const char* from, const char* to) {
  if (!mounted_ || !safePath(from) || !safePath(to)) return Status::Invalid;
  std::error_code ec;
  fs::rename(resolve(from), resolve(to), ec);
  return ec ? Status::Io : Status::Ok;
}

Status Fs::mkdir(const char* path) {
  if (!mounted_ || !safePath(path)) return Status::Invalid;
  std::error_code ec;
  fs::create_directories(resolve(path), ec);
  return ec ? Status::Io : Status::Ok;
}

Status Fs::list(const char* dir, DirVisitor visitor) {
  if (!mounted_ || !safePath(dir) || !visitor) return Status::Invalid;
  std::error_code ec;
  fs::directory_iterator it(resolve(dir), ec);
  if (ec) return Status::NotFound;
  for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
    DirEntry entry;
    const std::string name = it->path().filename().string();
    snprintf(entry.name, sizeof(entry.name), "%s", name.c_str());
    std::error_code ec2;
    entry.isDir = it->is_directory(ec2);
    entry.size = entry.isDir ? 0 : static_cast<uint32_t>(it->file_size(ec2));
    if (!visitor(entry)) break;
  }
  return Status::Ok;
}

void Fs::wipe() {
  std::error_code ec;
  fs::remove_all(root_, ec);
  if (mounted_) fs::create_directories(root_, ec);
}

// ---------------------------------------------------------------- key/value

Kv::Kv(const char* rootDir) : root_(rootDir) {}

std::string Kv::file(const char* ns, const char* key) const {
  return root_ + "/" + ns + "/" + key;
}

Status Kv::begin() {
  std::error_code ec;
  fs::create_directories(root_, ec);
  return ec ? Status::Io : Status::Ok;
}

Status Kv::get(const char* ns, const char* key, void* out, size_t cap, size_t& len) {
  len = 0;
  if (!safePath(ns) || !safePath(key) || !out) return Status::Invalid;
  FILE* f = fopen(file(ns, key).c_str(), "rb");
  if (!f) return Status::NotFound;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  Status result = Status::Ok;
  if (size < 0) {
    result = Status::Io;
  } else if (static_cast<size_t>(size) > cap) {
    len = static_cast<size_t>(size);
    result = Status::NoMem;
  } else {
    len = fread(out, 1, static_cast<size_t>(size), f);
    if (len != static_cast<size_t>(size)) result = Status::Io;
  }
  fclose(f);
  return result;
}

Status Kv::set(const char* ns, const char* key, const void* data, size_t len) {
  if (!safePath(ns) || !safePath(key) || (!data && len)) return Status::Invalid;
  std::error_code ec;
  fs::create_directories(root_ + "/" + ns, ec);
  if (ec) return Status::Io;
  FILE* f = fopen(file(ns, key).c_str(), "wb");
  if (!f) return Status::Io;
  const size_t written = len ? fwrite(data, 1, len, f) : 0;
  fclose(f);
  return written == len ? Status::Ok : Status::Io;
}

Status Kv::remove(const char* ns, const char* key) {
  if (!safePath(ns) || !safePath(key)) return Status::Invalid;
  std::error_code ec;
  if (!fs::remove(file(ns, key), ec)) return ec ? Status::Io : Status::NotFound;
  return Status::Ok;
}

Status Kv::eraseNamespace(const char* ns) {
  if (!safePath(ns)) return Status::Invalid;
  std::error_code ec;
  fs::remove_all(root_ + "/" + ns, ec);
  return ec ? Status::Io : Status::Ok;
}

void Kv::wipe() {
  std::error_code ec;
  fs::remove_all(root_, ec);
  fs::create_directories(root_, ec);
}

}  // namespace lmp::hal::native
