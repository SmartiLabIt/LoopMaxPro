#include "services/mqtt/mqtt_service.h"

#if LMP_FEATURE_MQTT

#include <stdio.h>
#include <string.h>

#include <new>

#include "services/net/net_service.h"

namespace lmp {
namespace {

constexpr ArgSpec kSetArgs[] = {
    Arg::boolean("enabled").optional(),
    Arg::str("host", 0, 64).optional(),
    Arg::u16("port", 1, 65535).optional(),
    Arg::boolean("tls").optional(),
    Arg::str("user", 0, 32).optional(),
    Arg::secret("psw", 0, 63).optional(),
};

constexpr ArgSpec kTopicArgs[] = {
    Arg::str("base", 0, 48).optional(),
    Arg::u16("keepAliveSec", 10, 3600).optional(),
    Arg::boolean("events").optional(),
    Arg::boolean("home").optional(),
    Arg::str("homePrefix", 1, 24).optional(),
};

constexpr ArgSpec kPublishArgs[] = {
    Arg::str("topic", 1, 64),
    Arg::str("payload", 0, 160).optional(),
    Arg::boolean("retain").optional(),
};

// Names the system keeps for itself, right after the base.
const char* const kReserved[] = {"cmd", "ack", "get", "doc", "evt", "status"};

// A part of a topic the device can publish on: no wildcard, no empty level.
bool validTopic(StringView topic) {
  if (topic.empty() || topic[0] == '/' || topic[topic.size - 1] == '/') return false;
  for (size_t i = 0; i < topic.size; ++i) {
    const char c = topic[i];
    if (c == '#' || c == '+' || static_cast<unsigned char>(c) < 0x20) return false;
    if (c == '/' && i + 1 < topic.size && topic[i + 1] == '/') return false;
  }
  return true;
}

bool reserved(StringView topic) {
  const size_t slash = topic.find('/');
  const StringView first = slash == StringView::npos ? topic : topic.substr(0, slash);
  for (const char* name : kReserved) {
    if (first == StringView(name)) return true;
  }
  return false;
}

}  // namespace

const ServiceInfo& MqttService::info() const {
  static const ServiceInfo kInfo{ServiceId::Mqtt, "mqtt", "\xF0\x9F\x93\xAD", Mode::Lan,
                                 RestartPolicy::Retry, 100,
                                 DepList(ServiceId::Net, ServiceId::Config, ServiceId::Log)};
  return kInfo;
}

Status MqttService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  mqtt_ = kernel.board().mqtt;
  if (!mqtt_) {
    fail("no MQTT on this board");
    return Status::Unsupported;
  }
  if (!mutex_ && kernel.board().rtos) mutex_ = kernel.board().rtos->createMutex();
  mqtt_->onMessage([this](StringView topic, StringView payload) { onMessage(topic, payload); });
  up_ = false;
  wanted_ = false;
  head_ = count_ = 0;
  lastError_.clear();
#if LMP_FEATURE_MODULES
  home_.begin(kernel, *this, src_);
  // Asked once: the list of who listens stays through the restarts of the service.
  if (!homeListens_) {
    homeListens_ = listen("home", [this](StringView topic, StringView payload) {
                     home_.onMessage(topic, payload);
                   }) == Status::Ok;
  }
#endif

  const INetService* net = kernel.api<INetService>();
  online_ = net && net->connected();
  apply();
  return Status::Ok;
}

void MqttService::stop() {
  leave();
  if (mqtt_) mqtt_->onMessage(hal::MqttMessageHandler());
}

// ---------------------------------------------------------------- connection

void MqttService::leave() {
  if (!mqtt_) return;
  if (up_) {
    // Said before leaving: the broker says it by itself only when the device disappears.
    send("status", StringView("offline"), true);
    kernel_->events().publish(Event(EventId::MqttLost));
  }
  if (wanted_ || up_) mqtt_->disconnect();
  wanted_ = false;
  up_ = false;
}

bool MqttService::roomReady() {
  if (!inbox_) inbox_ = new (std::nothrow) Message[LMP_MQTT_INBOX];
  if (!out_) out_ = new (std::nothrow) char[kOutMax];
  return inbox_ && out_;
}

void MqttService::apply() {
  const MqttSettings& cfg = kernel_->config().mqtt;
  leave();
  if (!cfg.enabled) {
    recover();   // turned off by the settings: nothing is wrong
    return;
  }
  if (cfg.host.empty()) {
    degrade("no broker in the settings");
    return;
  }
  if (!roomReady()) {
    degrade("no memory");
    return;
  }
  if (!online_) {
    degrade("no network");
    return;
  }

  const SysConfig& sys = kernel_->config().sys;
  if (cfg.base.empty()) {
    base_.format("lmp/%s", sys.hostname.c_str());
  } else {
    base_ = cfg.base;
  }
  clientId_.format("lmp-%s", sys.hostname.c_str());
  will_.format("%s/status", base_.c_str());

  hal::MqttConfig wire;
  wire.host = cfg.host.c_str();
  wire.port = cfg.port;
  wire.clientId = clientId_.c_str();
  wire.username = cfg.user.empty() ? nullptr : cfg.user.c_str();
  wire.password = cfg.psw.empty() ? nullptr : cfg.psw.c_str();
  wire.willTopic = will_.c_str();
  wire.willMessage = "offline";
  wire.keepAliveSec = cfg.keepAliveSec;
  wire.useTls = cfg.tls;

  Status s = mqtt_->configure(wire);
  if (s == Status::Ok) s = mqtt_->connect();
  if (s != Status::Ok) {
    lastError_.assign(statusName(s));
    degrade("the broker cannot be asked");
    LMP_LOGW(src_, "%s:%u not asked: %s", cfg.host.c_str(), static_cast<unsigned>(cfg.port),
             statusName(s));
    return;
  }
  wanted_ = true;
  degrade("connecting");
  LMP_LOGI(src_, "connecting to %s:%u as %s", cfg.host.c_str(), static_cast<unsigned>(cfg.port),
           clientId_.c_str());
}

void MqttService::onUp() {
  up_ = true;
  upSinceMs_ = kernel_->now();
  ++connections_;
  lastError_.clear();
  recover();
  LMP_LOGI(src_, "connected, topics under %s", base_.c_str());

  // Asked again at every connection: the broker forgets.
  snprintf(topic_, sizeof(topic_), "%s/cmd/#", base_.c_str());
  mqtt_->subscribe(topic_, 1);
  snprintf(topic_, sizeof(topic_), "%s/get/#", base_.c_str());
  mqtt_->subscribe(topic_, 0);
  for (size_t i = 0; i < listenerCount_; ++i) {
    snprintf(topic_, sizeof(topic_), "%s/%s/#", base_.c_str(), listeners_[i].prefix);
    mqtt_->subscribe(topic_, 0);
  }
  send("status", StringView("online"), true);
#if LMP_FEATURE_MODULES
  home_.onUp();
#endif
  kernel_->events().publish(Event(EventId::MqttConnected));
}

void MqttService::tick(uint32_t) {
  if (!mqtt_) return;
  mqtt_->poll();

  const bool now = wanted_ && mqtt_->connected();
  if (now && !up_) {
    onUp();
  } else if (!now && up_) {
    up_ = false;
    lastError_.assign(mqtt_->lastError());
    degrade("connection lost");
    LMP_LOGW(src_, "connection lost: %s", lastError_.c_str());
    kernel_->events().publish(Event(EventId::MqttLost));
  } else if (wanted_ && !up_) {
    // Still trying: the reason of the last failure is said once.
    const char* why = mqtt_->lastError();
    if (*why && !(lastError_ == why)) {
      lastError_.assign(why);
      LMP_LOGW(src_, "not connected: %s", why);
    }
  }

#if LMP_FEATURE_MODULES
  if (up_) home_.tick();
#endif

  // One message for each round: a command can take a while.
  Message m;
  {
    hal::LockGuard guard(mutex_);
    if (count_ == 0) return;
    m = inbox_[head_];
    head_ = (head_ + 1) % LMP_MQTT_INBOX;
    --count_;
  }
  handle(m);
}

void MqttService::onEvent(const Event& event) {
  if (!kernel_ || !mqtt_) return;
  if (event.id == EventId::NetStaConnected) {
    online_ = true;
    apply();
  } else if (event.id == EventId::NetStaLost) {
    online_ = false;
    apply();
  } else if (event.id == EventId::ConfigChanged && event.a == Config::kMqtt) {
    apply();
  } else if (event.id == EventId::ConfigChanged && event.a == Config::kSys && wanted_) {
    // The host name is part of the topics and of the name given to the broker: a new one
    // means a new connection. Any other change of those settings leaves it alone.
    FixedString<32> name;
    name.format("lmp-%s", kernel_->config().sys.hostname.c_str());
    if (!(name == clientId_)) apply();
  }

#if LMP_FEATURE_MODULES
  home_.onEvent(event);
#endif

  if (!up_ || !out_ || !kernel_->config().mqtt.events) return;
  // What this service says about itself would go round in circles.
  if (event.id == EventId::MqttConnected || event.id == EventId::MqttLost) return;
  BufferSink sink(out_, kOutMax);
  JsonWriter w(sink);
  w.beginObject();
  w.str("id", eventName(event.id));
  w.u32("a", event.a);
  w.u32("b", event.b);
  w.u32("value", event.value);
  w.endObject();
  if (w.complete()) send("evt", sink.view(), false);
}

// ---------------------------------------------------------------- messages

// Context of the driver: the message is copied and waits for the round of the system.
void MqttService::onMessage(StringView topic, StringView payload) {
  hal::LockGuard guard(mutex_);
  if (!inbox_ || topic.size >= LMP_MQTT_TOPIC_MAX || count_ >= LMP_MQTT_INBOX) {
    ++dropped_;
    return;
  }
  Message& m = inbox_[(head_ + count_) % LMP_MQTT_INBOX];
  topic.copyTo(m.topic, sizeof(m.topic));
  m.cut = payload.size >= sizeof(m.payload);
  m.length = static_cast<uint16_t>(payload.copyTo(m.payload, sizeof(m.payload)));
  ++count_;
}

void MqttService::handle(const Message& m) {
  ++received_;
  const StringView topic(m.topic);
  // Only what is under the base of this device.
  if (!topic.startsWith(base_.view()) || topic.size <= base_.size() + 1 ||
      topic[base_.size()] != '/') {
    return;
  }
  const StringView rest = topic.substr(base_.size() + 1);
  const StringView payload(m.payload, m.length);

  if (rest.startsWith("cmd/")) {
    if (m.cut) {
      LMP_LOGW(src_, "command too long: not run");
      return;
    }
    runCommand(rest.substr(4), payload);
    return;
  }
  if (rest.startsWith("get/")) {
    sendDocument(rest.substr(4), payload);
    return;
  }
  for (size_t i = 0; i < listenerCount_; ++i) {
    const StringView prefix(listeners_[i].prefix);
    if (rest == prefix || (rest.startsWith(prefix) && rest[prefix.size] == '/')) {
      listeners_[i].call(rest, payload);
    }
  }
}

void MqttService::runCommand(StringView rest, StringView payload) {
  // "<target>/<command>"
  const size_t slash = rest.find('/');
  if (slash == StringView::npos || slash == 0 || slash + 1 >= rest.size) return;
  const StringView target = rest.substr(0, slash);
  const StringView command = rest.substr(slash + 1);
  if (command.find('/') != StringView::npos) return;

  BufferSink sink(out_, kOutMax);
  JsonWriter w(sink);
  kernel_->commands().answer(target, command, payload, ArgsForm::Json, CmdSource::Mqtt, w);
  if (!w.complete()) {
    sink.clear();
    JsonWriter again(sink);
    again.beginObject();
    again.boolean("ok", false);
    again.str("status", statusName(Status::NoMem));
    again.endObject();
  }
  snprintf(topic_, sizeof(topic_), "ack/%.*s", static_cast<int>(rest.size), rest.data);
  send(topic_, sink.view(), false);
}

void MqttService::sendDocument(StringView target, StringView payload) {
  if (target.empty() || target.find('/') != StringView::npos) return;
  BufferSink sink(out_, kOutMax);
  JsonWriter w(sink);
  if (target == StringView("report")) {
    // The content of the request names the parts wanted: "system,net". Empty: all.
    kernel_->report(w, payload, true);
  } else {
    w.beginObject();
    if (kernel_->commands().describe(target, w) != Status::Ok) {
      w.str("error", "not_found");
    }
    w.endObject();
  }
  if (!w.complete()) {
    sink.clear();
    JsonWriter again(sink);
    again.beginObject();
    again.str("error", "too_large");
    again.endObject();
  }
  snprintf(topic_, sizeof(topic_), "doc/%.*s", static_cast<int>(target.size), target.data);
  send(topic_, sink.view(), false);
}

Status MqttService::send(const char* topic, StringView payload, bool retain) {
  if (!mqtt_ || !up_) return Status::Denied;
  char full[LMP_MQTT_TOPIC_MAX + 48];
  const int n = snprintf(full, sizeof(full), "%s/%s", base_.c_str(), topic);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(full)) return Status::Invalid;
  const Status s = mqtt_->publish(full, payload, retain ? 1 : 0, retain);
  if (s == Status::Ok) ++sent_;
  return s;
}

Status MqttService::publishAt(const char* topic, StringView payload, bool retain) {
  if (!mqtt_ || !up_) return Status::Denied;
  if (!topic || !validTopic(StringView(topic))) return Status::Invalid;
  const Status s = mqtt_->publish(topic, payload, retain ? 1 : 0, retain);
  if (s == Status::Ok) ++sent_;
  return s;
}

Status MqttService::publish(const char* topic, StringView payload, bool retain) {
  if (!topic || !validTopic(StringView(topic)) || reserved(StringView(topic))) {
    return Status::Invalid;
  }
  return send(topic, payload, retain);
}

Status MqttService::listen(const char* prefix, MqttListener listener) {
  if (!prefix || !listener || !validTopic(StringView(prefix)) || reserved(StringView(prefix))) {
    return Status::Invalid;
  }
  if (listenerCount_ >= LMP_MQTT_LISTENERS) return Status::NoMem;
  listeners_[listenerCount_].prefix = prefix;
  listeners_[listenerCount_].call = listener;
  ++listenerCount_;
  if (up_) {
    snprintf(topic_, sizeof(topic_), "%s/%s/#", base_.c_str(), prefix);
    mqtt_->subscribe(topic_, 0);
  }
  return Status::Ok;
}

// ---------------------------------------------------------------- documents and commands

void MqttService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const MqttSettings& cfg = kernel_->config().mqtt;
  w.boolean("available", mqtt_ != nullptr);
  w.boolean("enabled", cfg.enabled);
  w.boolean("connected", up_);
  w.str("host", cfg.host.c_str());
  w.u32("port", cfg.port);
  w.boolean("tls", cfg.tls);
  w.str("user", cfg.user.c_str());
  w.boolean("pswSet", !cfg.psw.empty());
  w.str("base", base_.empty() ? cfg.base.c_str() : base_.c_str());
  w.boolean("events", cfg.events);
  if (up_) w.u32("upSec", (kernel_->now() - upSinceMs_) / 1000);
  w.u32("connections", connections_);
  w.u32("sent", sent_);
  w.u32("received", received_);
  w.u32("dropped", dropped_);
  w.str("error", lastError_.c_str());
#if LMP_FEATURE_MODULES
  home_.describe(w);
#endif
}

void MqttService::commands(CommandSet& set) {
  set.add("set",
          LMP_HELP("Broker, name and password. Connects again with the new values"), kSetArgs,
          [this](const Args& a, CmdContext&) -> Status {
            if (a.count() == 0) return Status::Invalid;
            MqttSettings& cfg = kernel_->config().mqtt;
            if (a.has("enabled")) cfg.enabled = a.boolean("enabled");
            if (a.has("host")) cfg.host.assign(a.str("host"));
            if (a.has("port")) cfg.port = a.u16("port");
            if (a.has("tls")) cfg.tls = a.boolean("tls");
            if (a.has("user")) cfg.user.assign(a.str("user"));
            if (a.has("psw")) cfg.psw.assign(a.str("psw"));
            kernel_->config().touch(Config::kMqtt);
            kernel_->events().publish(Event(EventId::ConfigChanged, Config::kMqtt));
            return Status::Ok;
          });

  set.add("setTopics",
          LMP_HELP("First part of the topics, keep alive, events published or not, Home "
                   "Assistant told or not and where it looks"),
          kTopicArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (a.count() == 0) return Status::Invalid;
            MqttSettings& cfg = kernel_->config().mqtt;
            if (a.has("base")) {
              const char* base = a.str("base");
              if (*base && !validTopic(StringView(base))) {
                ctx.errorField = "base";
                return Status::Invalid;
              }
              cfg.base.assign(base);
            }
            if (a.has("keepAliveSec")) cfg.keepAliveSec = a.u16("keepAliveSec");
            if (a.has("events")) cfg.events = a.boolean("events");
            if (a.has("home")) cfg.home = a.boolean("home");
            if (a.has("homePrefix")) {
              if (!validTopic(StringView(a.str("homePrefix")))) {
                ctx.errorField = "homePrefix";
                return Status::Invalid;
              }
              cfg.homePrefix.assign(a.str("homePrefix"));
            }
            kernel_->config().touch(Config::kMqtt);
            kernel_->events().publish(Event(EventId::ConfigChanged, Config::kMqtt));
            return Status::Ok;
          });

  set.add("reconnect", LMP_HELP("Leaves the broker and connects again"),
          [this](const Args&, CmdContext&) -> Status {
            if (!mqtt_) return Status::Unsupported;
            apply();
            return wanted_ ? Status::Ok : Status::Denied;
          });

  set.add("publish", LMP_HELP("Sends a message under the base of the device"), kPublishArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Status s =
                publish(a.str("topic"), StringView(a.str("payload")), a.boolean("retain"));
            if (s == Status::Invalid) ctx.errorField = "topic";
            return s;
          });
}

}  // namespace lmp

#endif  // LMP_FEATURE_MQTT
