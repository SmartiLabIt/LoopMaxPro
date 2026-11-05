#include "services/geo/geo_service.h"

#if LMP_FEATURE_GEO

#include <stdlib.h>
#include <string.h>

#include "lmp_version.h"
#include "services/clock/clock_service.h"
#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif

namespace lmp {
namespace {

constexpr ArgSpec kSetArgs[] = {
    Arg::f64("lat", -90, 90).optional(),
    Arg::f64("lon", -180, 180).optional(),
    Arg::str("city", 0, 40).optional(),
    Arg::str("region", 0, 40).optional(),
    Arg::str("country", 0, 2).optional(),
    Arg::str("lookupUrl", 0, 96).optional(),
};

bool validCountry(const char* text) {
  if (!text || !*text) return true;   // not said
  if (strlen(text) != 2) return false;
  for (const char* p = text; *p; ++p) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) return false;
  }
  return true;
}

void upper(char* text) {
  for (; *text; ++text) {
    if (*text >= 'a' && *text <= 'z') *text = static_cast<char>(*text - 32);
  }
}

// First of the names that is in the object and has the wanted type.
JsonValue firstOf(const JsonValue& object, const char* const* names, size_t count, bool text) {
  for (size_t i = 0; i < count; ++i) {
    const JsonValue v = object.get(names[i]);
    if (!v.valid()) continue;
    if (text ? v.isString() : (v.isNumber() || v.isString())) return v;
  }
  return JsonValue();
}

// The same, in the first of the objects that has it.
JsonValue firstIn(const JsonValue* objects, size_t n, const char* const* names, size_t count,
                  bool text) {
  for (size_t i = 0; i < n; ++i) {
    if (!objects[i].isObject()) continue;
    const JsonValue v = firstOf(objects[i], names, count, text);
    if (v.valid()) return v;
  }
  return JsonValue();
}

// The order of the sources: a GPS on the board first, then what the owner gave
// (written, or the GPS of the phone, from the window of the console), then the network. Who is
// before is not overwritten by who is after.
int rank(GeoSource source) {
  switch (source) {
    case GeoSource::Gps:    return 3;
    case GeoSource::Manual: return 2;
    case GeoSource::Lookup:
    case GeoSource::Cloud:  return 1;
    case GeoSource::None:   break;
  }
  return 0;
}

bool fromNetwork(GeoSource source) {
  return source == GeoSource::Lookup || source == GeoSource::Cloud;
}

double distance(double a, double b) { return a > b ? a - b : b - a; }

constexpr uint8_t kLookupRetries = 3;
constexpr uint32_t kLookupRetryMs = 60000;

// Some servers write the numbers as text.
bool numberOf(const JsonValue& v, double& out) {
  if (v.getDouble(out)) return true;
  char text[24];
  if (!v.getString(text, sizeof(text)) || !text[0]) return false;
  const JsonValue again = JsonValue::parse(StringView(text));
  return again.getDouble(out);
}

}  // namespace

const char* geoSourceName(GeoSource source) {
  switch (source) {
    case GeoSource::Manual: return "manual";
    case GeoSource::Lookup: return "lookup";
    case GeoSource::Cloud:  return "cloud";
    case GeoSource::Gps:    return "gps";
    case GeoSource::None:   break;
  }
  return "none";
}

bool geoParse(StringView json, GeoFound& out) {
  const JsonValue root = JsonValue::parse(json);
  if (!root.isObject()) return false;
  const JsonValue success = root.get("success");
  if (success.isBool() && !success.asBool()) return false;
  // Flat, as most services, or in groups under "data", as some answer.
  const JsonValue data = root.get("data");
  const JsonValue objects[] = {root, data.get("geo"), data.get("client"), data.get("network"),
                               data};
  constexpr size_t kObjects = sizeof(objects) / sizeof(objects[0]);

  static const char* const kLat[] = {"lat", "latitude"};
  static const char* const kLon[] = {"lon", "lng", "longitude"};
  static const char* const kCity[] = {"city"};
  static const char* const kRegion[] = {"regionName", "region", "region_name"};
  static const char* const kCountry[] = {"countryCode", "country_code", "country"};
  static const char* const kIp[] = {"ip", "query"};
  static const char* const kIsp[] = {"isp", "org"};

  bool useful = false;
  double lat = 0, lon = 0;
  if (numberOf(firstIn(objects, kObjects, kLat, 2, false), lat) &&
      numberOf(firstIn(objects, kObjects, kLon, 3, false), lon) &&
      lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
    out.hasPosition = true;
    out.lat = lat;
    out.lon = lon;
    useful = true;
  }
  if (firstIn(objects, kObjects, kCity, 1, true).getString(out.city)) useful = true;
  if (firstIn(objects, kObjects, kRegion, 3, true).getString(out.region)) useful = true;
  // The first name that holds two letters: "country" is the whole name for some servers.
  bool country = false;
  for (size_t i = 0; i < kObjects && !country; ++i) {
    if (!objects[i].isObject()) continue;
    for (const char* name : kCountry) {
      char text[8];
      if (objects[i].get(name).getString(text, sizeof(text)) && strlen(text) == 2 &&
          validCountry(text)) {
        upper(text);
        out.country.assign(text);
        useful = country = true;
        break;
      }
    }
  }
  if (firstIn(objects, kObjects, kIp, 2, true).getString(out.ip)) useful = true;
  if (firstIn(objects, kObjects, kIsp, 2, true).getString(out.isp)) useful = true;
  // The time zone: its name, and its rules when the server gives them.
  const JsonValue zone = data.isObject() ? data.get("timezone") : root.get("timezone");
  if (zone.isObject()) {
    zone.get("name").getString(out.tz);
    zone.get("posix").getString(out.posixTz);
  }
  return useful;
}

const ServiceInfo& GeoService::info() const {
  static const ServiceInfo kInfo{ServiceId::Geo, "geo", "\xF0\x9F\x8C\x8D", Mode::Any,
                                 RestartPolicy::Retry, 1000, DepList(ServiceId::Config)};
  return kInfo;
}

Status GeoService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  lookup_ = Lookup::Idle;
  askedOnce_ = false;
  gpsUnsaved_ = false;
  retries_ = 0;
  retryAtMs_ = 0;
  return Status::Ok;
}

// A GPS that keeps moving: the last position is saved from time to time.
void GeoService::tick(uint32_t nowMs) {
  if (retryAtMs_ && static_cast<int32_t>(nowMs - retryAtMs_) >= 0) {
    retryAtMs_ = 0;
    lookup(0);
  }
  if (!gpsUnsaved_ || nowMs - gpsSavedMs_ < LMP_GEO_GPS_SAVE_MS) return;
  gpsUnsaved_ = false;
  gpsSavedMs_ = nowMs;
  changed();
}

bool GeoService::known() const { return source() != GeoSource::None; }

GeoSource GeoService::source() const {
  return kernel_ ? static_cast<GeoSource>(kernel_->config().geo.source) : GeoSource::None;
}

double GeoService::latitude() const { return kernel_ ? kernel_->config().geo.lat : 0; }
double GeoService::longitude() const { return kernel_ ? kernel_->config().geo.lon : 0; }

void GeoService::changed() {
  kernel_->config().touch(Config::kGeo);
  kernel_->events().publish(Event(EventId::ConfigChanged, Config::kGeo));
}

Status GeoService::set(const GeoPlace& place, GeoSource source) {
  if (!kernel_) return Status::Denied;
  if (source == GeoSource::None) return Status::Invalid;
  if (place.lat < -90 || place.lat > 90 || place.lon < -180 || place.lon > 180) {
    return Status::Invalid;
  }
  if (!validCountry(place.country)) return Status::Invalid;

  GeoConfig& geo = kernel_->config().geo;
  const GeoSource now = this->source();
  if (rank(source) < rank(now)) return Status::Denied;
  // A GPS reads a little differently at every fix: the position moves only past a step,
  // and it is saved at most once in a while.
  if (source == GeoSource::Gps && now == GeoSource::Gps) {
    if (distance(place.lat, geo.lat) < LMP_GEO_GPS_STEP &&
        distance(place.lon, geo.lon) < LMP_GEO_GPS_STEP) {
      return Status::Ok;
    }
    if (kernel_->now() - gpsSavedMs_ < LMP_GEO_GPS_SAVE_MS) {
      geo.lat = place.lat;
      geo.lon = place.lon;
      gpsUnsaved_ = true;
      return Status::Ok;
    }
  }
  if (source == GeoSource::Gps) {
    gpsSavedMs_ = kernel_->now();
    gpsUnsaved_ = false;
  }
  geo.lat = place.lat;
  geo.lon = place.lon;
  // The names: a GPS knows none and leaves the ones there; the network does not write over the
  // ones the owner wrote by hand.
  const bool keepNames = source == GeoSource::Gps || (geo.named && fromNetwork(source));
  if (!keepNames) {
    geo.city.assign(place.city ? place.city : "");
    geo.region.assign(place.region ? place.region : "");
    geo.country.assign(place.country ? place.country : "");
    upper(geo.country.data());
  }
  geo.source = static_cast<uint8_t>(source);
  changed();
  // Whole numbers only: not every board writes the others.
  const long lat = static_cast<long>(geo.lat * 10000 + (geo.lat < 0 ? -0.5 : 0.5));
  const long lon = static_cast<long>(geo.lon * 10000 + (geo.lon < 0 ? -0.5 : 0.5));
  LMP_LOGI(src_, "position from %s: %s%ld.%04ld, %s%ld.%04ld %s %s", geoSourceName(source),
           lat < 0 ? "-" : "", labs(lat) / 10000, labs(lat) % 10000, lon < 0 ? "-" : "",
           labs(lon) / 10000, labs(lon) % 10000, geo.city.c_str(), geo.country.c_str());
  return Status::Ok;
}

// ---------------------------------------------------------------- asking a server

#if LMP_FEATURE_HTTPCLIENT

Status GeoService::lookup(uint32_t ref) {
  if (lookup_ == Lookup::Running) return Status::Busy;
  const GeoConfig& geo = kernel_->config().geo;
  if (geo.lookupUrl.empty()) return Status::Denied;   // nobody to ask
  IHttpClientService* http = kernel_->api<IHttpClientService>();
  if (!http) return Status::Unsupported;

  url_ = geo.lookupUrl;
#if LMP_GEO_HTTP
  // ipwho.is without the secure connection, when a module asks for it on a board (lmp_config.h):
  // its 50 KB for a second, at every start, were all the memory a C3 with ExpNet had free.
  // The same answer, in http.
  if (strcmp(url_.c_str(), "https://ipwho.is/") == 0) url_.assign("http://ipwho.is/");
#endif
  HttpFetch what;
  what.url = url_.c_str();
  what.out = answer_;
  what.cap = sizeof(answer_);
  LMP_TRY(http->fetch(what, [this](const HttpAnswer& answer) { onAnswer(answer); }));
  lookup_ = Lookup::Running;
  lookupRef_ = ref;
  return Status::Ok;
}

void GeoService::onAnswer(const HttpAnswer& answer) {
  GeoFound found;
  Status outcome = Status::Ok;
  if (!answer.good()) {
    outcome = answer.result != Status::Ok ? answer.result : Status::Io;
    LMP_LOGW(src_, "position not received: %s, answer %d", statusName(answer.result),
             answer.status);
  } else if (answer.truncated || !geoParse(StringView(answer_, answer.length), found)) {
    outcome = Status::Invalid;
    LMP_LOGW(src_, "position not received: answer not understood");
  }

  if (outcome == Status::Ok) {
    ip_ = found.ip;
    isp_ = found.isp;
    // What the user wrote by hand, or a GPS, stays: only the address on Internet is news.
    if (rank(source()) <= rank(GeoSource::Lookup) && found.hasPosition) {
      GeoPlace place;
      place.lat = found.lat;
      place.lon = found.lon;
      place.city = found.city.c_str();
      place.region = found.region.c_str();
      place.country = found.country.c_str();
      outcome = set(place, GeoSource::Lookup);
    }
    // The time zone, while the user has not chosen one.
    IClockService* clock = kernel_->api<IClockService>();
    if (clock && !found.posixTz.empty() && !found.tz.empty() &&
        strcmp(kernel_->config().clock.tz.c_str(), "UTC") == 0 &&
        clock->setTimezone(found.tz.c_str(), found.posixTz.c_str()) == Status::Ok) {
      LMP_LOGI(src_, "time zone from the server: %s", found.tz.c_str());
    }
  }
  lookup_ = outcome == Status::Ok ? Lookup::Done : Lookup::Failed;
  // Asked by itself and failed on the way: again in a minute, up to three times.
  if (!lookupRef_ && outcome != Status::Ok && outcome != Status::Invalid &&
      retries_ < kLookupRetries) {
    ++retries_;
    retryAtMs_ = kernel_->now() + kLookupRetryMs;
    if (retryAtMs_ == 0) retryAtMs_ = 1;
  }
  if (lookupRef_) kernel_->events().publish(commandDone(lookupRef_, outcome));
  lookupRef_ = 0;
}

#else

Status GeoService::lookup(uint32_t) { return Status::Unsupported; }
void GeoService::onAnswer(const HttpAnswer&) {}

#endif

void GeoService::onEvent(const Event& event) {
  if (!kernel_ || event.id != EventId::InternetUp) return;
  // Once for each start: the position of a device does not change while it is on.
  if (askedOnce_ || kernel_->config().geo.lookupUrl.empty()) return;
  if (lookup(0) == Status::Ok) askedOnce_ = true;
}

// ---------------------------------------------------------------- documents and commands

void GeoService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const GeoConfig& geo = kernel_->config().geo;
  w.boolean("known", known());
  w.str("source", geoSourceName(source()));
  if (known()) {
    w.f64("lat", geo.lat, 6);
    w.f64("lon", geo.lon, 6);
  }
  w.str("city", geo.city.c_str());
  w.str("region", geo.region.c_str());
  w.str("country", geo.country.c_str());
  // The console asks the owner where the device is once, while the position is not a point.
  w.boolean("asked", geo.asked);
  w.str("publicIp", ip_.c_str());
  w.str("isp", isp_.c_str());
  w.beginObject("lookup");
  w.boolean("configured", !geo.lookupUrl.empty());
  const char* state = "idle";
  if (lookup_ == Lookup::Running) state = "running";
  if (lookup_ == Lookup::Done) state = "done";
  if (lookup_ == Lookup::Failed) state = "failed";
  w.str("state", state);
  w.endObject();
}

void GeoService::commands(CommandSet& set) {
  set.add("set",
          LMP_HELP("Position and place of the device, and the server that tells them"), kSetArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (a.count() == 0) return Status::Invalid;
            if (a.has("lat") != a.has("lon")) {
              ctx.errorField = a.has("lat") ? "lon" : "lat";
              return Status::Invalid;
            }
            if (a.has("country") && !validCountry(a.str("country"))) {
              ctx.errorField = "country";
              return Status::Invalid;
            }
            GeoConfig& geo = kernel_->config().geo;
            if (a.has("lookupUrl")) {
              const char* url = a.str("lookupUrl");
              if (*url && strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
                ctx.errorField = "lookupUrl";
                return Status::Invalid;
              }
              geo.lookupUrl.assign(url);
              askedOnce_ = false;
            }
            // The point of the owner: written, pasted, or the GPS of the phone, from the window
            // of the console. A GPS on the board comes before it: with one, no.
            if (a.has("lat")) {
              if (source() == GeoSource::Gps) {
                ctx.errorField = "gps";
                return Status::Denied;
              }
              geo.lat = a.f64("lat");
              geo.lon = a.f64("lon");
              geo.source = static_cast<uint8_t>(GeoSource::Manual);
              geo.asked = true;   // answered: the console does not ask by itself any more
            }
            // The names alone are not a point: where the device is stays as precise as it was
            // (in 1.0.5 a city written by hand made the position of the network "by hand").
            if (a.has("city") || a.has("region") || a.has("country")) {
              if (a.has("city")) geo.city.assign(a.str("city"));
              if (a.has("region")) geo.region.assign(a.str("region"));
              if (a.has("country")) {
                geo.country.assign(a.str("country"));
                upper(geo.country.data());
              }
              geo.named = true;
            }
            changed();
            return Status::Ok;
          });

  // "Not now" in the window of the console that asks where the device is: it opens by itself
  // once for a device, not once for every phone, so the device remembers it.
  set.add("later", LMP_HELP("The owner does not say where the device is now: not asked again"),
          [this](const Args&, CmdContext&) -> Status {
            kernel_->config().geo.asked = true;
            changed();
            return Status::Ok;
          });

  set.add("lookup", LMP_HELP("Asks the position to the server of the settings"),
          [this](const Args&, CmdContext& ctx) -> Status {
            const Status s = lookup(ctx.ref);
            return s == Status::Ok ? Status::Pending : s;
          },
          kCmdAsync);

  set.add("clear", LMP_HELP("Forgets the position: GPS and server write it again"),
          [this](const Args&, CmdContext&) -> Status {
            GeoConfig& geo = kernel_->config().geo;
            geo.lat = geo.lon = 0;
            geo.city.clear();
            geo.region.clear();
            geo.country.clear();
            geo.named = false;
            geo.source = static_cast<uint8_t>(GeoSource::None);
            gpsUnsaved_ = false;
            changed();
            // Back to automatic: the server is asked again now, if it can be.
            askedOnce_ = lookup(0) == Status::Ok;
            return Status::Ok;
          },
          kCmdConfirm);
}

}  // namespace lmp

#endif  // LMP_FEATURE_GEO
