#include "services/system/system_service.h"

#include "hal/api/memory.h"

namespace lmp {
namespace {

// Where the starts are kept: a few bytes, the same on every board. Format, how many starts,
// how many notes, then for each note the reason, the hour and the duration.
constexpr const char* kStartsNs = "boot";
constexpr const char* kStartsKey = "starts";
constexpr uint8_t kStartsFormat = 1;
constexpr size_t kStartsHead = 6;
constexpr size_t kStartBytes = 9;

void put32(uint8_t* at, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) at[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get32(const uint8_t* at) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(at[i]) << (8 * i);
  return value;
}

}  // namespace

const ServiceInfo& SystemService::info() const {
  static const ServiceInfo kInfo{ServiceId::System, "system", "\xF0\x9F\x9A\x80", Mode::Any,
                                 RestartPolicy::Critical, 0, DepList()};
  return kInfo;
}

Status SystemService::start(Kernel& kernel) {
  kernel_ = &kernel;
  return kernel.board().sys ? Status::Ok : Status::Unsupported;
}

void SystemService::onEvent(const Event& event) {
  if (!kernel_) return;
  if (event.id == EventId::ServiceState) {
    // The store of the small values comes up with the storage service, after this one.
    if (!counted_ && event.a == static_cast<uint8_t>(ServiceId::Storage) &&
        event.b >= static_cast<uint8_t>(ServiceState::Ready)) {
      countStart();
    }
  } else if (event.id == EventId::TimeSynced) {
    // The hour of this start, once: the hour of now less the time the board has been on.
    const uint32_t onS = static_cast<uint32_t>(kernel_->board().clock->uptimeMs() / 1000);
    if (counted_ && kept_ && last_[0].at == 0 && event.value > onS) {
      last_[0].at = event.value - onS;
      saveStarts();
    }
  }
}

void SystemService::countStart() {
  const hal::Board& board = kernel_->board();
  if (!board.kv) return;
  counted_ = true;
  starts_ = 0;
  kept_ = 0;
  uint8_t raw[kStartsHead + kStartsKept * kStartBytes];
  size_t len = 0;
  if (board.kv->get(kStartsNs, kStartsKey, raw, sizeof(raw), len) == Status::Ok &&
      len >= kStartsHead && raw[0] == kStartsFormat) {
    starts_ = get32(raw + 1);
    size_t notes = raw[5];
    if (notes > kStartsKept) notes = kStartsKept;
    if (notes > (len - kStartsHead) / kStartBytes) notes = (len - kStartsHead) / kStartBytes;
    for (size_t i = 0; i < notes; ++i) {
      const uint8_t* note = raw + kStartsHead + i * kStartBytes;
      last_[i].reason = note[0];
      last_[i].at = get32(note + 1);
      last_[i].ranS = get32(note + 5);
    }
    kept_ = static_cast<uint8_t>(notes);
  }
  // This start goes first; the oldest note leaves.
  for (uint8_t i = kStartsKept - 1; i > 0; --i) last_[i] = last_[i - 1];
  last_[0] = StartNote();
  last_[0].reason = static_cast<uint8_t>(board.sys->resetReason());
  if (kept_ < kStartsKept) ++kept_;
  ++starts_;
  // How long the start before lasted: known when the board kept its last words, that is when
  // it did not lose power.
  const hal::LastWords* words = board.sys->lastWordsPrevious();
  if (words && kept_ > 1) last_[1].ranS = words->uptimeS;
  saveStarts();
}

void SystemService::saveStarts() {
  hal::IKv* kv = kernel_->board().kv;
  if (!kv) return;
  uint8_t raw[kStartsHead + kStartsKept * kStartBytes];
  raw[0] = kStartsFormat;
  put32(raw + 1, starts_);
  raw[5] = kept_;
  for (uint8_t i = 0; i < kept_; ++i) {
    uint8_t* note = raw + kStartsHead + i * kStartBytes;
    note[0] = last_[i].reason;
    put32(note + 1, last_[i].at);
    put32(note + 5, last_[i].ranS);
  }
  Status s = kv->set(kStartsNs, kStartsKey, raw, kStartsHead + kept_ * kStartBytes);
  if (s == Status::Ok) s = kv->commit();
  if (s != Status::Ok) LMP_LOGW(kLogSys, "the starts of the board were not saved: %s", statusName(s));
}

void SystemService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const hal::Board& board = kernel_->board();
  const hal::SystemInfo& chip = board.sys->info();
  w.str("chip", chip.chipModel);
  w.str("edition", LMP_LIGHT ? "light" : "full");
  w.u32("cores", chip.cores);
  w.u32("revision", chip.revision);
  w.u32("cpuMhz", chip.cpuMhz);
  w.u32("flash", chip.flashSize);
  w.u32("flashHz", chip.flashSpeedHz);
  w.str("sdk", chip.sdkVersion);
  w.beginObject("heap");
  w.u32("free", board.sys->heapFree());
  w.u32("min", board.sys->heapMin());
  w.u32("total", board.sys->heapTotal());
  w.u32("largest", board.sys->heapLargest());
  w.endObject();
  // The external RAM, where the board has it: the large buffers of the system are there.
  const hal::ExternalRam psram = hal::externalRam();
  if (psram.total) {
    w.beginObject("psram");
    w.u32("free", static_cast<uint32_t>(psram.free));
    w.u32("total", static_cast<uint32_t>(psram.total));
    w.endObject();
  }
  const uint32_t stack = board.sys->stackMin();
  if (stack) w.u32("stackMin", stack);
  w.u64("uptime", board.clock->uptimeMs() / 1000);
  w.str("resetReason", hal::resetReasonName(board.sys->resetReason()));
  // How many times the board started, and the last starts, the newest first: why, when (Unix
  // time, once the clock had the hour) and how long (seconds, where the board could tell).
  if (counted_) {
    w.u32("starts", starts_);
    w.beginArray("lastStarts");
    for (uint8_t i = 0; i < kept_; ++i) {
      w.beginObject();
      w.str("why", hal::resetReasonName(static_cast<hal::ResetReason>(last_[i].reason)));
      if (last_[i].at) w.u32("at", last_[i].at);
      if (last_[i].ranS) w.u32("ran", last_[i].ranS);
      w.endObject();
    }
    w.endArray();
  }
  w.str("mode", modeName(kernel_->mode()));
#ifdef LMP_DEBUG
  w.boolean("debug", true);
#else
  w.boolean("debug", false);
#endif
}

void SystemService::commands(CommandSet& set) {
  set.add("restart", LMP_HELP("Restarts the device"),
          [this](const Args&, CmdContext&) -> Status {
            if (!kernel_) return Status::Error;
            kernel_->requestRestart("command");
            return Status::Ok;
          },
          kCmdConfirm);
  set.add("factoryReset", LMP_HELP("Back to the factory settings. The device key is kept"),
          [this](const Args&, CmdContext&) -> Status {
            if (!kernel_) return Status::Error;
            kernel_->requestRestart("command", true);
            return Status::Ok;
          },
          kCmdConfirm);
}

}  // namespace lmp
