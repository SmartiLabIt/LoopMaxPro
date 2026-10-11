// LoopMaxPro - the bridge towards Home Assistant, on the broker.
// It tells Home Assistant what the modules of the device show (MQTT discovery), keeps their
// values up to date and takes what Home Assistant asks. The modules know nothing of it: they
// tell their things (sdk/home.h), and a part of a module with a name of its own, as a node
// of a hub, becomes a device of its own.
//
//   <prefix>/<kind>/<node>/<module>_<thing>/config   out, kept: what a thing is
//   <base>/home/<module>/state                       out, kept: the values of its things, JSON
//   <base>/home/<module>/<part>/state                the same, for a part
//   <base>/home/<module>/light/<thing>               out, kept: a light, as Home Assistant reads it
//   <base>/home/<module>/set/<thing>                 in: ON, OFF, PRESS, or the JSON of a light
//
// <prefix> is "homeassistant" unless the settings say otherwise; <node> is "lmp_" and the MAC
// address of the device. Everything is kept by the broker: Home Assistant finds it again after
// its own restarts, and <base>/status tells it when the device is not there.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_MQTT && LMP_FEATURE_MODULES

#include "kernel/kernel.h"

namespace lmp {

class MqttService;

class HomeBridge {
 public:
  void begin(Kernel& kernel, MqttService& mqtt, LogSource src);
  // The broker is there: everything is told again, from what the things are.
  void onUp();
  void onEvent(const Event& event);
  // A few messages for each round: a hub tells hundreds of them.
  void tick();
  // A message under <base>/home/: topic is what follows the base.
  void onMessage(StringView topic, StringView payload);
  void describe(JsonWriter& w) const;

 private:
  friend class HomePass;
  static constexpr size_t kModules = 16;
  // The last value told of each state: what did not change is not told again.
  struct Told {
    uint16_t key = 0;
    uint16_t hash = 0;
  };

  bool enabled() const;
  bool room();
  void all();
  // True when the value is new: told now, and remembered.
  bool fresh(uint16_t key, uint16_t hash);

  Kernel* kernel_ = nullptr;
  MqttService* mqtt_ = nullptr;
  LogSource src_ = kLogSys;
  char node_[20] = "";         // "lmp_" and the MAC address
  char* config_ = nullptr;     // room for a description; taken at the first use, and kept
  char* state_ = nullptr;      // room for the values of a part

  uint32_t dirty_ = 0;         // modules whose values are to be told
  uint32_t describe_ = 0;      // modules whose things are to be described again
  int8_t current_ = -1;        // the module being told
  bool describing_ = false;
  uint16_t cursor_ = 0;        // how far in its messages
  uint16_t sigs_[kModules] = {};
  uint16_t things_ = 0;        // things of the modules told so far
  uint32_t sent_ = 0;
  uint32_t asked_ = 0;         // what Home Assistant asked
  Told told_[LMP_HOME_STATES];
  uint16_t toldCount_ = 0;
};

}  // namespace lmp

#endif  // LMP_FEATURE_MQTT && LMP_FEATURE_MODULES
