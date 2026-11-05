// LoopMaxPro HAL - ESP8266: console, filesystem, PWM, analog input.
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include "hal/arduino/common/arduino_hal.h"
#include "hal/arduino/esp8266/esp8266_hal.h"

namespace lmp::hal::esp8266 {

// ---------------------------------------------------------------- console

Status Serial0::begin(uint32_t baud) {
  ::Serial.begin(baud);
  head_ = used_ = 0;
  ready_ = true;
  return Status::Ok;
}

void Serial0::pump() {
  while (used_) {
    // The core counts 128 places but stops writing at 127: with one place left it reports
    // room and then waits for a byte to leave. That place is never used here.
    const int room = ::Serial.availableForWrite() - 1;
    if (room <= 0) return;
    size_t n = sizeof(buffer_) - head_;          // up to the end of the buffer
    if (n > used_) n = used_;
    if (n > static_cast<size_t>(room)) n = static_cast<size_t>(room);
    ::Serial.write(&buffer_[head_], n);
    head_ = (head_ + n) % sizeof(buffer_);
    used_ -= n;
  }
}

size_t Serial0::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data) return 0;
  size_t done = 0;
  while (done < len) {
    if (used_ == sizeof(buffer_)) {
      // No room left: the text is longer than the buffer and the rest has to wait.
      pump();
      if (used_ == sizeof(buffer_)) delay(1);
      continue;
    }
    const size_t tail = (head_ + used_) % sizeof(buffer_);
    size_t n = sizeof(buffer_) - used_;
    if (n > sizeof(buffer_) - tail) n = sizeof(buffer_) - tail;
    if (n > len - done) n = len - done;
    memcpy(&buffer_[tail], data + done, n);
    used_ += n;
    done += n;
  }
  pump();
  return len;
}

size_t Serial0::read(uint8_t* out, size_t cap) {
  if (!ready_ || !out) return 0;
  pump();
  size_t n = 0;
  while (n < cap && ::Serial.available() > 0) {
    const int c = ::Serial.read();
    if (c < 0) break;
    out[n++] = static_cast<uint8_t>(c);
  }
  return n;
}

size_t Serial0::available() const {
  if (!ready_) return 0;
  const int n = ::Serial.available();
  return n > 0 ? static_cast<size_t>(n) : 0;
}

void Serial0::flush() {
  if (!ready_) return;
  while (used_) {
    pump();
    if (used_) delay(1);
  }
  ::Serial.flush();
}

// ---------------------------------------------------------------- files

namespace {

class File : public IFile {
 public:
  bool inUse() const { return used_; }
  void attach(const fs::File& f) {
    file_ = f;
    used_ = true;
  }

  size_t read(uint8_t* out, size_t cap) override { return file_.read(out, cap); }
  size_t write(const uint8_t* data, size_t len) override { return file_.write(data, len); }
  bool seek(size_t position) override { return file_.seek(position, SeekSet); }
  size_t position() const override { return const_cast<fs::File&>(file_).position(); }
  size_t size() const override { return const_cast<fs::File&>(file_).size(); }
  void close() override {
    file_.close();
    used_ = false;
  }

 private:
  fs::File file_;
  bool used_ = false;
};

constexpr size_t kMaxOpenFiles = 3;
File g_files[kMaxOpenFiles];

}  // namespace

Status Fs::mount(bool formatOnFail) {
  if (mounted_) return Status::Ok;
  LittleFSConfig config;
  config.setAutoFormat(formatOnFail);
  LittleFS.setConfig(config);
  mounted_ = LittleFS.begin();
  return mounted_ ? Status::Ok : Status::Io;
}

void Fs::unmount() {
  if (!mounted_) return;
  LittleFS.end();
  mounted_ = false;
}

Status Fs::info(FsInfo& out) const {
  if (!mounted_) return Status::Io;
  FSInfo space;
  if (!LittleFS.info(space)) return Status::Io;
  out.totalBytes = space.totalBytes;
  out.usedBytes = space.usedBytes;
  return Status::Ok;
}

IFile* Fs::open(const char* path, OpenMode mode) {
  if (!mounted_ || !path) return nullptr;
  File* slot = nullptr;
  for (File& f : g_files) {
    if (!f.inUse()) {
      slot = &f;
      break;
    }
  }
  if (!slot) return nullptr;

  if (mode == OpenMode::Read && !LittleFS.exists(path)) return nullptr;
  const char* flags = mode == OpenMode::Read ? "r" : (mode == OpenMode::Write ? "w" : "a");
  // The missing folders are created by the library when writing.
  fs::File f = LittleFS.open(path, flags);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return nullptr;
  }
  slot->attach(f);
  return slot;
}

bool Fs::exists(const char* path) const { return mounted_ && path && LittleFS.exists(path); }

Status Fs::remove(const char* path) {
  if (!mounted_) return Status::Io;
  if (!path) return Status::Invalid;
  if (!LittleFS.exists(path)) return Status::NotFound;
  // Files and empty folders go away with two different calls.
  if (LittleFS.remove(path) || LittleFS.rmdir(path)) return Status::Ok;
  return Status::Io;
}

Status Fs::rename(const char* from, const char* to) {
  if (!mounted_) return Status::Io;
  if (!from || !to) return Status::Invalid;
  return LittleFS.rename(from, to) ? Status::Ok : Status::Io;
}

Status Fs::mkdir(const char* path) {
  if (!mounted_) return Status::Io;
  if (!path) return Status::Invalid;
  if (LittleFS.exists(path)) return Status::Ok;
  return LittleFS.mkdir(path) ? Status::Ok : Status::Io;
}

Status Fs::list(const char* dir, DirVisitor visitor) {
  if (!mounted_) return Status::Io;
  if (!dir || !visitor) return Status::Invalid;
  if (!LittleFS.exists(dir)) return Status::NotFound;
  fs::Dir entries = LittleFS.openDir(dir);
  while (entries.next()) {
    DirEntry e;
    snprintf(e.name, sizeof(e.name), "%s", entries.fileName().c_str());
    e.isDir = entries.isDirectory();
    e.size = e.isDir ? 0 : static_cast<uint32_t>(entries.fileSize());
    if (!visitor(e)) break;
  }
  return Status::Ok;
}

// ---------------------------------------------------------------- PWM

bool Pwm::supports(uint8_t pin) const { return arduino::pinCanOutput(pin); }

Status Pwm::setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) {
  // Limits of the core: from 100 Hz to 40 kHz, from 4 to 16 bits.
  if (!supports(pin) || frequencyHz < 100 || frequencyHz > 40000 || resolutionBits < 4 ||
      resolutionBits > 16) {
    return Status::Invalid;
  }
  const uint32_t others = pins_ & ~(1u << pin);
  if (others && (frequencyHz != frequencyHz_ || resolutionBits != resolutionBits_)) {
    return Status::Busy;
  }
  frequencyHz_ = frequencyHz;
  resolutionBits_ = resolutionBits;
  analogWriteFreq(frequencyHz);
  analogWriteResolution(resolutionBits);
  pins_ |= 1u << pin;
  analogWrite(pin, 0);
  return Status::Ok;
}

Status Pwm::write(uint8_t pin, uint32_t duty) {
  if (pin > 16 || !(pins_ & (1u << pin))) return Status::NotFound;
  const uint32_t top = (1u << resolutionBits_) - 1;
  analogWrite(pin, static_cast<int>(duty > top ? top : duty));
  return Status::Ok;
}

Status Pwm::release(uint8_t pin) {
  if (pin > 16 || !(pins_ & (1u << pin))) return Status::NotFound;
  analogWrite(pin, 0);
  pinMode(pin, INPUT);
  pins_ &= ~(1u << pin);
  return Status::Ok;
}

// ---------------------------------------------------------------- ADC

bool Adc::supports(uint8_t pin) const { return pin == A0; }

Status Adc::readRaw(uint8_t pin, uint32_t& value) {
  if (!supports(pin)) return Status::Invalid;
  value = static_cast<uint32_t>(analogRead(A0));
  return Status::Ok;
}

Status Adc::readMillivolts(uint8_t pin, uint32_t& millivolts) {
  uint32_t raw = 0;
  LMP_TRY(readRaw(pin, raw));
  millivolts = raw * 1000 / 1023;
  return Status::Ok;
}

}  // namespace lmp::hal::esp8266
