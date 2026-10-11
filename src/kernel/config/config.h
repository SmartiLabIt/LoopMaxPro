// LoopMaxPro - system settings.
// Native structs in memory, one key/value entry for each field in storage. No JSON inside.
// The settings of the modules are not here: each module has its own file.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/fixed_string.h"
#include "base/status.h"
#include "hal/api/icrypto.h"
#include "hal/api/istorage.h"
#include "hal/api/isystem.h"
#include "kernel/fields/fields.h"
#include "kernel/service/service.h"
#include "lmp_config.h"

namespace lmp {

constexpr uint16_t kConfigSchema = 2;
constexpr size_t kDeviceKeyLength = 30;
// How the access point of a device is called when it is born: this, and the end of its MAC
// address. Who looks for the devices that are not in a network yet knows them by it.
constexpr char kApNamePrefix[] = "LoopMax-";

struct SysConfig {
  FixedString<32> key;         // identity of the device: created once, never changed
  FixedString<32> hostname;
  FixedString<32> deviceName;
  FixedString<2> lang;
  uint8_t mode;                // 1 = AP, 2 = LAN
  uint16_t schema;
};

struct NetConfig {
  FixedString<32> apSsid;
  FixedString<63> apPsw;
  FixedString<32> staSsid;
  FixedString<63> staPsw;
  FixedString<32> prevSsid;    // network to go back to when a change fails
  FixedString<63> prevPsw;
  bool captive;
  uint16_t rollbackSec;
  FixedString<96> probeUrl;    // used to check that Internet is reachable
};

struct WebConfig {
  FixedString<63> adminPsw;    // empty: the access point password is used
  uint16_t sessionTtlMin;
};

struct ClockConfig {
  FixedString<40> tz;          // IANA name, for example Europe/Rome
  FixedString<48> posixTz;     // what the C library needs
  FixedString<48> ntpServer;
};

struct CloudConfig {
  uint16_t heartbeatMin;       // 0 = no periodic report
  uint8_t pushLogLevel;        // 0 = logs are sent only on request
};

struct MqttSettings {
  bool enabled;
  FixedString<64> host;
  uint16_t port;
  bool tls;
  FixedString<32> user;
  FixedString<63> psw;
  FixedString<48> base;        // first part of every topic; empty: lmp/<host name>
  uint16_t keepAliveSec;
  bool events;                 // the events of the system are published too
  bool home;                   // the modules are told to Home Assistant (MQTT discovery)
  FixedString<24> homePrefix;  // where Home Assistant looks for them
};

struct GeoConfig {
  double lat;
  double lon;
  FixedString<40> city;
  FixedString<40> region;
  FixedString<2> country;      // two letters, as IT
  uint8_t source;              // 0 not known, 1 by hand, 2 asked to a server, 3 from the cloud, 4 GPS
  FixedString<96> lookupUrl;   // server that tells where the device is; empty: nobody is asked
  bool named;                  // city, region and country were written by hand: the network keeps them
  bool asked;                  // the console asked the owner where the device is, once
};

class Config {
 public:
  enum Section : uint8_t { kSys, kNet, kWeb, kClock, kCloud, kMqtt, kGeo, kSectionCount };

  // Every pointer can be null on boards without that capability: the settings then live
  // in memory only.
  Config(hal::IKv* kv, hal::IRandom* rng, hal::ISystem* sys);

  // Reads the stored values. Missing values get their default.
  // At the first start creates the device key and the default names, then saves.
  Status load();
  // Writes the values that differ from the stored ones.
  Status save();
  // Back to the defaults. The device key is kept.
  Status factoryReset();

  // Marks a section as changed. The write happens later, from tick().
  void touch(Section section);
  bool dirty() const { return dirtyMask_ != 0; }
  void tick(uint32_t nowMs);

  bool loaded() const { return loaded_; }
  bool firstBoot() const { return firstBoot_; }
  Mode mode() const { return sys.mode == 2 ? Mode::Lan : Mode::Ap; }

  // Generic access, used by documents and commands.
  static const char* sectionName(Section section);
  static bool findSection(const char* name, Section& out);
  Span<const FieldDesc> fields(Section section) const;
  // Members of the section. Secrets are replaced by "<key>Set": true or false.
  void describe(JsonWriter& w, Section section) const;
  // Applies the members found in the JSON object. Nothing changes when a value is invalid.
  Status apply(Section section, const JsonValue& json, FieldError* error = nullptr);

  SysConfig sys;
  NetConfig net;
  WebConfig web;
  ClockConfig clock;
  CloudConfig cloud;
  MqttSettings mqtt;
  GeoConfig geo;

 private:
  void* object(Section section);
  const void* object(Section section) const;
  void defaults();
  void fillIdentity();
  Status loadSection(Section section);
  Status saveSection(Section section);

  hal::IKv* kv_;
  hal::IRandom* rng_;
  hal::ISystem* system_;
  uint8_t dirtyMask_ = 0;
  uint32_t dirtySinceMs_ = 0;
  bool dirtyStamped_ = false;
  bool loaded_ = false;
  bool firstBoot_ = false;
};

}  // namespace lmp
