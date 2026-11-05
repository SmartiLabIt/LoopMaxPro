// LoopMaxPro SDK - schedules for a bank of pins.
// A schedule turns a channel on for a span of time: every week on the chosen days, or once.
// It needs the time of the clock, not Internet: a device that knows what time it is follows
// its schedules also on a network that goes nowhere.
//
// A schedule acts when its span begins and when it ends. In between it holds the channel:
// the commands leave it alone, as in LoopMax v1. Out of its span the channel is free.
//
// Two enabled schedules of a channel never share time: one would hide the other without
// anyone knowing. A new schedule that would is kept disabled; enabling one that would is
// refused, unless forced: then the others that share its time are disabled.
//
//   lmp::sdk::PinSchedule<2> sched_{pins_};
//   LMP_TRY(use(sched_));
#pragma once
#include "hal/api/iclock.h"
#include "sdk/pin_bank.h"

namespace lmp::sdk {

#ifndef LMP_SCHEDULES_PER_CHANNEL
#define LMP_SCHEDULES_PER_CHANNEL 4
#endif

enum class ScheduleKind : uint8_t { Weekly, Once };

// Minutes in a day, and the bits of the days: bit 0 is Sunday.
constexpr uint16_t kDayMinutes = 1440;
constexpr uint8_t kEveryDay = 0x7F;

struct Schedule {
  uint16_t id = 0;            // 0: the place is free
  uint8_t channel = 0;
  ScheduleKind kind = ScheduleKind::Weekly;
  bool enabled = true;
  uint8_t days = kEveryDay;   // weekly
  uint16_t onMin = 0;         // weekly: minute of the day it turns on
  uint16_t offMin = 0;        // weekly: minute it turns off. Before onMin: the day after
  int64_t onUnix = 0;         // once
  int64_t offUnix = 0;
};

// True when the schedule wants its channel on at that moment.
// minute is the minute of the day, weekday counts from Sunday, 0.
bool scheduleActive(const Schedule& s, int64_t unixNow, uint16_t minute, uint8_t weekday);

// True when the two schedules want their channel at some same moment. utcOffset, in seconds,
// puts a schedule that happens once among the days of the week. One that ends the minute the
// other begins shares nothing.
bool schedulesOverlap(const Schedule& a, const Schedule& b, int32_t utcOffset);

class PinScheduleBase : public Component, public ChannelGuard {
 public:
  uint8_t capacity() const { return capacity_; }
  uint8_t count() const;
  const Schedule* find(uint16_t id) const;

  // Invalid for a span that makes no sense, NoMem when the channel has no place left. A
  // schedule that shares time with an enabled one is added disabled.
  Status add(const Schedule& wanted, uint16_t& id);
  // Busy when the schedule, enabled, would share time with another enabled one; with force the
  // others are disabled instead.
  Status update(uint16_t id, const Schedule& wanted, bool force = false);
  Status remove(uint16_t id);
  Status enable(uint16_t id, bool enabled, bool force = false);
  // The enabled schedules of its channel that share time with s, s itself left out.
  uint8_t conflicts(const Schedule& s, uint16_t* ids, uint8_t cap) const;
  Status clear(uint8_t channel);

  // The clock changed, or the schedules did: at the next round every channel is taken to
  // what its schedules want.
  void resync();

  bool held(uint8_t channel) const override;
  bool planned(uint8_t channel) const override;

  void commands(CommandSet& set) override;
  void describe(JsonWriter& w) const override;
  void save(JsonWriter& w) const override;
  void load(const JsonValue& doc) override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;

 protected:
  PinScheduleBase(PinBankBase& bank, Schedule* slots, uint8_t capacity, int8_t* wanted,
                  uint8_t channels)
      : bank_(bank), slots_(slots), capacity_(capacity), wanted_(wanted), channels_(channels) {
    bank.guard(this);
  }

 private:
  Schedule* findSlot(uint16_t id);
  Status check(const Schedule& s) const;
  int32_t utcOffset() const;
  void disableConflicts(const Schedule& s);
  void writeConflicts(JsonWriter& w, const Schedule& s) const;
  void write(JsonWriter& w, const Schedule& s, bool withState) const;
  bool now(int64_t& unixNow, uint16_t& minute, uint8_t& weekday) const;

  PinBankBase& bank_;
  Schedule* slots_;
  uint8_t capacity_;
  int8_t* wanted_;          // for each channel: -1 not known, 0 off, 1 on
  uint8_t channels_;
  uint16_t nextId_ = 1;
  uint32_t lastTickMs_ = 0;
  bool ticked_ = false;
};

template <size_t N, size_t PerChannel = LMP_SCHEDULES_PER_CHANNEL>
class PinSchedule : public PinScheduleBase {
 public:
  explicit PinSchedule(PinBankBase& bank)
      : PinScheduleBase(bank, slots_, static_cast<uint8_t>(N * PerChannel), wanted_,
                        static_cast<uint8_t>(N)) {
    for (int8_t& w : wanted_) w = -1;
  }

 private:
  Schedule slots_[N * PerChannel];
  int8_t wanted_[N];
};

}  // namespace lmp::sdk
