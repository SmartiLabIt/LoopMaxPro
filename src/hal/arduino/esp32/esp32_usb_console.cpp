#include "hal/arduino/esp32/esp32_usb_console.h"

#if LMP_CONSOLE_USB_DEVICE

#include <string.h>

#include "hal/api/memory.h"

namespace lmp::hal::esp32 {
namespace {

// Longest wait of flush(), in milliseconds.
constexpr uint32_t kFlushMs = 200;

}  // namespace

Status UsbConsole::begin(uint32_t baud) {
  // The speed means nothing on this port. On S3 and C3 this starts the unit of the core.
  ::Serial.begin(baud);
  if (!buffer_) {
    buffer_ = static_cast<uint8_t*>(hal::allocLarge(LMP_SERIAL_TX_BUFFER));
    capacity_ = buffer_ ? LMP_SERIAL_TX_BUFFER : 0;
  }
  ready_ = true;
  return Status::Ok;
}

void UsbConsole::pumpLocked() {
  while (used_) {
    // Zero when nobody has opened the port.
    const int room = ::Serial.availableForWrite();
    if (room <= 0) return;
    size_t n = capacity_ - head_;          // up to the end of the buffer
    if (n > used_) n = used_;
    if (n > static_cast<size_t>(room)) n = static_cast<size_t>(room);
    const size_t sent = ::Serial.write(&buffer_[head_], n);
    if (sent == 0) return;
    head_ = (head_ + sent) % capacity_;
    used_ -= sent;
  }
}

void UsbConsole::pump() {
  if (!ready_) return;
  LockGuard guard(mutex_);
  pumpLocked();
}

size_t UsbConsole::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data || len == 0) return 0;
  LockGuard guard(mutex_);

  // Of a text longer than the whole buffer only the end can be kept.
  size_t skip = 0;
  if (len > capacity_) skip = len - capacity_;
  const size_t take = len - skip;

  size_t room = capacity_ - used_;
  if (room < take) {
    pumpLocked();
    room = capacity_ - used_;
  }
  if (room < take) {
    const size_t drop = take - room;
    head_ = (head_ + drop) % capacity_;
    used_ -= drop;
  }

  size_t done = 0;
  while (done < take) {
    const size_t tail = (head_ + used_) % capacity_;
    size_t n = capacity_ - tail;
    if (n > take - done) n = take - done;
    memcpy(&buffer_[tail], data + skip + done, n);
    used_ += n;
    done += n;
  }
  pumpLocked();
  return len;
}

size_t UsbConsole::read(uint8_t* out, size_t cap) {
  if (!ready_ || !out) return 0;
  LockGuard guard(mutex_);
  size_t n = 0;
  while (n < cap && ::Serial.available() > 0) {
    const int c = ::Serial.read();
    if (c < 0) break;
    out[n++] = static_cast<uint8_t>(c);
  }
  return n;
}

size_t UsbConsole::available() const {
  if (!ready_) return 0;
  const int n = ::Serial.available();
  return n > 0 ? static_cast<size_t>(n) : 0;
}

void UsbConsole::flush() {
  if (!ready_) return;
  const uint32_t start = millis();
  for (;;) {
    {
      LockGuard guard(mutex_);
      pumpLocked();
      if (used_ == 0) break;
    }
    // Nobody reads, or too slowly: what is left is lost with the restart.
    if (millis() - start >= kFlushMs) return;
    delay(1);
  }
  ::Serial.flush();
}

}  // namespace lmp::hal::esp32

#endif  // LMP_CONSOLE_USB_DEVICE
