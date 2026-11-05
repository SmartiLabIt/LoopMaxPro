#include "services/clock/clock_service.h"

#include <stdio.h>
#include <string.h>

#if LMP_FEATURE_NET
#include "services/net/net_service.h"
#endif

namespace lmp {
namespace {

// Any time before this is a clock that was never set.
constexpr int64_t kMinValidUnix = 1600000000;   // September 2020
constexpr int64_t kMaxValidUnix = 4102444800;   // January 2100
// A command that asks the time to the network is given up after this long.
constexpr uint32_t kAskTimeoutMs = 15000;

constexpr ArgSpec kSetArgs[] = {
    Arg::i64("unix").optional(),
    Arg::str("tz", 1, 40).optional(),
    Arg::str("posix", 1, 48).optional(),
    Arg::str("ntp", 0, 48).optional(),
};

}  // namespace

const char* timeSourceName(TimeSource source) {
  switch (source) {
    case TimeSource::Client: return "client";
    case TimeSource::Ntp:    return "ntp";
    case TimeSource::Cloud:  return "cloud";
    case TimeSource::None:   break;
  }
  return "none";
}

const ServiceInfo& ClockService::info() const {
  static const ServiceInfo kInfo{ServiceId::Clock, "clock", "\xF0\x9F\x95\xA5", Mode::Any,
                                 RestartPolicy::Retry, 500,
                                 DepList(ServiceId::Config, ServiceId::Log)};
  return kInfo;
}

Status ClockService::start(Kernel& kernel) {
  kernel_ = &kernel;
  clock_ = kernel.board().clock;
  if (!clock_) return Status::Unsupported;
  ntp_ = kernel.board().ntp;
  src_ = kernel.log().addSource(info().name, info().icon);
  asking_ = false;
  online_ = false;
  if (ntp_) answersSeen_ = ntp_->answers();

  const ClockConfig& cfg = kernel.config().clock;
  if (!cfg.posixTz.empty()) clock_->setTimezone(cfg.posixTz.c_str());

  // A battery backed clock, or a restart without power loss, may already have the time.
  if (clock_->unixTime() >= kMinValidUnix) {
    if (source_ == TimeSource::None) source_ = TimeSource::Client;
  } else {
    source_ = TimeSource::None;
    degrade("time not set");
  }

#if LMP_FEATURE_NET
  // Started again while the device is on a network: the event is not coming.
  const INetService* net = kernel.api<INetService>();
  if (net && net->connected()) {
    online_ = true;
    askNetwork();
  }
#endif
  return Status::Ok;
}

void ClockService::stop() {
  if (ntp_) ntp_->stop();
  asking_ = false;
}

int64_t ClockService::unixTime() const {
  if (!clock_ || !synced()) return 0;
  return clock_->unixTime();
}

bool ClockService::localTime(hal::DateTime& out) const {
  const int64_t now = unixTime();
  return now > 0 && clock_->localTime(now, out);
}

bool ClockService::localTimeOf(int64_t unixSeconds, hal::DateTime& out) const {
  return synced() && unixSeconds > 0 && clock_->localTime(unixSeconds, out);
}

void ClockService::accept(TimeSource source, int64_t unixSeconds) {
  source_ = source;
  syncedAt_ = unixSeconds;
  recover();

  hal::DateTime t;
  if (clock_->localTime(unixSeconds, t)) {
    LMP_LOGI(src_, "time set from %s: %04u-%02u-%02u %02u:%02u:%02u", timeSourceName(source),
             static_cast<unsigned>(t.year), static_cast<unsigned>(t.month),
             static_cast<unsigned>(t.day), static_cast<unsigned>(t.hour),
             static_cast<unsigned>(t.minute), static_cast<unsigned>(t.second));
  }
  if (kernel_) {
    kernel_->events().publish(Event(EventId::TimeSynced, static_cast<uint8_t>(source), 0,
                                    static_cast<uint32_t>(unixSeconds)));
  }
}

Status ClockService::setTime(int64_t unixSeconds, TimeSource source) {
  if (!clock_) return Status::Unsupported;
  if (unixSeconds < kMinValidUnix || unixSeconds > kMaxValidUnix || source == TimeSource::None) {
    return Status::Invalid;
  }
  LMP_TRY(clock_->setUnixTime(unixSeconds));
  accept(source, unixSeconds);
  return Status::Ok;
}

Status ClockService::setTimezone(const char* tz, const char* posix) {
  if (!clock_ || !kernel_) return Status::Unsupported;
  if (!posix || !*posix) return Status::Invalid;
  LMP_TRY(clock_->setTimezone(posix));
  ClockConfig& cfg = kernel_->config().clock;
  cfg.posixTz.assign(posix);
  cfg.tz.assign(tz && *tz ? tz : posix);
  kernel_->config().touch(Config::kClock);
  LMP_LOGI(src_, "time zone %s (%s)", cfg.tz.c_str(), cfg.posixTz.c_str());
  return Status::Ok;
}

// ---------------------------------------------------------------- time from the network

Status ClockService::askNetwork() {
  if (!ntp_) return Status::Unsupported;
  if (!online_) return Status::Denied;
  const ClockConfig& cfg = kernel_->config().clock;
  if (ntp_->running()) ntp_->stop();
  server_ = cfg.ntpServer;
  if (server_.empty()) return Status::Denied;   // turned off by the settings
  answersSeen_ = ntp_->answers();
  const Status s = ntp_->start(server_.c_str());
  if (s != Status::Ok) {
    LMP_LOGW(src_, "time from %s not asked: %s", server_.c_str(), statusName(s));
    return s;
  }
  LMP_LOGD(src_, "asking the time to %s", server_.c_str());
  return Status::Ok;
}

void ClockService::tick(uint32_t nowMs) {
  if (!ntp_) return;

  if (ntp_->answers() != answersSeen_) {
    answersSeen_ = ntp_->answers();
    const int64_t now = clock_->unixTime();
    if (now >= kMinValidUnix && now <= kMaxValidUnix) {
      accept(TimeSource::Ntp, now);
      if (asking_) {
        asking_ = false;
        kernel_->events().publish(commandDone(askRef_, Status::Ok));
      }
    } else {
      LMP_LOGW(src_, "answer of %s not usable", server_.c_str());
    }
  }

  if (asking_ && static_cast<int32_t>(nowMs - askedAtMs_) > static_cast<int32_t>(kAskTimeoutMs)) {
    asking_ = false;
    LMP_LOGW(src_, "no answer from %s", server_.c_str());
    kernel_->events().publish(commandDone(askRef_, Status::Timeout));
  }
}

void ClockService::onEvent(const Event& event) {
  if (!kernel_ || !ntp_) return;
  if (event.id == EventId::NetStaConnected) {
    online_ = true;
    askNetwork();
  } else if (event.id == EventId::NetStaLost) {
    online_ = false;
    ntp_->stop();
  } else if (event.id == EventId::ConfigChanged && event.a == Config::kClock) {
    // A new server: the next answer comes from there.
    if (online_ && !(server_ == kernel_->config().clock.ntpServer)) askNetwork();
  }
}

// ---------------------------------------------------------------- documents and commands

void ClockService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const ClockConfig& cfg = kernel_->config().clock;
  const int64_t now = unixTime();
  w.i64("unix", now);
  w.boolean("synced", synced());
  w.str("source", timeSourceName(source_));
  w.i64("syncedAt", syncedAt_);
  w.str("tz", cfg.tz.c_str());
  w.str("posix", cfg.posixTz.c_str());
  w.str("ntpServer", cfg.ntpServer.c_str());
  w.beginObject("ntp");
  w.boolean("available", ntp_ != nullptr);
  w.boolean("running", ntp_ && ntp_->running());
  w.u32("answers", ntp_ ? ntp_->answers() : 0);
  w.endObject();
  hal::DateTime t;
  if (now > 0 && clock_->localTime(now, t)) {
    char text[40];
    snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(t.year),
             static_cast<unsigned>(t.month), static_cast<unsigned>(t.day),
             static_cast<unsigned>(t.hour), static_cast<unsigned>(t.minute),
             static_cast<unsigned>(t.second));
    w.str("local", text);
    w.i32("offset", t.utcOffset);
    w.u32("weekday", t.weekday);
  }
}

void ClockService::commands(CommandSet& set) {
  set.add("set",
          LMP_HELP("Sets the time, the time zone, the server of the time: one or more"), kSetArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!a.has("unix") && !a.has("posix") && !a.has("ntp")) return Status::Invalid;
            if (a.has("tz") && !a.has("posix")) {
              ctx.errorField = "posix";
              return Status::Invalid;
            }
            // Time zone first: the log shows the local time with the new zone.
            if (a.has("posix")) {
              const Status s = setTimezone(a.str("tz"), a.str("posix"));
              if (s != Status::Ok) {
                ctx.errorField = "posix";
                return s;
              }
            }
            if (a.has("ntp")) {
              kernel_->config().clock.ntpServer.assign(a.str("ntp"));
              kernel_->config().touch(Config::kClock);
              kernel_->events().publish(Event(EventId::ConfigChanged, Config::kClock));
            }
            if (a.has("unix")) {
              const Status s = setTime(a.i64("unix"), TimeSource::Client);
              if (s != Status::Ok) {
                ctx.errorField = "unix";
                return s;
              }
            }
            return Status::Ok;
          });

  set.add("sync", LMP_HELP("Asks the time to the server on the network, now"),
          [this](const Args&, CmdContext& ctx) -> Status {
            if (asking_) return Status::Busy;
            const Status s = askNetwork();
            if (s != Status::Ok) return s;
            asking_ = true;
            askRef_ = ctx.ref;
            askedAtMs_ = kernel_->now();
            return Status::Pending;
          },
          kCmdAsync);
}

}  // namespace lmp
