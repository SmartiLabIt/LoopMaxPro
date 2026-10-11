#include "services/mqtt/home_bridge.h"

#if LMP_FEATURE_MQTT && LMP_FEATURE_MODULES

#include <stdio.h>
#include <string.h>

#include "hal/api/memory.h"
#include "lmp_version.h"
#include "sdk/home.h"
#include "services/modules/modules_service.h"
#include "services/mqtt/mqtt_service.h"
#include "services/net/net_service.h"

namespace lmp {
namespace {

// Messages for each round of the system: a hub with its nodes tells hundreds, a few at a time.
constexpr uint8_t kPerRound = 3;
constexpr size_t kConfigMax = 720;
constexpr size_t kStateMax = 512;
constexpr size_t kTopicMax = 160;

// A short fingerprint of some text, carried on from a previous one.
uint16_t mix(uint16_t h, const char* text) {
  uint32_t v = h ? h : 0x811Cu;
  for (const char* c = text ? text : ""; *c; ++c) v = ((v ^ static_cast<uint8_t>(*c)) * 0x0193u) & 0xFFFFu;
  // A separator: "ab" and "c" must not give what "a" and "bc" give.
  v = ((v ^ 0x1Fu) * 0x0193u) & 0xFFFFu;
  return static_cast<uint16_t>(v ? v : 1);
}

}  // namespace

// One walk through the things of a module. Every message it could send has a place in a row:
// the description of each thing, the state of each light, the values of each part. The walk
// starts at the place where the one before stopped, sends what its round allows, and says
// where it arrived. The module is asked its things again at every walk: nothing is kept.
class HomePass final : public sdk::ThingSink {
 public:
  HomePass(HomeBridge& bridge, uint8_t module, const sdk::ModuleManifest& manifest,
           bool describing, uint16_t from)
      : bridge_(bridge), module_(module), manifest_(manifest), describing_(describing), from_(from) {
    beginPart();
  }

  void part(const char* id, const char* name) override {
    endPart();
    snprintf(part_, sizeof(part_), "%s", id ? id : "");
    snprintf(partName_, sizeof(partName_), "%s", name ? name : "");
    sig_ = mix(mix(sig_, part_), partName_);
    beginPart();
  }

  void technical() override { technical_ = true; }

  void toggle(const char* id, sdk::ThingName name, bool on, const char* kind) override {
    thing("switch", id, name, nullptr, kind, 0, false);
    value(id, on ? "\"ON\"" : "\"OFF\"");
  }

  void flag(const char* id, sdk::ThingName name, bool on, const char* kind) override {
    thing("binary_sensor", id, name, nullptr, kind, 0, false);
    value(id, on ? "\"ON\"" : "\"OFF\"");
  }

  void number(const char* id, sdk::ThingName name, double number, const char* unit,
              const char* kind, uint8_t decimals, bool known) override {
    thing("sensor", id, name, unit, kind, decimals, true);
    char text[24] = "null";
    if (known) snprintf(text, sizeof(text), "%.*f", static_cast<int>(decimals), number);
    value(id, text);
  }

  void text(const char* id, sdk::ThingName name, const char* word) override {
    thing("sensor", id, name, nullptr, nullptr, 0, false);
    char quoted[48];
    size_t n = 0;
    quoted[n++] = '"';
    for (const char* c = word ? word : ""; *c && n + 3 < sizeof(quoted); ++c) {
      if (*c == '"' || *c == '\\' || static_cast<uint8_t>(*c) < 0x20) continue;
      quoted[n++] = *c;
    }
    quoted[n++] = '"';
    quoted[n] = '\0';
    value(id, quoted);
  }

  void button(const char* id, sdk::ThingName name) override {
    thing("button", id, name, nullptr, nullptr, 0, false);
  }

  void light(const char* id, sdk::ThingName name, bool on, uint8_t brightness, uint8_t r,
             uint8_t g, uint8_t b, bool colours) override {
    thing("light", id, name, nullptr, colours ? "rgb" : "brightness", 0, false);
    if (!place()) return;
    char topic[kTopicMax];
    snprintf(topic, sizeof(topic), "%s/home/%s/light/%s", bridge_.mqtt_->base(), manifest_.name,
             full(id));
    const unsigned level = brightness >= 100 ? 255u : (brightness * 255u + 50u) / 100u;
    const int n = snprintf(bridge_.config_, kConfigMax,
                           "{\"state\":\"%s\",\"brightness\":%u%s", on ? "ON" : "OFF", level ? level : 1,
                           colours ? ",\"color_mode\":\"rgb\"" : "");
    int m = 0;
    if (colours) {
      m = snprintf(bridge_.config_ + n, kConfigMax - static_cast<size_t>(n),
                   ",\"color\":{\"r\":%u,\"g\":%u,\"b\":%u}", static_cast<unsigned>(r),
                   static_cast<unsigned>(g), static_cast<unsigned>(b));
    }
    snprintf(bridge_.config_ + n + m, kConfigMax - static_cast<size_t>(n + m), "}");
    const uint16_t key = mix(mix(mix(static_cast<uint16_t>(module_ + 1), part_), id), "light");
    send(topic, bridge_.config_, key);
  }

  // After the last thing.
  void end() { endPart(); }

  bool finished() const { return !stopped_; }
  uint16_t reached() const { return at_; }
  uint16_t sig() const { return sig_; }
  uint16_t count() const { return count_; }

 private:
  // The id of a thing with its part in front: how it is written in a topic.
  const char* full(const char* id) {
    if (part_[0]) {
      snprintf(full_, sizeof(full_), "%s.%s", part_, id);
    } else {
      snprintf(full_, sizeof(full_), "%s", id);
    }
    return full_;
  }

  // True when the message at this place is for this walk. The place is taken either way.
  bool place() {
    if (stopped_) return false;
    if (at_ < from_) {
      ++at_;
      return false;
    }
    if (left_ == 0) {
      stopped_ = true;
      return false;
    }
    ++at_;
    return true;
  }

  // Sends when the value is new, or when everything is told again. A message that does not
  // go out stops the walk at its place: the next one starts from there.
  void send(const char* topic, const char* payload, uint16_t key) {
    const uint16_t hash = mix(0, payload);
    if (!describing_ && !bridge_.fresh(key, hash)) return;
    if (describing_) bridge_.fresh(key, hash);
    if (bridge_.mqtt_->publishAt(topic, StringView(payload), true) != Status::Ok) {
      bridge_.fresh(key, 0);   // not told: the next walk finds it new again
      --at_;
      stopped_ = true;
      return;
    }
    ++bridge_.sent_;
    --left_;
  }

  void beginPart() {
    stateLength_ = 0;
    stated_ = false;
    if (bridge_.state_) bridge_.state_[0] = '\0';
  }

  void value(const char* id, const char* text) {
    if (!bridge_.state_) return;
    const int n = snprintf(bridge_.state_ + stateLength_, kStateMax - stateLength_, "%s\"%s\":%s",
                           stated_ ? "," : "{", id, text);
    // What does not fit is left out whole: the values before it stay good.
    if (n < 0 || static_cast<size_t>(n) + 2 >= kStateMax - stateLength_) {
      bridge_.state_[stateLength_] = '\0';
      return;
    }
    stateLength_ += static_cast<size_t>(n);
    stated_ = true;
  }

  // The values of the part that ends here: one message for all of them.
  void endPart() {
    if (!stated_ || !bridge_.state_) return;
    if (!place()) return;
    snprintf(bridge_.state_ + stateLength_, kStateMax - stateLength_, "}");
    char topic[kTopicMax];
    stateTopic(topic, sizeof(topic));
    send(topic, bridge_.state_, mix(mix(static_cast<uint16_t>(module_ + 1), part_), "state"));
  }

  void stateTopic(char* out, size_t cap) const {
    if (part_[0]) {
      snprintf(out, cap, "%s/home/%s/%s/state", bridge_.mqtt_->base(), manifest_.name, part_);
    } else {
      snprintf(out, cap, "%s/home/%s/state", bridge_.mqtt_->base(), manifest_.name);
    }
  }

  // What a thing is, for Home Assistant: told when everything is told again.
  void thing(const char* component, const char* id, sdk::ThingName names, const char* unit,
             const char* kind, uint8_t decimals, bool measure) {
    // Home Assistant gets the name in the language of the device.
    const char* name = names.in(bridge_.kernel_->config().sys.lang.c_str());
    ++count_;
    // Asked for this thing only: taken here, whatever happens to its description.
    const bool apart = technical_;
    technical_ = false;
    sig_ = mix(mix(mix(mix(mix(mix(sig_, component), id), name), unit), kind), apart ? "t" : "");
    if (!describing_ || !place()) return;

    const char* base = bridge_.mqtt_->base();
    const char* node = bridge_.node_;
    char topic[kTopicMax];
    char unique[72];
    snprintf(unique, sizeof(unique), "%s_%s_%s%s%s", node, manifest_.name, part_, part_[0] ? "_" : "", id);
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s_%s%s%s/config",
             bridge_.kernel_->config().mqtt.homePrefix.c_str(), component, node, manifest_.name,
             part_, part_[0] ? "_" : "", id);
    char text[kTopicMax];

    BufferSink sink(bridge_.config_, kConfigMax);
    JsonWriter w(sink);
    w.beginObject();
    w.str("name", name && *name ? name : id);
    w.str("uniq_id", unique);
    snprintf(text, sizeof(text), "%s/status", base);
    w.str("avty_t", text);
    const bool light = strcmp(component, "light") == 0;
    const bool pressed = strcmp(component, "button") == 0;
    if (light) {
      w.str("schema", "json");
      snprintf(text, sizeof(text), "%s/home/%s/light/%s", base, manifest_.name, full(id));
      w.str("stat_t", text);
      w.boolean("brightness", true);
      w.beginArray("sup_clrm");
      w.str(StringView(kind ? kind : "brightness"));
      w.endArray();
    } else if (!pressed) {
      stateTopic(text, sizeof(text));
      w.str("stat_t", text);
      snprintf(text, sizeof(text), "{{ value_json.%s }}", id);
      w.str("val_tpl", text);
    }
    if (light || pressed || strcmp(component, "switch") == 0) {
      snprintf(text, sizeof(text), "%s/home/%s/set/%s", base, manifest_.name, full(id));
      w.str("cmd_t", text);
    }
    if (pressed) w.str("pl_prs", "PRESS");
    if (!light) {
      if (unit && *unit) w.str("unit_of_meas", unit);
      if (kind && *kind) w.str("dev_cla", kind);
      if (measure && unit && *unit) {
        w.str("stat_cla", "measurement");
        w.u32("sug_dsp_prc", decimals);
      }
    }
    // Kept apart by Home Assistant, and never offered to a voice assistant.
    if (apart) w.str("ent_cat", "diagnostic");
    // The device the thing belongs to: the device itself, or the part with its own name.
    const Config& config = bridge_.kernel_->config();
    w.beginObject("dev");
    w.beginArray("ids");
    if (part_[0]) {
      snprintf(text, sizeof(text), "%s_%s_%s", node, manifest_.name, part_);
      w.str(StringView(text));
    } else {
      w.str(StringView(node));
    }
    w.endArray();
    if (part_[0]) {
      w.str("name", partName_[0] ? partName_ : part_);
      w.str("via_device", node);
    } else {
      w.str("name", config.sys.deviceName.empty() ? config.sys.hostname.c_str()
                                                  : config.sys.deviceName.c_str());
      w.str("sw", LMP_VERSION);
#if LMP_FEATURE_NET
      const INetService* net = bridge_.kernel_->api<INetService>();
      if (net && net->connected()) {
        char address[16];
        net->address().toString(address, sizeof(address));
        snprintf(text, sizeof(text), "http://%s/", address);
        w.str("cu", text);
      }
#endif
    }
    w.str("mf", "SmartiLab");
    w.str("mdl", manifest_.title);
    w.endObject();
    w.endObject();
    if (!w.complete()) {
      // A description that does not fit is not sent: the walk goes on with the next thing.
      LMP_LOGW(bridge_.src_, "home: the description of %s.%s is too long", manifest_.name, id);
      return;
    }
    if (bridge_.mqtt_->publishAt(topic, sink.view(), true) != Status::Ok) {
      --at_;
      stopped_ = true;
      return;
    }
    ++bridge_.sent_;
    --left_;
  }

  HomeBridge& bridge_;
  bool technical_ = false;
  uint8_t module_;
  const sdk::ModuleManifest& manifest_;
  bool describing_;
  uint16_t from_;
  uint16_t at_ = 0;
  uint8_t left_ = kPerRound;
  bool stopped_ = false;
  uint16_t sig_ = 0;
  uint16_t count_ = 0;
  char part_[24] = "";
  char partName_[33] = "";
  char full_[56] = "";
  size_t stateLength_ = 0;
  bool stated_ = false;
};

// ---------------------------------------------------------------- the bridge

void HomeBridge::begin(Kernel& kernel, MqttService& mqtt, LogSource src) {
  kernel_ = &kernel;
  mqtt_ = &mqtt;
  src_ = src;
  current_ = -1;
  dirty_ = describe_ = 0;
  if (!node_[0] && kernel.board().sys) {
    hal::Mac mac;
    kernel.board().sys->mac(mac);
    snprintf(node_, sizeof(node_), "lmp_%02x%02x%02x%02x%02x%02x", mac.b[0], mac.b[1], mac.b[2],
             mac.b[3], mac.b[4], mac.b[5]);
  }
}

bool HomeBridge::enabled() const {
  return kernel_ && mqtt_ && kernel_->config().mqtt.home && node_[0];
}

bool HomeBridge::room() {
  if (!config_) config_ = static_cast<char*>(hal::allocLarge(kConfigMax + kStateMax));
  if (config_ && !state_) state_ = config_ + kConfigMax;
  return config_ != nullptr;
}

void HomeBridge::all() {
  const IModulesService* modules = kernel_ ? kernel_->api<IModulesService>() : nullptr;
  const size_t count = modules ? modules->count() : 0;
  const uint32_t mask = count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u);
  dirty_ = describe_ = mask;
  current_ = -1;
}

void HomeBridge::onUp() {
  toldCount_ = 0;
  things_ = 0;
  if (enabled()) all();
}

void HomeBridge::onEvent(const Event& event) {
  if (!enabled()) return;
  if (event.id == EventId::ModuleData) {
    if (event.a < 32) dirty_ |= 1u << event.a;
  } else if (event.id == EventId::ModuleState) {
    // A module that starts has its things to tell; one that stops has none any more.
    if (event.a < 32) {
      dirty_ |= 1u << event.a;
      describe_ |= 1u << event.a;
    }
  } else if (event.id == EventId::ConfigChanged &&
             (event.a == Config::kSys || event.a == Config::kMqtt)) {
    // The name of the device is in every description.
    all();
  }
}

bool HomeBridge::fresh(uint16_t key, uint16_t hash) {
  for (uint16_t i = 0; i < toldCount_; ++i) {
    if (told_[i].key != key) continue;
    if (told_[i].hash == hash) return false;
    told_[i].hash = hash;
    return true;
  }
  // No room to remember it: told every time.
  if (toldCount_ < LMP_HOME_STATES) {
    told_[toldCount_].key = key;
    told_[toldCount_].hash = hash;
    ++toldCount_;
  }
  return true;
}

void HomeBridge::tick() {
  if (!enabled() || !mqtt_->connected()) return;
  IModulesService* modules = kernel_->api<IModulesService>();
  if (!modules) return;
  if (current_ < 0) {
    if (!dirty_) return;
    for (uint8_t i = 0; i < 32; ++i) {
      if (dirty_ & (1u << i)) {
        current_ = static_cast<int8_t>(i);
        break;
      }
    }
    const uint32_t bit = 1u << current_;
    cursor_ = 0;
    describing_ = (describe_ & bit) != 0;
    // A change that arrives during the walk asks for another one.
    dirty_ &= ~bit;
    describe_ &= ~bit;
  }
  const char* name = modules->nameAt(static_cast<size_t>(current_));
  sdk::Module* module = name ? modules->find(name) : nullptr;
  const bool works = module && (module->state() == sdk::ModuleState::Ready ||
                                module->state() == sdk::ModuleState::Degraded);
  if (!works || !room()) {
    current_ = -1;
    return;
  }
  HomePass pass(*this, static_cast<uint8_t>(current_), module->manifest(), describing_, cursor_);
  module->thingsAll(pass);
  pass.end();
  cursor_ = pass.reached();
  if (!pass.finished()) return;   // the rest at the next round

  const uint32_t bit = 1u << current_;
  const size_t index = static_cast<size_t>(current_);
  if (index < kModules && pass.sig() != sigs_[index]) {
    // Its things changed, or their names: described again, unless this walk just did.
    sigs_[index] = pass.sig();
    if (!describing_) {
      dirty_ |= bit;
      describe_ |= bit;
    }
  }
  if (describing_ && pass.count()) {
    LMP_LOGI(src_, "home: %s told with %u things", module->manifest().name,
             static_cast<unsigned>(pass.count()));
  }
  if (describing_) things_ = static_cast<uint16_t>(things_ + pass.count());
  current_ = -1;
}

// "home/<module>/set/<thing>": what Home Assistant asks of a thing.
void HomeBridge::onMessage(StringView topic, StringView payload) {
  if (!enabled()) return;
  char path[96];
  topic.copyTo(path, sizeof(path));
  char* module = strchr(path, '/');
  if (!module) return;
  ++module;
  char* set = strchr(module, '/');
  if (!set || strncmp(set, "/set/", 5) != 0) return;
  *set = '\0';
  const char* thing = set + 5;
  if (!*thing) return;

  IModulesService* modules = kernel_->api<IModulesService>();
  sdk::Module* target = modules ? modules->find(module) : nullptr;
  if (!target) return;

  sdk::ThingAsk ask;
  if (payload == StringView("ON") || payload == StringView("OFF")) {
    ask.hasOn = true;
    ask.on = payload == StringView("ON");
  } else if (payload == StringView("PRESS")) {
    ask.press = true;
  } else if (payload.size && payload[0] == '{') {
    // A light, as Home Assistant writes it: state, brightness up to 255, colour.
    const JsonValue json = JsonValue::parse(payload);
    if (!json.valid()) return;
    if (json["state"].valid()) {
      ask.hasOn = true;
      ask.on = json["state"].stringEquals("ON");
    }
    if (json["brightness"].valid()) {
      const int64_t level = json["brightness"].asInt(255);
      const int64_t percent = (level * 100 + 127) / 255;
      ask.hasBrightness = true;
      ask.brightness = static_cast<uint8_t>(percent < 1 ? 1 : (percent > 100 ? 100 : percent));
    }
    if (json["color"].valid()) {
      ask.hasColour = true;
      ask.r = static_cast<uint8_t>(json["color"]["r"].asInt(0));
      ask.g = static_cast<uint8_t>(json["color"]["g"].asInt(0));
      ask.b = static_cast<uint8_t>(json["color"]["b"].asInt(0));
    }
  } else {
    return;
  }
  ++asked_;
  const Status s = target->thingSetAll(thing, ask);
  if (s != Status::Ok) {
    LMP_LOGW(src_, "home: %s of %s not done: %s", thing, module, statusName(s));
  }
  // Told again in any case: Home Assistant must see what the thing is now, also when the
  // device did not do what it asked.
  for (size_t i = 0; i < modules->count() && i < 32; ++i) {
    if (modules->nameAt(i) && strcmp(modules->nameAt(i), module) == 0) dirty_ |= 1u << i;
  }
  // A value that went back to what it was is a new value for who asked for another one.
  toldCount_ = 0;
}

void HomeBridge::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const MqttSettings& cfg = kernel_->config().mqtt;
  w.beginObject("home");
  w.boolean("enabled", cfg.home);
  w.str("prefix", cfg.homePrefix.c_str());
  w.str("node", node_);
  w.u32("things", things_);
  w.u32("sent", sent_);
  w.u32("asked", asked_);
  w.endObject();
}

}  // namespace lmp

#endif  // LMP_FEATURE_MQTT && LMP_FEATURE_MODULES
