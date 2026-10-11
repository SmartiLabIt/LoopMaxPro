// LoopMaxPro HAL - chip and runtime information.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

enum class ResetReason : uint8_t {
  Unknown, PowerOn, External, Software, Panic, Watchdog, Brownout, DeepSleep
};

inline const char* resetReasonName(ResetReason r) {
  switch (r) {
    case ResetReason::PowerOn:   return "poweron";
    case ResetReason::External:  return "external";
    case ResetReason::Software:  return "software";
    case ResetReason::Panic:     return "panic";
    case ResetReason::Watchdog:  return "watchdog";
    case ResetReason::Brownout:  return "brownout";
    case ResetReason::DeepSleep: return "deepsleep";
    case ResetReason::Unknown:   break;
  }
  return "unknown";
}

// Values that never change while the firmware runs.
struct SystemInfo {
  const char* chipModel = "";
  const char* sdkVersion = "";
  uint8_t cores = 1;
  uint8_t revision = 0;
  uint16_t cpuMhz = 0;
  uint32_t flashSize = 0;
  uint32_t flashSpeedHz = 0;
};

// What the device wrote last before it restarted, kept in a memory a restart does not clear (a
// loss of power does). With it, a board that stopped tells why after it starts again.
constexpr size_t kLastWordsLines = 12;
constexpr size_t kLastWordsLine = 96;
struct LastWords {
  char why[48] = "";          // who restarted it on purpose; empty for a panic, a watchdog, a cable
  uint32_t uptimeS = 0;       // how long the previous start lasted
  uint8_t count = 0;
  char lines[kLastWordsLines][kLastWordsLine] = {};   // the last entries of the log, oldest first
};

class ISystem {
 public:
  virtual ~ISystem() = default;

  virtual const SystemInfo& info() const = 0;
  virtual uint32_t heapFree() const = 0;
  virtual uint32_t heapMin() const = 0;    // lowest free heap since boot
  virtual uint32_t heapTotal() const = 0;
  // Largest block that can be taken in one piece: a secure connection needs large ones.
  virtual uint32_t heapLargest() const { return heapFree(); }
  // Lowest free stack of the calling context since boot, in bytes. 0 when not known.
  virtual uint32_t stackMin() const { return 0; }
  virtual ResetReason resetReason() const = 0;
  // Which one, when the reason groups several: "int", "task" or "other" for a watchdog.
  // Empty when there is nothing to add.
  virtual const char* resetDetail() const { return ""; }
  virtual void mac(Mac& out) const = 0;    // factory MAC, used for unique names

  // A few bytes burnt in the chip for ever (eFuse): no erase of the flash, no tool reaches
  // them. They are written once, all together: the chips keep them with error correction, and a
  // second write would break it. Read: false where the chip has no such place; all zero when
  // nothing was written. Write: Unsupported where there is none, Denied when it was written.
  static constexpr size_t kChipNoteMax = 16;
  virtual bool chipNoteRead(uint8_t* out, size_t len) const {
    (void)out;
    (void)len;
    return false;
  }
  virtual Status chipNoteWrite(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return Status::Unsupported;
  }

  // How the memory of the board is laid out: the first 16 hex digits of the SHA-256 of the
  // partition table, as it is in the flash. Empty on boards without one.
  virtual const char* layout() const { return ""; }
  // What the program is built on: "idf" and the major version of ESP-IDF. Empty elsewhere.
  // With layout it tells the server whether an update of the program alone fits the board.
  virtual const char* base() const { return ""; }

  virtual void watchdogFeed() = 0;
  virtual void restart() = 0;              // does not return on real hardware

  // The last words (LastWords). Keep a line, any task; say why before a restart on purpose;
  // what the previous start left, null when nothing. Boards without such a memory keep nothing.
  virtual void lastWordsKeep(const char* line) { (void)line; }
  virtual void lastWordsWhy(const char* why) { (void)why; }
  virtual const LastWords* lastWordsPrevious() const { return nullptr; }
  // The main task must feed the watchdog within these seconds once it has started to, or the
  // board restarts, the reason in the last words. 0: off. Boards without it do nothing.
  virtual void stallGuard(uint32_t seconds) { (void)seconds; }
};

}  // namespace lmp::hal
