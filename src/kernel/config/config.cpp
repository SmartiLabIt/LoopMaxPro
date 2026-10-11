#include "kernel/config/config.h"

#include <stdio.h>
#include <string.h>

#include "kernel/config/kv_fields.h"
#include "kernel/log/logger.h"

// Key names are limited to 15 characters by the NVS of the ESP32.
LMP_FIELDS(lmp::SysConfig,
  LMP_F(key,        "key",        Str(0, 32, "").secret().readOnly()),
  LMP_F(hostname,   "hostname",   Str(1, 32, "")),
  LMP_F(deviceName, "deviceName", Str(0, 32, "")),
  LMP_F(lang,       "lang",       Str(2, 2, "en")),
  LMP_F(mode,       "mode",       U8(1, 2, 1)),
  LMP_F(schema,     "schema",     U16(0, 65535, lmp::kConfigSchema).readOnly()))

LMP_FIELDS(lmp::NetConfig,
  LMP_F(apSsid,      "apSsid",      Str(1, 32, "")),
  LMP_F(apPsw,       "apPsw",       Str(8, 63, "loopmaxap").secret()),
  LMP_F(staSsid,     "staSsid",     Str(0, 32, "")),
  LMP_F(staPsw,      "staPsw",      Str(0, 63, "").secret()),
  LMP_F(prevSsid,    "prevSsid",    Str(0, 32, "").readOnly()),
  LMP_F(prevPsw,     "prevPsw",     Str(0, 63, "").secret().readOnly()),
  LMP_F(captive,     "captive",     Bool(true)),
  LMP_F(rollbackSec, "rollbackSec", U16(30, 3600, 180)),
  LMP_F(probeUrl,    "probeUrl",    Str(0, 96, "http://connectivitycheck.gstatic.com/generate_204")))

LMP_FIELDS(lmp::WebConfig,
  LMP_F(adminPsw,      "adminPsw",      Str(0, 63, "").secret()),
  LMP_F(sessionTtlMin, "sessionTtlMin", U16(1, 43200, 1440)))

LMP_FIELDS(lmp::ClockConfig,
  LMP_F(tz,        "tz",        Str(0, 40, "UTC")),
  LMP_F(posixTz,   "posixTz",   Str(0, 48, "UTC0")),
  LMP_F(ntpServer, "ntpServer", Str(0, 48, "pool.ntp.org")))

LMP_FIELDS(lmp::CloudConfig,
  LMP_F(heartbeatMin, "heartbeatMin", U16(0, 1440, 15)),
  LMP_F(pushLogLevel, "pushLogLevel", U8(0, 5, 0)))

LMP_FIELDS(lmp::MqttSettings,
  LMP_F(enabled,      "enabled",      Bool(false)),
  LMP_F(host,         "host",         Str(0, 64, "")),
  LMP_F(port,         "port",         U16(1, 65535, 8883)),
  LMP_F(tls,          "tls",          Bool(true)),
  LMP_F(user,         "user",         Str(0, 32, "")),
  LMP_F(psw,          "psw",          Str(0, 63, "").secret()),
  LMP_F(base,         "base",         Str(0, 48, "")),
  LMP_F(keepAliveSec, "keepAliveSec", U16(10, 3600, 60)),
  LMP_F(events,       "events",       Bool(true)),
  LMP_F(home,         "home",         Bool(true)),
  LMP_F(homePrefix,   "homePrefix",   Str(1, 24, "homeassistant")))

LMP_FIELDS(lmp::GeoConfig,
  LMP_F(lat,       "lat",       F64(-90, 90, 0)),
  LMP_F(lon,       "lon",       F64(-180, 180, 0)),
  LMP_F(city,      "city",      Str(0, 40, "")),
  LMP_F(region,    "region",    Str(0, 40, "")),
  LMP_F(country,   "country",   Str(0, 2, "")),
  LMP_F(source,    "source",    U8(0, 4, 0).readOnly()),
  LMP_F(lookupUrl, "lookupUrl", Str(0, 96, LMP_GEO_LOOKUP_URL)),
  LMP_F(named,     "named",     Bool(false).readOnly()),
  LMP_F(asked,     "asked",     Bool(false)))

namespace lmp {
namespace {

const char* const kSectionNames[Config::kSectionCount] = {"sys",   "net",  "web", "clock",
                                                         "cloud", "mqtt", "geo"};

}  // namespace

Config::Config(hal::IKv* kv, hal::IRandom* rng, hal::ISystem* sys)
    : kv_(kv), rng_(rng), system_(sys) {
  defaults();
}

// ---------------------------------------------------------------- sections

const char* Config::sectionName(Section section) {
  return section < kSectionCount ? kSectionNames[section] : "";
}

bool Config::findSection(const char* name, Section& out) {
  if (!name) return false;
  for (uint8_t i = 0; i < kSectionCount; ++i) {
    if (strcmp(kSectionNames[i], name) == 0) {
      out = static_cast<Section>(i);
      return true;
    }
  }
  return false;
}

Span<const FieldDesc> Config::fields(Section section) const {
  switch (section) {
    case kSys:   return fieldsOf<SysConfig>();
    case kNet:   return fieldsOf<NetConfig>();
    case kWeb:   return fieldsOf<WebConfig>();
    case kClock: return fieldsOf<ClockConfig>();
    case kCloud: return fieldsOf<CloudConfig>();
    case kMqtt:  return fieldsOf<MqttSettings>();
    case kGeo:   return fieldsOf<GeoConfig>();
    case kSectionCount: break;
  }
  return Span<const FieldDesc>();
}

void* Config::object(Section section) {
  switch (section) {
    case kSys:   return &sys;
    case kNet:   return &net;
    case kWeb:   return &web;
    case kClock: return &clock;
    case kCloud: return &cloud;
    case kMqtt:  return &mqtt;
    case kGeo:   return &geo;
    case kSectionCount: break;
  }
  return nullptr;
}

const void* Config::object(Section section) const {
  return const_cast<Config*>(this)->object(section);
}

void Config::defaults() {
  for (uint8_t i = 0; i < kSectionCount; ++i) {
    const Section s = static_cast<Section>(i);
    fieldsDefaults(object(s), fields(s));
  }
}

// Values that are different on every device.
void Config::fillIdentity() {
  hal::Mac mac;
  if (system_) system_->mac(mac);

  if (sys.key.empty() && rng_) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const size_t letters = sizeof(alphabet) - 1;       // 62
    const uint8_t limit = static_cast<uint8_t>(256 - (256 % letters));
    char key[kDeviceKeyLength + 1];
    size_t n = 0;
    while (n < kDeviceKeyLength) {
      uint8_t byte = 0;
      rng_->bytes(&byte, 1);
      if (byte >= limit) continue;  // keeps every letter equally likely
      key[n++] = alphabet[byte % letters];
    }
    key[n] = '\0';
    sys.key.assign(key);
    firstBoot_ = true;
    touch(kSys);
  }
  if (net.apSsid.empty()) {
    net.apSsid.format("%s%02X%02X", kApNamePrefix, mac.b[4], mac.b[5]);
    touch(kNet);
  }
  if (sys.hostname.empty()) {
    sys.hostname.format("loopmax-%02x%02x", mac.b[4], mac.b[5]);
    touch(kSys);
  }
}

// ---------------------------------------------------------------- storage

Status Config::loadSection(Section section) {
  if (!kv_) return Status::Ok;
  return kvLoadFields(*kv_, sectionName(section), object(section), fields(section));
}

Status Config::saveSection(Section section) {
  if (!kv_) return Status::Ok;
  return kvSaveFields(*kv_, sectionName(section), object(section), fields(section));
}

Status Config::load() {
  defaults();
  firstBoot_ = false;
  dirtyMask_ = 0;
  for (uint8_t i = 0; i < kSectionCount; ++i) loadSection(static_cast<Section>(i));

  if (sys.schema != kConfigSchema) {
    // Values of older versions are kept: only the missing ones got their default.
    LMP_LOGI(kLogSys, "config: schema %u updated to %u", static_cast<unsigned>(sys.schema),
             static_cast<unsigned>(kConfigSchema));
    sys.schema = kConfigSchema;
    touch(kSys);
  }
  if (sys.mode != 1 && sys.mode != 2) sys.mode = 1;

  fillIdentity();
  loaded_ = true;
  return dirty() ? save() : Status::Ok;
}

Status Config::save() {
  Status result = Status::Ok;
  for (uint8_t i = 0; i < kSectionCount; ++i) {
    if (!(dirtyMask_ & (1u << i))) continue;
    const Status s = saveSection(static_cast<Section>(i));
    if (s != Status::Ok) result = s;
  }
  if (kv_) {
    const Status s = kv_->commit();
    if (s != Status::Ok) result = s;
  }
  if (result == Status::Ok) {
    dirtyMask_ = 0;
    dirtyStamped_ = false;
  }
  return result;
}

Status Config::factoryReset() {
  const FixedString<32> key = sys.key;
  defaults();
  sys.key = key;
  if (kv_) {
    for (uint8_t i = 0; i < kSectionCount; ++i) {
      kv_->eraseNamespace(sectionName(static_cast<Section>(i)));
    }
  }
  fillIdentity();
  dirtyMask_ = static_cast<uint8_t>((1u << kSectionCount) - 1);
  return save();
}

void Config::touch(Section section) {
  if (section >= kSectionCount) return;
  dirtyMask_ |= static_cast<uint8_t>(1u << section);
  dirtyStamped_ = false;  // the delay starts again from the last change
}

void Config::tick(uint32_t nowMs) {
  if (!dirtyMask_) return;
  if (!dirtyStamped_) {
    dirtyStamped_ = true;
    dirtySinceMs_ = nowMs;
    return;
  }
  if (nowMs - dirtySinceMs_ >= LMP_CONFIG_SAVE_DELAY_MS) save();
}

// ---------------------------------------------------------------- documents

void Config::describe(JsonWriter& w, Section section) const {
  fieldsWrite(w, object(section), fields(section), Secrets::Flag);
}

Status Config::apply(Section section, const JsonValue& json, FieldError* error) {
  if (section >= kSectionCount) return Status::Invalid;
  const Status s = fieldsRead(json, object(section), fields(section), true, error);
  if (s == Status::Ok) touch(section);
  return s;
}

}  // namespace lmp
