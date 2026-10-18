// LoopMaxPro - clock service: wall clock and time zone.
// The time comes from who has it: a command, for example from the browser in AP mode, or
// a server on the network as soon as the device is connected. Nothing here waits.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

enum class TimeSource : uint8_t { None, Client, Ntp, Cloud };
const char* timeSourceName(TimeSource source);

class IClockService {
 public:
  static constexpr ServiceId kId = ServiceId::Clock;
  virtual ~IClockService() = default;

  virtual bool synced() const = 0;
  virtual int64_t unixTime() const = 0;           // 0 when the time is not known
  virtual bool localTime(hal::DateTime& out) const = 0;
  // A moment of the past or of the future, in the time zone of the device, with its own
  // daylight saving. False while the time is not known.
  virtual bool localTimeOf(int64_t unixSeconds, hal::DateTime& out) const = 0;
  virtual Status setTime(int64_t unixSeconds, TimeSource source) = 0;
  // tz is the name shown to the user, posix is what the C library needs.
  virtual Status setTimezone(const char* tz, const char* posix) = 0;
};

class ClockService : public Service, public IClockService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "clock"; }
  void* api() override { return static_cast<IClockService*>(this); }

  bool synced() const override { return source_ != TimeSource::None; }
  int64_t unixTime() const override;
  bool localTime(hal::DateTime& out) const override;
  bool localTimeOf(int64_t unixSeconds, hal::DateTime& out) const override;
  Status setTime(int64_t unixSeconds, TimeSource source) override;
  Status setTimezone(const char* tz, const char* posix) override;

 private:
  // Asks the time to the server of the settings. Denied without a network.
  Status askNetwork();
  void accept(TimeSource source, int64_t unixSeconds);

  Kernel* kernel_ = nullptr;
  hal::IClock* clock_ = nullptr;
  hal::INtp* ntp_ = nullptr;
  LogSource src_ = kLogSys;
  TimeSource source_ = TimeSource::None;
  int64_t syncedAt_ = 0;
  uint32_t answersSeen_ = 0;
  uint32_t askRef_ = 0;        // reference of the command waiting for an answer
  uint32_t askedAtMs_ = 0;
  bool online_ = false;        // the device is on a network
  bool asking_ = false;
  FixedString<48> server_;     // what the driver was given: it keeps the pointer
};

}  // namespace lmp
