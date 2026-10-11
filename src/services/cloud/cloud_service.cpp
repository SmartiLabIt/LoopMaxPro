#include "services/cloud/cloud_service.h"

#if LMP_FEATURE_CLOUD

#include <stdio.h>
#include <string.h>

#include <new>

#include "hal/api/memory.h"
#include "services/clock/clock_service.h"
#include "services/config/config_service.h"
#if LMP_FEATURE_MODULES
#include "services/modules/modules_service.h"
#include "kernel/service/product.h"
#endif

namespace lmp {
namespace {

constexpr uint16_t kAllParts = 0xFFFF;
constexpr uint16_t kEveryPart = 0xFF;   // the eight parts one by one, as bits
// What the periodic report carries: the figures that change by themselves.
constexpr uint16_t kHeartbeatParts = 2 | 4 | 8;   // system, clock, net

constexpr const char* kPartNames[] = {"device", "system", "clock", "net",
                                      "geo", "storage", "services", "modules"};

constexpr ArgSpec kSetArgs[] = {
    Arg::u16("heartbeatMin", 0, 1440).optional(),
    Arg::u8("pushLogLevel", 0, 2).optional(),
};

// "system,net": the names of the parts in the mask.
void partsText(uint16_t parts, char* out, size_t cap) {
  out[0] = '\0';
  if (parts == kAllParts) return;   // empty: every part
  size_t used = 0;
  for (size_t i = 0; i < sizeof(kPartNames) / sizeof(kPartNames[0]); ++i) {
    if (!(parts & (1u << i))) continue;
    const int n = snprintf(out + used, cap - used, "%s%s", used ? "," : "", kPartNames[i]);
    if (n < 0 || static_cast<size_t>(n) >= cap - used) break;
    used += static_cast<size_t>(n);
  }
}

// The events the console follows to read again what changed. Not these: the data of a module,
// which goes itself as a "doc"; the end of a command, which is an "ack"; the cloud, which the
// server knows better than the device.
bool forServer(EventId id) {
  switch (id) {
    case EventId::ServiceState:
    case EventId::ConfigChanged:
    case EventId::ModeChanged:
    case EventId::NetApUp:
    case EventId::NetApDown:
    case EventId::NetStaConnected:
    case EventId::NetStaLost:
    case EventId::NetFallback:
    case EventId::NetJoinResult:
    case EventId::NetNear:
    case EventId::InternetUp:
    case EventId::InternetDown:
    case EventId::TimeSynced:
    case EventId::ModuleState:
    case EventId::OtaProgress:
    case EventId::OtaDone:
    case EventId::ResetRequested:
    case EventId::MqttConnected:
    case EventId::MqttLost:
    case EventId::LoraReceived:
    case EventId::LoraSent:
    case EventId::ProductChanged:
      return true;
    default:
      return false;
  }
}

void copyText(StringView text, char* out, size_t cap) {
  const size_t n = text.size < cap - 1 ? text.size : cap - 1;
  if (n) memcpy(out, text.data, n);
  out[n] = '\0';
}

}  // namespace

CloudService::~CloudService() {
  if (kernel_) kernel_->log().removeSink(this);
  hal::freeLarge(out_);
  hal::freeLarge(inbox_);
}

const ServiceInfo& CloudService::info() const {
  static const ServiceInfo kInfo{ServiceId::Cloud, "cloud", "\xE2\x98\x81", Mode::Lan,
                                 RestartPolicy::Retry, 50,
                                 DepList(ServiceId::Net, ServiceId::Config, ServiceId::Log)};
  return kInfo;
}

// ---------------------------------------------------------------- life cycle

Status CloudService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  if (!mutex_ && kernel.board().rtos) mutex_ = kernel.board().rtos->createMutex();
  kernel.log().addSink(this);
  up_ = false;
  head_ = count_ = 0;
  // A device without a bridge works on its own: the service waits for one, doing nothing.
  if (bridge_) {
    LMP_LOGI(src_, "bridge %s", bridge_->name());
  }
  return Status::Ok;
}

void CloudService::stop() {
  if (up_ && kernel_) kernel_->events().publish(Event(EventId::CloudLost));
  up_ = false;
  if (kernel_) kernel_->log().removeSink(this);
}

bool CloudService::roomReady() {
  if (!out_) out_ = static_cast<char*>(hal::allocLarge(kOutMax));
  if (!inbox_) inbox_ = static_cast<Incoming*>(hal::allocLarge(sizeof(Incoming) * LMP_CLOUD_INBOX));
  return out_ && inbox_;
}

Status CloudService::attach(ICloudBridge& bridge) {
  if (bridge_ == &bridge) return Status::Ok;
  if (bridge_) return Status::Busy;
  if (!roomReady()) return Status::NoMem;
  bridge_ = &bridge;
  bridge.onReceive([this](StringView envelope) { onReceive(envelope); });
  if (kernel_) LMP_LOGI(src_, "bridge %s attached", bridge.name());
  return Status::Ok;
}

void CloudService::detach(ICloudBridge& bridge) {
  if (bridge_ != &bridge) return;
  bridge.onReceive(CloudReceiveFn());
  if (up_ && kernel_) kernel_->events().publish(Event(EventId::CloudLost));
  up_ = false;
  bridge_ = nullptr;
  for (Waiting& wt : waiting_) wt = Waiting();
  hal::LockGuard guard(mutex_);
  head_ = count_ = 0;
}

void CloudService::tick(uint32_t nowMs) {
  if (!bridge_ || !kernel_) return;
  bridge_->poll(nowMs);

  const bool now = bridge_->connected();
  if (now && !up_) {
    onUp(nowMs);
  } else if (!now && up_) {
    up_ = false;
    LMP_LOGW(src_, "server lost");
    kernel_->events().publish(Event(EventId::CloudLost));
  }

  // One request of the server for each round: a command can take a while.
  Incoming* in = nullptr;
  {
    hal::LockGuard guard(mutex_);
    if (count_) in = &inbox_[head_];
  }
  if (in) {
    handle(*in);
    hal::LockGuard guard(mutex_);
    head_ = (head_ + 1) % LMP_CLOUD_INBOX;
    --count_;
  }

  if (up_) flush(nowMs);
}

void CloudService::onUp(uint32_t nowMs) {
  up_ = true;
  upSinceMs_ = nowMs;
  ++connections_;
  // The server knows nothing of what happened while the device was away: everything again.
  fullReport_ = true;
  parts_ = 0;
  single_ = 0;
  moduleDocs_ = 0;
  eventCount_ = 0;
#if LMP_FEATURE_MODULES
  const IModulesService* modules = kernel_->api<IModulesService>();
  const size_t n = modules ? modules->count() : 0;
  for (size_t i = 0; i < n && i < 32; ++i) moduleDocs_ |= 1u << i;
  docsAtMs_ = nowMs;
#endif
  const uint16_t beat = kernel_->config().cloud.heartbeatMin;
  nextBeatMs_ = nowMs + beat * 60000u;
  LMP_LOGI(src_, "server connected through %s", bridge_->name());
  kernel_->events().publish(Event(EventId::CloudConnected));
}

// ---------------------------------------------------------------- what the device tells

void CloudService::flush(uint32_t nowMs) {
  // Commands that ended: the outcome goes with the ref of the server.
  for (Waiting& wt : waiting_) {
    if (!wt.done) continue;
    const Status outcome = wt.outcome;
    if (!send("ack", StringView(), StringView(), StringView(wt.ref), [outcome](JsonWriter& w) {
          w.beginObject();
          w.boolean("ok", outcome == Status::Ok);
          w.str("status", statusName(outcome));
          w.endObject();
        })) {
      return;
    }
    wt.busRef = 0;
    wt.done = false;
  }
  if (eventCount_) {
    if (!sendEvents()) return;
    eventCount_ = 0;
  }
  if (fullReport_) {
    parts_ = 0;
    if (!sendReport(kAllParts, StringView(), true)) return;
    fullReport_ = false;
    if (tooLarge_) single_ = kEveryPart;
  }
  if (parts_ && static_cast<int32_t>(nowMs - partsAtMs_) >= 0) {
    if (!sendParts(parts_)) return;
    parts_ = 0;
  }
  for (size_t i = 0; single_ && i < 16; ++i) {
    const uint16_t bit = static_cast<uint16_t>(1u << i);
    if (!(single_ & bit)) continue;
    if (!sendReport(bit, StringView())) return;
    single_ &= static_cast<uint16_t>(~bit);
  }
#if LMP_FEATURE_MODULES
  if (moduleDocs_ && static_cast<int32_t>(nowMs - docsAtMs_) >= 0) {
    IModulesService* modules = kernel_->api<IModulesService>();
    for (size_t i = 0; i < 32 && moduleDocs_; ++i) {
      if (!(moduleDocs_ & (1u << i))) continue;
      const char* name = modules ? modules->nameAt(i) : nullptr;
      if (name) {
        char target[40];
        snprintf(target, sizeof(target), "mod.%s", name);
        if (!sendDoc(StringView(target), StringView())) return;
      }
      moduleDocs_ &= ~(1u << i);
    }
  }
#endif
  size_t logs = 0;
  uint32_t first = 0;
  {
    hal::LockGuard guard(mutex_);
    logs = logCount_;
    first = logs ? logSeq_[0] : 0;
  }
  if (logs) {
    if (!sendLog(first - 1, static_cast<uint32_t>(logs), StringView())) return;
    hal::LockGuard guard(mutex_);
    logCount_ = 0;
  }
  const uint16_t beat = kernel_->config().cloud.heartbeatMin;
  if (beat && static_cast<int32_t>(nowMs - nextBeatMs_) >= 0) {
    if (!sendParts(kHeartbeatParts)) return;
    nextBeatMs_ = nowMs + beat * 60000u;
  }
}

bool CloudService::send(const char* type, StringView target, StringView name, StringView ref,
                        DescribeFn body, bool whole) {
  tooLarge_ = false;
  if (!bridge_ || !out_) return true;   // nobody to tell: nothing stays due
  const Config& config = kernel_->config();
  const IClockService* clock = kernel_->api<IClockService>();

  BufferSink sink(out_, kOutMax);
  JsonWriter w(sink);
  w.beginObject();
  w.u32("v", 2);
  w.str("type", type);
  w.str("key", config.sys.key.c_str());
  if (*LMP_DEVICE_CODE) w.str("code", LMP_DEVICE_CODE);
  if (!target.empty()) w.str("target", target);
  if (!name.empty()) w.str("name", name);
  if (!ref.empty()) w.str("ref", ref);
  w.i64("ts", clock && clock->synced() ? clock->unixTime() : 0);
  w.key("body");
  body(w);
  w.endObject();
  if (!w.complete() && whole) {
    tooLarge_ = true;
    return true;
  }
  if (!w.complete()) {
    // Longer than the room: the server is told, without the content.
    sink.clear();
    JsonWriter again(sink);
    again.beginObject();
    again.u32("v", 2);
    again.str("type", type);
    again.str("key", config.sys.key.c_str());
    if (!target.empty()) again.str("target", target);
    if (!ref.empty()) again.str("ref", ref);
    again.beginObject("body");
    again.str("error", "too_large");
    again.endObject();
    again.endObject();
  }
  outLength_ = sink.size();
  const Status s = bridge_->send(sink.view());
  if (s == Status::Busy) return false;
  if (s == Status::Ok) {
    ++sent_;
  } else {
    ++failed_;
  }
  return true;
}

bool CloudService::sendParts(uint16_t parts) {
  if (!sendReport(parts, StringView(), true)) return false;
  if (tooLarge_) single_ |= parts;
  return true;
}

bool CloudService::sendReport(uint16_t parts, StringView ref, bool whole) {
  char names[96];
  partsText(parts, names, sizeof(names));
  struct Job {
    Kernel* kernel;
    const char* parts;
  } job{kernel_, names};
  return send("report", StringView(), StringView(), ref, [&job](JsonWriter& w) {
    job.kernel->report(w, StringView(job.parts), true);
  }, whole);
}

bool CloudService::sendDoc(StringView target, StringView ref) {
  struct Job {
    Kernel* kernel;
    StringView target;
  } job{kernel_, target};
  return send("doc", target, StringView(), ref, [&job](JsonWriter& w) {
    w.beginObject();
    if (job.kernel->commands().describe(job.target, w) != Status::Ok) w.str("error", "not_found");
    w.endObject();
  });
}

bool CloudService::sendLog(uint32_t since, uint32_t max, StringView ref) {
  struct Job {
    Kernel* kernel;
    uint32_t since;
    uint32_t max;
  } job{kernel_, since, max};
  return send("log", StringView(), StringView(), ref, [&job](JsonWriter& w) {
    Logger& log = job.kernel->log();
    w.beginObject();
    w.u32("seqLast", log.lastSeq());
    w.beginArray("items");
    uint32_t next = job.since;
    LogEntry e;
    for (uint32_t i = 0; i < job.max && log.read(next, &e, 1) == 1; ++i) {
      next = e.seq;
      w.beginObject();
      w.u32("seq", e.seq);
      w.u32("ms", e.ms);
      w.str("lvl", logLevelName(e.level));
      w.str("src", log.sourceName(e.source));
      w.str("msg", e.msg);
      w.endObject();
    }
    w.endArray();
    w.endObject();
  });
}

bool CloudService::sendEvents() {
  struct Job {
    Kernel* kernel;
    const Event* events;
    size_t count;
  } job{kernel_, events_, eventCount_};
  return send("event", StringView(), StringView(), StringView(), [&job](JsonWriter& w) {
    w.beginObject();
    w.beginArray("items");
    for (size_t i = 0; i < job.count; ++i) job.kernel->describeEvent(job.events[i], w);
    w.endArray();
    w.endObject();
  });
}

void CloudService::answerError(const char* type, StringView ref, const char* error) {
  send(type, StringView(), StringView(), ref, [error](JsonWriter& w) {
    w.beginObject();
    w.str("error", error);
    w.endObject();
  });
}

// ---------------------------------------------------------------- what the server asks

// Context of the bridge: copied, handled at the next round.
void CloudService::onReceive(StringView envelope) {
  hal::LockGuard guard(mutex_);
  if (!inbox_ || count_ >= LMP_CLOUD_INBOX) {
    ++dropped_;
    return;
  }
  Incoming& in = inbox_[(head_ + count_) % LMP_CLOUD_INBOX];
  in.cut = envelope.size >= sizeof(in.text);
  in.length = static_cast<uint16_t>(envelope.copyTo(in.text, sizeof(in.text)));
  ++count_;
}

void CloudService::handle(const Incoming& in) {
  ++received_;
  if (in.cut) {
    ++refused_;
    LMP_LOGW(src_, "request of the server too long: not handled");
    return;
  }
  const JsonValue env = JsonValue::parse(StringView(in.text, in.length));
  if (!env.isObject()) {
    ++refused_;
    LMP_LOGW(src_, "request of the server not readable");
    return;
  }
  char ref[24] = "";
  env["ref"].getString(ref, sizeof(ref));
  // The bridge may carry the envelopes of more devices: the ones for another are left.
  const JsonValue key = env["key"];
  if (key.valid() && !key.stringEquals(kernel_->config().sys.key.view())) {
    ++refused_;
    LMP_LOGW(src_, "request for another device");
    return;
  }
  char type[12] = "";
  env["type"].getString(type, sizeof(type));
  char target[40] = "";
  env["target"].getString(target, sizeof(target));
  const JsonValue body = env["body"];

  if (strcmp(type, "cmd") == 0) {
    char name[32] = "";
    env["name"].getString(name, sizeof(name));
    if (!target[0] || !name[0]) {
      ++refused_;
      answerError("ack", StringView(ref), "invalid");
      return;
    }
    runCommand(StringView(target), StringView(name),
               body.valid() ? body.raw() : StringView(), StringView(ref));
  } else if (strcmp(type, "doc") == 0) {
    sendDoc(StringView(target), StringView(ref));
  } else if (strcmp(type, "report") == 0) {
    // body: "system,net", or {"parts":"system,net"}, or nothing for the whole report.
    char parts[96] = "";
    if (!body.getString(parts, sizeof(parts))) body["parts"].getString(parts, sizeof(parts));
    struct Job {
      Kernel* kernel;
      const char* parts;
    } job{kernel_, parts};
    send("report", StringView(), StringView(), StringView(ref), [&job](JsonWriter& w) {
      job.kernel->report(w, StringView(job.parts), true);
    });
  } else if (strcmp(type, "log") == 0) {
    const int64_t since = body["since"].asInt(0);
    int64_t max = body["max"].asInt(20);
    if (max < 1) max = 1;
    if (max > 50) max = 50;
    sendLog(since > 0 ? static_cast<uint32_t>(since) : 0, static_cast<uint32_t>(max),
            StringView(ref));
  } else if (strcmp(type, "manifest") == 0) {
    struct Job {
      Kernel* kernel;
      const char* target;
    } job{kernel_, target};
    send("manifest", StringView(target), StringView(), StringView(ref), [&job](JsonWriter& w) {
      if (!job.target[0]) {
        w.beginObject();
        w.key("targets");
        job.kernel->commands().targets(w);
        w.endObject();
      } else if (job.kernel->commands().manifest(StringView(job.target), w) != Status::Ok) {
        w.beginObject();
        w.str("error", "not_found");
        w.endObject();
      }
    });
  } else if (strcmp(type, "cfg") == 0) {
    // The settings of a target, without secrets: the same answer as GET /api/cfg/<target>.
    struct Job {
      Kernel* kernel;
      const char* target;
    } job{kernel_, target};
    send("cfg", StringView(target), StringView(), StringView(ref), [&job](JsonWriter& w) {
      if (describeTargetConfig(*job.kernel, StringView(job.target), w) != Status::Ok) {
        w.beginObject();
        w.str("error", "not_found");
        w.endObject();
      }
    });
  } else if (strcmp(type, "time") == 0) {
    // The server knows the time: taken when no time server gave it.
    IClockService* clock = kernel_->api<IClockService>();
    const int64_t unix = body["unix"].asInt(0);
    if (clock && unix > 0 && !clock->synced()) clock->setTime(unix, TimeSource::Cloud);
  } else {
    // A type of the service of the product, in a firmware that has one. No answer is due:
    // the next report tells what changed.
    IProduct* product = kernel_->api<IProduct>();
    const Status taken = product ? product->onServerRequest(type, body) : Status::Unsupported;
    if (taken == Status::Unsupported) {
      ++refused_;
      LMP_LOGW(src_, "request of the server of unknown type");
      answerError("error", StringView(ref), "unknown_type");
    } else if (taken != Status::Ok) {
      ++refused_;
    }
  }
}

void CloudService::runCommand(StringView target, StringView name, StringView args,
                              StringView ref) {
  struct Job {
    Kernel* kernel;
    StringView target;
    StringView name;
    StringView args;
  } job{kernel_, target, name, args};
  send("ack", target, name, ref, [&job](JsonWriter& w) {
    job.kernel->commands().answer(job.target, job.name, job.args, ArgsForm::Json,
                                  CmdSource::Remote, w);
  });
  if (!out_ || ref.empty()) return;
  // A command that ends later: its outcome is told with the same ref (CommandDone).
  const JsonValue sent = JsonValue::parse(StringView(out_, outLength_));
  const JsonValue answer = sent["body"];
  if (!answer["status"].stringEquals("pending")) return;
  const int64_t busRef = answer["ref"].asInt(0);
  if (busRef <= 0) return;
  for (Waiting& wt : waiting_) {
    if (wt.busRef == 0) {
      wt.busRef = static_cast<uint32_t>(busRef);
      copyText(ref, wt.ref, sizeof(wt.ref));
      return;
    }
  }
}

// ---------------------------------------------------------------- events and log

void CloudService::onEvent(const Event& event) {
  if (!kernel_ || !bridge_) return;
  const uint32_t now = kernel_->now();
  if (up_ && forServer(event.id)) {
    // The same event again, as the percent of an update, takes the place of the one waiting.
    size_t i = 0;
    while (i < eventCount_ && !(events_[i].id == event.id && events_[i].a == event.a)) ++i;
    if (i < eventCount_) {
      events_[i] = event;
    } else if (eventCount_ < LMP_CLOUD_EVENT_QUEUE) {
      events_[eventCount_++] = event;
    } else {
      ++eventsLost_;
    }
  }
  uint16_t part = 0;
  switch (event.id) {
    case EventId::ServiceState: part = kServices; break;
    case EventId::ModeChanged:
    case EventId::NetApUp:
    case EventId::NetApDown:
    case EventId::NetStaConnected:
    case EventId::NetStaLost:
    case EventId::NetFallback:
    case EventId::NetNear:
    case EventId::InternetUp:
    case EventId::InternetDown: part = kNet; break;
    case EventId::TimeSynced: part = kClock; break;
    // What the service of the product adds is in the part of the device.
    case EventId::ProductChanged: part = kDevice; break;
    case EventId::ModuleState: part = kModules; break;
    case EventId::ConfigChanged:
      switch (event.a) {
        case Config::kSys: part = kDevice; break;
        case Config::kNet: part = kNet; break;
        case Config::kClock: part = kClock; break;
        case Config::kGeo: part = kGeo; break;
        case Config::kCloud:
          nextBeatMs_ = now + kernel_->config().cloud.heartbeatMin * 60000u;
          break;
        default: break;
      }
      break;
    case EventId::ModuleData:
      if (event.a < 32) {
        if (!moduleDocs_) docsAtMs_ = now + LMP_CLOUD_DATA_MS;
        moduleDocs_ |= 1u << event.a;
      }
      break;
    case EventId::CommandDone:
      for (Waiting& wt : waiting_) {
        if (wt.busRef != event.value || wt.done) continue;
        wt.done = true;
        wt.outcome = static_cast<Status>(event.b);
      }
      break;
    default: break;
  }
  if (part) {
    // Changes close together: one partial report, a moment later.
    if (!parts_) partsAtMs_ = now + LMP_CLOUD_PARTIAL_MS;
    parts_ |= part;
  }
}

// Any task. Only the number is kept: the entry is read back when it leaves.
void CloudService::onLog(const LogEntry& entry, const char* sourceName) {
  if (!bridge_ || !kernel_ || entry.source == src_) return;
  const uint8_t push = kernel_->config().cloud.pushLogLevel;
  const uint8_t level = static_cast<uint8_t>(entry.level);
  if (!push || level == 0 || level > push) return;
  hal::LockGuard guard(mutex_);
  if (logCount_ < LMP_CLOUD_LOG_QUEUE) logSeq_[logCount_++] = entry.seq;
}

// ---------------------------------------------------------------- document and commands

void CloudService::describe(JsonWriter& w) const {
  w.boolean("attached", bridge_ != nullptr);
  w.str("bridge", bridge_ ? bridge_->name() : "");
  w.boolean("connected", up_);
  if (up_ && kernel_) w.u32("upSec", (kernel_->now() - upSinceMs_) / 1000);
  w.u32("connections", connections_);
  w.u32("sent", sent_);
  w.u32("received", received_);
  w.u32("refused", refused_);
  w.u32("dropped", dropped_);
  w.u32("failed", failed_);
  w.u32("eventsLost", eventsLost_);
  if (kernel_) {
    w.u32("heartbeatMin", kernel_->config().cloud.heartbeatMin);
    w.u32("pushLogLevel", kernel_->config().cloud.pushLogLevel);
  }
  w.beginObject("pending");
  w.boolean("report", fullReport_);
  w.u32("parts", parts_);
  w.u32("modules", moduleDocs_);
  w.u32("logs", static_cast<uint32_t>(logCount_));
  w.u32("events", static_cast<uint32_t>(eventCount_));
  w.endObject();
  if (bridge_) {
    w.beginObject("detail");
    bridge_->describe(w);
    w.endObject();
  }
}

void CloudService::commands(CommandSet& set) {
  set.add("set", LMP_HELP("How often the report goes to the server, and which log entries"),
          kSetArgs,
          [this](const Args& a, CmdContext&) -> Status {
            if (a.count() == 0) return Status::Invalid;
            CloudConfig& cfg = kernel_->config().cloud;
            if (a.has("heartbeatMin")) cfg.heartbeatMin = a.u16("heartbeatMin");
            if (a.has("pushLogLevel")) cfg.pushLogLevel = a.u8("pushLogLevel");
            kernel_->config().touch(Config::kCloud);
            kernel_->events().publish(Event(EventId::ConfigChanged, Config::kCloud));
            return Status::Ok;
          });
  set.add("report", LMP_HELP("Sends the whole report to the server now"),
          [this](const Args&, CmdContext&) -> Status {
            if (!bridge_) return Status::Unsupported;
            if (!up_) return Status::Denied;
            fullReport_ = true;
            return Status::Ok;
          });
  set.add("checkUpdate", LMP_HELP("Asks the server now whether there is an update"),
          [this](const Args&, CmdContext&) -> Status {
            return bridge_ ? bridge_->checkUpdate() : Status::Unsupported;
          });
  set.add("update", LMP_HELP("Installs the update the server offers, then restarts"),
          [this](const Args&, CmdContext&) -> Status {
            return bridge_ ? bridge_->installUpdate() : Status::Unsupported;
          },
          kCmdConfirm);
}

}  // namespace lmp

#endif  // LMP_FEATURE_CLOUD
