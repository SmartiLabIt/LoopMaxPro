#include "sdk/pin_schedule.h"

#include <string.h>

#include "services/clock/clock_service.h"

namespace lmp::sdk {
namespace {

// The schedules are looked at once a second: their times are in minutes.
constexpr uint32_t kEveryMs = 1000;

constexpr ArgSpec kAddArgs[] = {
    Arg::u8("channel", 0, 63),
    Arg::str("type", 4, 6).optional(),
    Arg::u8("days", 0, 127).optional(),
    Arg::i64("on"),
    Arg::i64("off"),
    Arg::boolean("enabled").optional(),
};
constexpr ArgSpec kUpdateArgs[] = {
    Arg::u16("id", 1, 65535),
    Arg::u8("days", 0, 127).optional(),
    Arg::i64("on").optional(),
    Arg::i64("off").optional(),
    Arg::boolean("enabled").optional(),
    Arg::boolean("force").optional(),
};
constexpr ArgSpec kIdArgs[] = {
    Arg::u16("id", 1, 65535),
};
constexpr ArgSpec kChannelArgs[] = {
    Arg::u8("channel", 0, 63),
};

const char* kindName(ScheduleKind kind) { return kind == ScheduleKind::Once ? "once" : "weekly"; }

bool kindOf(const char* name, ScheduleKind& out) {
  if (strcmp(name, "weekly") == 0) {
    out = ScheduleKind::Weekly;
    return true;
  }
  if (strcmp(name, "once") == 0) {
    out = ScheduleKind::Once;
    return true;
  }
  return false;
}

// The times of a request, put where the kind of schedule keeps them.
bool setTimes(Schedule& s, int64_t on, int64_t off) {
  if (s.kind == ScheduleKind::Weekly) {
    if (on < 0 || on >= kDayMinutes || off < 0 || off >= kDayMinutes) return false;
    s.onMin = static_cast<uint16_t>(on);
    s.offMin = static_cast<uint16_t>(off);
  } else {
    s.onUnix = on;
    s.offUnix = off;
  }
  return true;
}

// Minutes of the week, from Sunday at midnight.
constexpr int32_t kWeekMinutes = 7 * kDayMinutes;

struct Span {
  int32_t from, to;   // minutes of the week, the end left out
};

// The spans of a schedule in the week: one a day, and the one that crosses the end of the week
// cut in two. A schedule that happens once is put in the week it happens in.
uint8_t spansOf(const Schedule& s, int32_t utcOffset, Span* out) {
  uint8_t n = 0;
  auto put = [&](int32_t from, int32_t length) {
    if (length >= kWeekMinutes) {
      out[n++] = {0, kWeekMinutes};
      return;
    }
    from %= kWeekMinutes;
    const int32_t to = from + length;
    if (to <= kWeekMinutes) {
      out[n++] = {from, to};
    } else {
      out[n++] = {from, kWeekMinutes};
      out[n++] = {0, to - kWeekMinutes};
    }
  };
  if (s.kind == ScheduleKind::Once) {
    // 1 January 1970 was a Thursday: day 4 of its week.
    const int64_t minute = (s.onUnix + utcOffset) / 60 + 4 * kDayMinutes;
    const int64_t length = (s.offUnix - s.onUnix + 59) / 60;
    put(static_cast<int32_t>(minute % kWeekMinutes),
        length > kWeekMinutes ? kWeekMinutes : static_cast<int32_t>(length));
    return n;
  }
  const int32_t length = s.offMin > s.onMin ? s.offMin - s.onMin
                                            : kDayMinutes - s.onMin + s.offMin;
  for (uint8_t day = 0; day < 7; ++day) {
    if (s.days & (1u << day)) put(day * kDayMinutes + s.onMin, length);
  }
  return n;
}

}  // namespace

bool schedulesOverlap(const Schedule& a, const Schedule& b, int32_t utcOffset) {
  if (a.channel != b.channel) return false;
  if (a.kind == ScheduleKind::Once && b.kind == ScheduleKind::Once) {
    return a.onUnix < b.offUnix && b.onUnix < a.offUnix;
  }
  Span sa[9], sb[9];
  const uint8_t na = spansOf(a, utcOffset, sa);
  const uint8_t nb = spansOf(b, utcOffset, sb);
  for (uint8_t i = 0; i < na; ++i) {
    for (uint8_t j = 0; j < nb; ++j) {
      if (sa[i].from < sb[j].to && sb[j].from < sa[i].to) return true;
    }
  }
  return false;
}

bool scheduleActive(const Schedule& s, int64_t unixNow, uint16_t minute, uint8_t weekday) {
  if (!s.id || !s.enabled) return false;
  if (s.kind == ScheduleKind::Once) return unixNow >= s.onUnix && unixNow < s.offUnix;

  const uint8_t today = static_cast<uint8_t>(1u << (weekday % 7));
  const uint8_t yesterday = static_cast<uint8_t>(1u << ((weekday + 6) % 7));
  if (s.onMin < s.offMin) return (s.days & today) && minute >= s.onMin && minute < s.offMin;
  if (s.onMin > s.offMin) {
    // Across midnight: the evening belongs to the day it began, the morning to the one before.
    return ((s.days & today) && minute >= s.onMin) || ((s.days & yesterday) && minute < s.offMin);
  }
  return false;   // on and off at the same minute: no span at all
}

// ---------------------------------------------------------------- the list

uint8_t PinScheduleBase::count() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (slots_[i].id) ++n;
  }
  return n;
}

Schedule* PinScheduleBase::findSlot(uint16_t id) {
  if (!id) return nullptr;
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (slots_[i].id == id) return &slots_[i];
  }
  return nullptr;
}

const Schedule* PinScheduleBase::find(uint16_t id) const {
  return const_cast<PinScheduleBase*>(this)->findSlot(id);
}

Status PinScheduleBase::check(const Schedule& s) const {
  if (s.channel >= channels_ || s.channel >= bank_.size()) return Status::Invalid;
  if (s.kind == ScheduleKind::Weekly) {
    if (s.onMin >= kDayMinutes || s.offMin >= kDayMinutes || s.onMin == s.offMin) {
      return Status::Invalid;
    }
    if ((s.days & kEveryDay) == 0) return Status::Invalid;
  } else if (s.offUnix <= s.onUnix || s.onUnix <= 0) {
    return Status::Invalid;
  }
  return Status::Ok;
}

Status PinScheduleBase::add(const Schedule& wanted, uint16_t& id) {
  id = 0;
  if (!bank_.output()) return Status::Denied;
  LMP_TRY(check(wanted));
  uint8_t ofChannel = 0;
  Schedule* free = nullptr;
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (!slots_[i].id) {
      if (!free) free = &slots_[i];
    } else if (slots_[i].channel == wanted.channel) {
      ++ofChannel;
    }
  }
  const uint8_t each = channels_ ? static_cast<uint8_t>(capacity_ / channels_) : 0;
  if (!free || ofChannel >= each) return Status::NoMem;

  // A number never given before, as far as the device remembers. The one of the request
  // does not count: it is the device that gives the numbers.
  while (!nextId_ || findSlot(nextId_)) ++nextId_;
  *free = wanted;
  free->days &= kEveryDay;
  free->id = nextId_++;
  // It would hide an enabled schedule, or be hidden by it: kept, but disabled.
  uint16_t clash = 0;
  if (free->enabled && conflicts(*free, &clash, 1)) free->enabled = false;
  id = free->id;
  unsaved();
  resync();
  return Status::Ok;
}

Status PinScheduleBase::update(uint16_t id, const Schedule& wanted, bool force) {
  Schedule* s = findSlot(id);
  if (!s) return Status::NotFound;
  Schedule next = wanted;
  next.id = id;
  next.channel = s->channel;
  next.kind = s->kind;
  LMP_TRY(check(next));
  next.days &= kEveryDay;
  if (next.enabled) {
    uint16_t clash = 0;
    if (conflicts(next, &clash, 1)) {
      if (!force) return Status::Busy;
      disableConflicts(next);
    }
  }
  *s = next;
  unsaved();
  resync();
  return Status::Ok;
}

Status PinScheduleBase::remove(uint16_t id) {
  Schedule* s = findSlot(id);
  if (!s) return Status::NotFound;
  *s = Schedule();
  unsaved();
  resync();
  return Status::Ok;
}

Status PinScheduleBase::enable(uint16_t id, bool enabled, bool force) {
  Schedule* s = findSlot(id);
  if (!s) return Status::NotFound;
  if (s->enabled == enabled) return Status::Ok;
  uint16_t clash = 0;
  if (enabled && conflicts(*s, &clash, 1)) {
    if (!force) return Status::Busy;
    disableConflicts(*s);
  }
  s->enabled = enabled;
  unsaved();
  resync();
  return Status::Ok;
}

Status PinScheduleBase::clear(uint8_t channel) {
  if (channel >= channels_) return Status::Invalid;
  bool any = false;
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (slots_[i].id && slots_[i].channel == channel) {
      slots_[i] = Schedule();
      any = true;
    }
  }
  if (any) {
    unsaved();
    resync();
  }
  return Status::Ok;
}

uint8_t PinScheduleBase::conflicts(const Schedule& s, uint16_t* ids, uint8_t cap) const {
  const int32_t offset = utcOffset();
  uint8_t n = 0;
  for (uint8_t i = 0; i < capacity_; ++i) {
    const Schedule& other = slots_[i];
    if (!other.id || other.id == s.id || !other.enabled) continue;
    if (!schedulesOverlap(s, other, offset)) continue;
    if (n < cap) ids[n] = other.id;
    ++n;
  }
  return n;
}

void PinScheduleBase::disableConflicts(const Schedule& s) {
  const int32_t offset = utcOffset();
  for (uint8_t i = 0; i < capacity_; ++i) {
    Schedule& other = slots_[i];
    if (other.id && other.id != s.id && other.enabled && schedulesOverlap(s, other, offset)) {
      other.enabled = false;
    }
  }
}

void PinScheduleBase::writeConflicts(JsonWriter& w, const Schedule& s) const {
  uint16_t ids[LMP_SCHEDULES_PER_CHANNEL * 2];
  constexpr uint8_t kCap = sizeof(ids) / sizeof(ids[0]);
  const uint8_t n = conflicts(s, ids, kCap);
  w.beginArray("conflicts");
  for (uint8_t i = 0; i < n && i < kCap; ++i) w.u64(ids[i]);
  w.endArray();
}

// ---------------------------------------------------------------- time

// Seconds of the local time: a schedule that happens once is compared with the weekly ones
// in the time of the device. 0 without the time.
int32_t PinScheduleBase::utcOffset() const {
  const IClockService* clock = ctx() ? ctx()->clock : nullptr;
  hal::DateTime t;
  return clock && clock->synced() && clock->localTime(t) ? t.utcOffset : 0;
}

void PinScheduleBase::resync() {
  for (uint8_t i = 0; i < channels_; ++i) wanted_[i] = -1;
  ticked_ = false;   // looked at in the next round, not a second later
}

bool PinScheduleBase::now(int64_t& unixNow, uint16_t& minute, uint8_t& weekday) const {
  const IClockService* clock = ctx() ? ctx()->clock : nullptr;
  if (!clock || !clock->synced()) return false;
  hal::DateTime t;
  if (!clock->localTime(t)) return false;
  unixNow = clock->unixTime();
  minute = static_cast<uint16_t>(t.hour * 60 + t.minute);
  weekday = t.weekday;
  return true;
}

void PinScheduleBase::tick(uint32_t nowMs) {
  if (ticked_ && static_cast<uint32_t>(nowMs - lastTickMs_) < kEveryMs) return;
  ticked_ = true;
  lastTickMs_ = nowMs;

  int64_t unixNow = 0;
  uint16_t minute = 0;
  uint8_t weekday = 0;
  // A device that does not know what time it is leaves the channels alone.
  if (!now(unixNow, minute, weekday)) return;

  for (uint8_t channel = 0; channel < channels_; ++channel) {
    bool any = false;
    bool on = false;
    for (uint8_t i = 0; i < capacity_; ++i) {
      Schedule& s = slots_[i];
      if (!s.id || s.channel != channel) continue;
      // What happened once and is over has nothing more to say.
      if (s.kind == ScheduleKind::Once && unixNow >= s.offUnix) {
        s = Schedule();
        unsaved();
        // Its end is an edge only if it was holding the channel on: one that ended while
        // the device was off changes nothing.
        if (wanted_[channel] == 1) any = true;
        continue;
      }
      if (!s.enabled) continue;
      any = true;
      if (scheduleActive(s, unixNow, minute, weekday)) on = true;
    }
    if (!any) {
      wanted_[channel] = -1;
      continue;
    }
    const int8_t want = on ? 1 : 0;
    if (wanted_[channel] == want) continue;
    wanted_[channel] = want;
    bank_.set(channel, on);
  }
}

bool PinScheduleBase::planned(uint8_t channel) const {
  for (uint8_t i = 0; i < capacity_; ++i) {
    const Schedule& s = slots_[i];
    if (s.id && s.enabled && s.channel == channel) return true;
  }
  return false;
}

bool PinScheduleBase::held(uint8_t channel) const {
  int64_t unixNow = 0;
  uint16_t minute = 0;
  uint8_t weekday = 0;
  // Without the time no schedule acts, and none holds its channel.
  if (!planned(channel) || !now(unixNow, minute, weekday)) return false;
  for (uint8_t i = 0; i < capacity_; ++i) {
    const Schedule& s = slots_[i];
    if (s.channel == channel && scheduleActive(s, unixNow, minute, weekday)) return true;
  }
  return false;
}

void PinScheduleBase::onEvent(const Event& event) {
  if (event.id == EventId::TimeSynced) resync();
}

// ---------------------------------------------------------------- documents and file

void PinScheduleBase::write(JsonWriter& w, const Schedule& s, bool withState) const {
  w.beginObject();
  w.u32("id", s.id);
  w.u32("channel", s.channel);
  w.str("type", kindName(s.kind));
  w.boolean("enabled", s.enabled);
  if (s.kind == ScheduleKind::Weekly) {
    w.u32("days", s.days);
    w.u32("on", s.onMin);
    w.u32("off", s.offMin);
  } else {
    w.i64("on", s.onUnix);
    w.i64("off", s.offUnix);
  }
  if (withState) {
    int64_t unixNow = 0;
    uint16_t minute = 0;
    uint8_t weekday = 0;
    w.boolean("active", now(unixNow, minute, weekday) &&
                            scheduleActive(s, unixNow, minute, weekday));
    writeConflicts(w, s);
  }
  w.endObject();
}

void PinScheduleBase::describe(JsonWriter& w) const {
  w.beginArray("schedules");
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (slots_[i].id) write(w, slots_[i], true);
  }
  w.endArray();
  w.u32("schedulesMax", channels_ ? capacity_ / channels_ : 0);
  const IClockService* clock = ctx() ? ctx()->clock : nullptr;
  // Without the time nothing is followed: the console says so.
  w.boolean("schedulesRunning", clock && clock->synced());
}

void PinScheduleBase::save(JsonWriter& w) const {
  w.beginArray("schedules");
  for (uint8_t i = 0; i < capacity_; ++i) {
    if (slots_[i].id) write(w, slots_[i], false);
  }
  w.endArray();
}

void PinScheduleBase::load(const JsonValue& doc) {
  for (uint8_t i = 0; i < capacity_; ++i) slots_[i] = Schedule();
  nextId_ = 1;
  const JsonValue list = doc.get("schedules");
  if (!list.isArray()) return;

  uint8_t used = 0;
  for (const JsonMember m : list) {
    if (used >= capacity_) break;
    Schedule s;
    char type[8] = "";
    int64_t id = 0, channel = -1, days = kEveryDay, on = 0, off = 0;
    if (!m.value.get("id").getInt(id) || id <= 0 || id > 65535) continue;
    if (!m.value.get("channel").getInt(channel) || channel < 0 || channel > 255) continue;
    if (!m.value.get("type").getString(type, sizeof(type)) || !kindOf(type, s.kind)) continue;
    if (!m.value.get("on").getInt(on) || !m.value.get("off").getInt(off)) continue;
    m.value.get("days").getInt(days);
    s.id = static_cast<uint16_t>(id);
    s.channel = static_cast<uint8_t>(channel);
    s.days = static_cast<uint8_t>(days & kEveryDay);
    s.enabled = m.value.get("enabled").asBool(true);
    // What the file says is checked as what a command says: a file can be old, or wrong.
    if (!setTimes(s, on, off) || check(s) != Status::Ok || findSlot(s.id)) continue;
    slots_[used++] = s;
    if (s.id >= nextId_) nextId_ = static_cast<uint16_t>(s.id + 1);
  }
  resync();
}

void PinScheduleBase::commands(CommandSet& set) {
  set.add("sched.add",
          LMP_HELP("Adds a schedule. weekly: days and minutes of the day. once: two moments"),
          kAddArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            Schedule s;
            s.channel = a.u8("channel");
            if (a.has("type") && !kindOf(a.str("type"), s.kind)) {
              ctx.errorField = "type";
              return Status::Invalid;
            }
            s.days = a.u8("days", kEveryDay);
            s.enabled = a.boolean("enabled", true);
            if (!setTimes(s, a.i64("on"), a.i64("off"))) {
              ctx.errorField = "on";
              return Status::Invalid;
            }
            uint16_t id = 0;
            const Status status = add(s, id);
            if (status == Status::Invalid) {
              ctx.errorField = s.channel >= bank_.size() ? "channel"
                                   : ((s.days & kEveryDay) == 0 ? "days" : "off");
            }
            if (status == Status::Ok && ctx.result) {
              // Kept disabled when it shares time with another: the answer says with which.
              const Schedule* added = find(id);
              ctx.result->u32("id", id);
              ctx.result->boolean("enabled", added && added->enabled);
              if (added) writeConflicts(*ctx.result, *added);
            }
            return status;
          });

  set.add("sched.update", LMP_HELP("Changes days, times or state of a schedule"), kUpdateArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Schedule* old = find(a.u16("id"));
            if (!old) {
              ctx.errorField = "id";
              return Status::NotFound;
            }
            Schedule s = *old;
            if (a.has("days")) s.days = a.u8("days");
            if (a.has("enabled")) s.enabled = a.boolean("enabled");
            const bool weekly = s.kind == ScheduleKind::Weekly;
            const int64_t on = a.has("on") ? a.i64("on") : (weekly ? s.onMin : s.onUnix);
            const int64_t off = a.has("off") ? a.i64("off") : (weekly ? s.offMin : s.offUnix);
            if (!setTimes(s, on, off)) {
              ctx.errorField = "on";
              return Status::Invalid;
            }
            const Status status = update(s.id, s, a.boolean("force", false));
            if (status == Status::Invalid) {
              ctx.errorField = (s.days & kEveryDay) == 0 ? "days" : "off";
            }
            // Refused because it would share time with others: the answer says with which,
            // and force enables it and disables them.
            if (status == Status::Busy) {
              ctx.errorField = "enabled";
              if (ctx.result) writeConflicts(*ctx.result, s);
            }
            return status;
          });

  set.add("sched.remove", LMP_HELP("Removes a schedule"), kIdArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Status status = remove(a.u16("id"));
            if (status != Status::Ok) ctx.errorField = "id";
            return status;
          });

  set.add("sched.clear", LMP_HELP("Removes every schedule of a channel"), kChannelArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Status status = clear(a.u8("channel"));
            if (status != Status::Ok) ctx.errorField = "channel";
            return status;
          },
          kCmdConfirm);
}

}  // namespace lmp::sdk
