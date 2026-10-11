// LoopMaxPro - MeteoWifi, free edition: the weather of a place, in the console and on the
// 0.96" screen of the board.
// The weather comes from Open-Meteo, free, without a key and over plain HTTP, so that the
// ESP8266 reaches it too. The place is the position of the device, or a city chosen in
// the module. The screen belongs to the module: the system only gives the bus.
#include "lmp_config.h"

// The sources of every module are compiled in every build. This one needs the client of web
// services and the I2C bus, which its "requires" turn on when it is chosen.
#if LMP_FEATURE_HTTPCLIENT && LMP_FEATURE_BUS

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "art.h"
#include "hal/api/memory.h"
#include "kernel/kernel.h"
#include "oled.h"
#include "sdk/module.h"
#include "sdk/state.h"
#include "services/bus/bus_service.h"
#include "services/geo/geo_service.h"
#include "services/httpclient/httpclient_service.h"
#include "services/net/net_service.h"

// Parameters of the build: custom_lmp_modules = meteowifi:sda=21;scl=22. The pins of each board
// are in module.json ("boardPins"): the build puts them here. -1: no screen.
#ifndef LMP_METEOWIFI_SDA
#define LMP_METEOWIFI_SDA -1
#endif
#ifndef LMP_METEOWIFI_SCL
#define LMP_METEOWIFI_SCL -1
#endif
// The second place of the screen, on the boards that look the same and carry it on different
// pins: the NodeMCU V3 with a screen has it on 4 and 5, or on 14 and 12. -1: one place only.
#ifndef LMP_METEOWIFI_SDA2
#define LMP_METEOWIFI_SDA2 -1
#endif
#ifndef LMP_METEOWIFI_SCL2
#define LMP_METEOWIFI_SCL2 -1
#endif
#ifndef LMP_METEOWIFI_ADDRESS
#define LMP_METEOWIFI_ADDRESS 60
#endif

namespace meteowifi {

struct Cfg {
  lmp::FixedString<96> api;       // forecast
  lmp::FixedString<96> geoApi;    // search of a city by name
  lmp::FixedString<48> place;     // the chosen city; empty: the position of the device
  float lat;
  float lon;
  uint16_t everyMin;
  bool fahrenheit;
  bool screen;
};

}  // namespace meteowifi

LMP_FIELDS(meteowifi::Cfg,
  LMP_F(api,        "api",        Str(0, 96, "http://api.open-meteo.com/v1/forecast")),
  LMP_F(geoApi,     "geoApi",     Str(0, 96, "http://geocoding-api.open-meteo.com/v1/search")),
  LMP_F(place,      "place",      Str(0, 48, "")),
  LMP_F(lat,        "lat",        F32(-90, 90, 0)),
  LMP_F(lon,        "lon",        F32(-180, 180, 0)),
  LMP_F(everyMin,   "everyMin",   U16(5, 1440, 15)),
  LMP_F(fahrenheit, "fahrenheit", Bool(false)),
  // On from the factory where the build gives the pins of a screen (NodeMCU, Kit 8); off on
  // the boards without one, where a switch that is on would promise what is not there.
  LMP_F(screen,     "screen",     Bool(LMP_METEOWIFI_SDA >= 0 && LMP_METEOWIFI_SCL >= 0)))

namespace meteowifi {
namespace {

using lmp::Status;

constexpr uint8_t kDays = 7;
constexpr size_t kAnswer = 1400;          // the forecast is about 1 KB
constexpr uint32_t kRetryMs = 60000;      // after a failed request
constexpr uint32_t kSearchTimeoutMs = 8000;
// Where the screen of the board can be: the pins of the build, then its second place.
struct ScreenPlace {
  int8_t sda;
  int8_t scl;
  constexpr bool wired() const { return sda >= 0 && scl >= 0; }
};
constexpr ScreenPlace kScreenPlaces[] = {
    {LMP_METEOWIFI_SDA, LMP_METEOWIFI_SCL},
    {LMP_METEOWIFI_SDA2, LMP_METEOWIFI_SCL2},
};
constexpr uint8_t kScreenPlaceCount = sizeof(kScreenPlaces) / sizeof(kScreenPlaces[0]);
constexpr const char* kScreenOwner = "meteowifi";
constexpr uint32_t kScreenHz = 400000;
// The screen is asked a few times in a place, a second apart, before the bus moves to the
// other one. With no answer anywhere the module goes on without a screen and leaves the bus
// free. The place that answered is kept on file: the next start goes there at once.
constexpr uint32_t kScreenRetryMs = 1000;
constexpr uint8_t kScreenLooks = 3;

// lang: the language of the name, two letters; the one of the device when not given.
const lmp::ArgSpec kUnitArgs[] = {lmp::Arg::boolean("fahrenheit")};
const lmp::ArgSpec kPlaceArgs[] = {lmp::Arg::str("name", 0, 48).optional(),
                                   lmp::Arg::str("lang", 2, 2).optional()};
// What the sky is like, from the WMO code the service gives.
Sky skyOf(int code, bool day) {
  if (code >= 95) return Sky::Storm;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return day ? Sky::Rain : Sky::NightRain;
  }
  if (code == 45 || code == 48) return Sky::Fog;
  if (code == 3 || (code == 2 && day)) return Sky::Cloud;
  return day ? Sky::Sun : Sky::Night;
}

const char* skyName(Sky sky) {
  static const char* const kNames[] = {"sun", "night", "cloud", "fog", "rain", "night_rain",
                                       "storm", "snow"};
  return kNames[static_cast<uint8_t>(sky)];
}

// The same sky among the icons of the console.
const char* skyIcon(Sky sky) {
  static const char* const kIcons[] = {"sun", "moon", "cloud", "cloud-fog", "cloud-rain",
                                       "cloud-moon-rain", "cloud-lightning", "cloud-snow"};
  return kIcons[static_cast<uint8_t>(sky)];
}

// A small value of the document, in the shape the console draws: { key, value, unit, icon }.
void reading(lmp::JsonWriter& w, const char* key, double value, uint8_t decimals,
             const char* unit, const char* icon) {
  w.beginObject();
  w.str("key", key);
  w.f64("value", value, decimals);
  w.str("unit", unit);
  w.str("icon", icon);
  w.endObject();
}

// Coordinates with four decimals, without printf of floats.
void coordinate(char* out, size_t cap, double v) {
  const long scaled = lround(v * 10000.0);
  const unsigned long a = static_cast<unsigned long>(scaled < 0 ? -scaled : scaled);
  snprintf(out, cap, "%s%lu.%04lu", scaled < 0 ? "-" : "", a / 10000, a % 10000);
}

void encode(char* out, size_t cap, const char* text) {
  static const char kHex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (; text && *text && n + 4 < cap; ++text) {
    const uint8_t c = static_cast<uint8_t>(*text);
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        c == '-' || c == '.' || c == '_') {
      out[n++] = static_cast<char>(c);
    } else {
      out[n++] = '%';
      out[n++] = kHex[c >> 4];
      out[n++] = kHex[c & 15];
    }
  }
  out[n] = 0;
}

// A town the search found: what the console shows in its list, and where it is.
struct Town {
  int64_t id = 0;         // the number the service gives it: the same town in two languages
  int64_t weight = 0;
  char name[33] = "";
  char region[25] = "";
  char country[3] = "";
  float lat = 0, lon = 0;
};

// The device keeps the most likely one only, for who gives it a name and nothing else (the
// command "location", a name written in the settings). Choosing among many towns with the same
// name is the work of the console, which asks the service from the browser and shows them
// all (ui/meteowifi.js): the device then gets the name with its coordinates.
// Two are kept: the first is the one taken, the second lets a town found again in another
// language move up past it.
constexpr uint8_t kTowns = 2;

// Reads the answers of the search as they arrive, one town at a time, and keeps the best
// ones, the first the most likely: the most populous, where a town with just the name asked
// counts as twenty times its people ("San Marco" before a larger "San Marcos"; but a hamlet
// called Roma never before the capital). The service gives them in no useful order, and looks
// for the name only in the language asked: the same name is asked in more than one, and the
// towns of all the answers end in one list.
class PlaceFinder {
 public:
  void reset(const char* wanted) {
    strncpy(wanted_, wanted, sizeof(wanted_) - 1);
    wanted_[sizeof(wanted_) - 1] = 0;
    count_ = 0;
    again();
  }

  // Another answer for the same name, in another language: the towns found so far stay.
  void again() {
    len_ = 0;
    depth_ = 0;
    quoted_ = escaped_ = skipping_ = false;
  }

  void clear() { count_ = 0; }

  bool feed(lmp::Span<const uint8_t> data) {
    for (uint8_t c : data) take(static_cast<char>(c));
    return true;
  }

  bool found() const { return count_ > 0; }
  uint8_t count() const { return count_; }
  const Town& at(uint8_t i) const { return towns_[i]; }

 private:
  // {"results":[{town},{town}]}: a town is an object at the third level. What is inside it one
  // level deeper, the list of its postcodes, is not kept: a large city has hundreds of them, and
  // with them Roma and Milano did not fit and were left out.
  void take(char c) {
    if (quoted_) {
      if (depth_ == 3) keep(c);
      if (escaped_) escaped_ = false;
      else if (c == '\\') escaped_ = true;
      else if (c == '"') quoted_ = false;
      return;
    }
    if (c == '"') {
      quoted_ = true;
      if (depth_ == 3) keep(c);
    } else if (c == '{' || c == '[') {
      ++depth_;
      if (depth_ == 3) {
        len_ = 0;
        skipping_ = false;
        keep(c);
      } else if (depth_ == 4) {
        keep('0');   // in the place of the list: the town stays a well formed object
      }
    } else if (c == '}' || c == ']') {
      if (depth_ == 3) {
        keep(c);
        if (!skipping_) town();
      }
      --depth_;
    } else if (depth_ == 3) {
      keep(c);
    }
  }

  void keep(char c) {
    if (len_ + 1 >= sizeof(town_)) {
      skipping_ = true;   // a town longer than the room is left out
      return;
    }
    town_[len_++] = c;
  }

  void town() {
    const lmp::JsonValue t = lmp::JsonValue::parse(lmp::StringView(town_, len_));
    if (!t.isObject()) return;
    Town found;
    t["name"].getString(found.name, sizeof(found.name));
    if (!found.name[0]) return;
    found.id = t["id"].asInt(0);
    const int64_t people = t["population"].asInt(0);
    found.weight = (people > 0 ? people : 1) * (strcasecmp(found.name, wanted_) == 0 ? 20 : 1);
    t["admin1"].getString(found.region, sizeof(found.region));
    t["country_code"].getString(found.country, sizeof(found.country));
    found.lat = static_cast<float>(t["latitude"].asDouble());
    found.lon = static_cast<float>(t["longitude"].asDouble());

    // The same town from another language: the name of the first language stays, the one of
    // who asked; the weight is the larger, the very name may be in one language only.
    uint8_t at = count_;
    for (uint8_t i = 0; i < count_; ++i) {
      if (found.id && towns_[i].id == found.id) at = i;
    }
    if (at < count_) {
      if (found.weight <= towns_[at].weight) return;
      towns_[at].weight = found.weight;
    } else if (count_ < kTowns) {
      towns_[count_++] = found;
    } else if (found.weight > towns_[count_ - 1].weight) {
      at = static_cast<uint8_t>(count_ - 1);
      towns_[at] = found;
    } else {
      return;
    }
    // In order, the most likely first: the list is short, the town moves up one step at a time.
    for (uint8_t i = at < count_ ? at : static_cast<uint8_t>(count_ - 1);
         i > 0 && towns_[i].weight > towns_[i - 1].weight; --i) {
      const Town moved = towns_[i];
      towns_[i] = towns_[i - 1];
      towns_[i - 1] = moved;
    }
  }

  char town_[640];
  size_t len_ = 0;
  int depth_ = 0;
  bool quoted_ = false, escaped_ = false, skipping_ = false;
  char wanted_[49] = "";
  Town towns_[kTowns];
  uint8_t count_ = 0;
};

struct Now {
  float temp = 0, feels = 0, wind = 0;
  uint8_t humidity = 0;
  int16_t code = -1;
  bool day = true;
};

struct Day {
  char date[11] = "";
  int16_t code = -1;
  float hi = 0, lo = 0;
  uint8_t rain = 0;
};

class MeteoWifi;

class Settings final : public lmp::sdk::ModuleConfig<Cfg> {
 public:
  MeteoWifi* owner = nullptr;

 protected:
  void applied() override;
};

class MeteoWifi final : public lmp::sdk::Module {
  LMP_MODULE("meteowifi", "MeteoWifi", "\xF0\x9F\x8C\xA4", "1.0.4",
             lmp::sdk::ModuleKind::Online, 1000)

 public:
  Status onInit(lmp::sdk::ModuleContext& c) override {
    // A module that starts again starts from nothing: the weather is asked again.
    have_ = asked_ = asking_ = forced_ = false;
    dayCount_ = 0;
    problem_ = searched_ = "";
    search_.clear();
    searchRef_ = 0;
    bySpace_ = wholeName_ = fromSettings_ = false;
    goodPlace_.clear();
    goodLat_ = goodLon_ = 0;
    searchAgainMs_ = 0;
    searchWaits_ = false;
    langIndex_ = langCount_ = answers_ = 0;
    // The screen too: where it is comes from the file, read after this.
    screen_.end();
    screenOpen_ = false;
    screenVisits_ = 0;
    screenPlace_ = 0;
    settings_.owner = this;
    kernel_ = c.kernel;
    LMP_TRY(useSettings(settings_));
    if (!answer_) answer_ = static_cast<char*>(lmp::hal::allocLarge(kAnswer));
    if (!answer_) return Status::NoMem;
    // The search of a city takes a moment: the outcome arrives later, found or not found, so
    // that who asked is told and does not keep looking at the weather of before.
    c.commands->add("location", LMP_HELP("Weather of a city; without a name, of the device"),
                    kPlaceArgs, [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return choose(a.str("name"), a.str("lang"), ctx.ref);
                    },
                    lmp::kCmdAsync);
    c.commands->add("units", LMP_HELP("Degrees Fahrenheit, or Celsius"), kUnitArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      settings_->fahrenheit = a.boolean("fahrenheit");
                      settings_.touch();
                      onSettings();
                      return Status::Ok;
                    });
    c.commands->add("refresh", LMP_HELP("Asks the weather now"),
                    [this](const lmp::Args&, lmp::CmdContext&) -> Status {
                      forced_ = true;
                      return Status::Ok;
                    });
    return Status::Ok;
  }

  // The screen is looked for here and not before: the file of the module, which says where it
  // answered the last time, is read after onInit.
  Status onStart() override {
    if (kernel_) nowMs_ = kernel_->now();   // no tick yet: the looks are timed from now
    startScreen();
    return Status::Ok;
  }

  void onStop() override { stopScreen(); }

  // Beside the settings the module keeps where its screen is, among kScreenPlaces.
  void save(lmp::JsonWriter& w) const override { w.u32("screenPlace", screenPlace_); }

  void load(const lmp::JsonValue& doc) override {
    const int64_t place = doc["screenPlace"].asInt(0);
    screenPlace_ = place >= 0 && place < kScreenPlaceCount ? static_cast<uint8_t>(place) : 0;
  }

  void onTick(uint32_t nowMs) override {
    nowMs_ = nowMs;
    if (screenVisits_ && static_cast<int32_t>(nowMs - screenAtMs_) >= 0) lookForScreen();
    if (asking_) return;
    followPlace();
    // Only with Internet: the services of the weather are out there.
    lmp::IHttpClientService* http = ctx().http;
    const lmp::INetService* net = ctx().net;
    if (!http || !http->ready() || !net || !net->internet()) {
      problem("network");
      return;
    }
    if (!search_.empty()) {
      askPlace();
      return;
    }
    double lat = 0, lon = 0;
    if (!where(lat, lon)) {
      problem("place");
      return;
    }
    if (!forced_ && asked_ && static_cast<int32_t>(nowMs - nextMs_) < 0) return;
    askForecast(lat, lon);
  }

  // now and days have the shapes the console draws by itself; code and sky are for
  // who reads the document with a program.
  // What a home system reads of the weather (sdk/home.h): the values of the console.
  void things(lmp::sdk::ThingSink& out) const override {
    const char* deg = settings_->fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
    out.number("temperature", {"Temperature", "Temperatura"}, now_.temp, deg,
               "temperature", 1, have_);
    out.number("feels", {"Feels like", "Percepita"}, now_.feels, deg, "temperature", 1,
               have_);
    out.number("humidity", {"Humidity", "Umidit\xC3\xA0"}, now_.humidity, "%", "humidity", 0,
               have_);
    out.number("wind", {"Wind", "Vento"}, now_.wind, "km/h", "wind_speed", 1, have_);
    out.button("refresh", {"Refresh the weather", "Aggiorna il meteo"});
  }

  Status thingSet(const char* id, const lmp::sdk::ThingAsk&) override {
    if (strcmp(id, "refresh") != 0) return Status::NotFound;
    forced_ = true;
    return Status::Ok;
  }

  void describe(lmp::JsonWriter& w) const override {
    const bool city = !settings_->place.empty();
    if (city) w.str("place", settings_->place.c_str());
    if (*problem_) w.str("problem", problem_);
    if (*searched_) w.str("search", searched_);   // how the last search of a city went
    w.str("units", settings_->fahrenheit ? "F" : "C");
    w.u32("ageSec", have_ ? (nowMs_ - gotMs_) / 1000 : 0);
    w.boolean("screen", screen_.ready());
    if (!have_) return;
    const char* deg = settings_->fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
    const Sky sky = skyOf(now_.code, now_.day);
    w.beginObject("now");
    w.str("icon", skyIcon(sky));
    w.f64("value", now_.temp, 1);
    w.str("unit", deg);
    w.str("label", city ? settings_->place.c_str() : deviceCity());
    w.i32("code", now_.code);
    w.str("sky", skyName(sky));
    w.beginArray("items");
    reading(w, "feels", now_.feels, 1, deg, "thermometer");
    reading(w, "humidity", now_.humidity, 0, "%", "droplets");
    reading(w, "wind", now_.wind, 1, "km/h", "wind");
    w.endArray();
    w.endObject();
    w.beginArray("days");
    for (uint8_t i = 0; i < dayCount_; ++i) {
      const Day& d = days_[i];
      const Sky daySky = skyOf(d.code, true);
      w.beginObject();
      w.str("date", d.date);
      w.str("icon", skyIcon(daySky));
      w.f64("hi", d.hi, 1);
      w.f64("lo", d.lo, 1);
      w.str("unit", deg);
      w.i32("code", d.code);
      w.str("sky", skyName(daySky));
      w.beginArray("items");
      reading(w, "rain", d.rain, 0, "%", "umbrella");
      w.endArray();
      w.endObject();
    }
    w.endArray();
  }

  void onSettings() {
    followPlace();   // at once: a city just written is looked for before any weather is asked
    forced_ = true;
    if (!settings_->screen && screen_.ready()) screen_.power(false);
    if (settings_->screen && screen_.ready()) screen_.power(true);
    paint();
  }

 private:
  // ---- the screen: where it is
  // On the ESP8266 the screen on board of NodeMCU and Kit 8 (module.json); elsewhere only when
  // the build gives its pins: they stay free for the other modules.
  void startScreen() {
    stopScreen();
    uint8_t places = 0;
    for (const ScreenPlace& p : kScreenPlaces) {
      if (p.wired()) ++places;
    }
    if (!ctx().bus || !places) return;
    // From the place where it answered the last time.
    screenAt_ = kScreenPlaces[screenPlace_].wired() ? screenPlace_ : placeAfter(screenPlace_);
    screenVisits_ = places;
    screenWhy_ = Status::NotFound;
    lookForScreen();
  }

  void stopScreen() {
    screen_.end();
    closeScreenBus();
    screenVisits_ = 0;
  }

  void closeScreenBus() {
    if (screenOpen_ && ctx().bus) ctx().bus->closeI2c(0, kScreenOwner);
    screenOpen_ = false;
  }

  // The next place with pins after this one: the same, on a board with one place.
  static uint8_t placeAfter(uint8_t place) {
    for (uint8_t step = 1; step < kScreenPlaceCount; ++step) {
      const uint8_t next = static_cast<uint8_t>((place + step) % kScreenPlaceCount);
      if (kScreenPlaces[next].wired()) return next;
    }
    return place;
  }

  // One more look: here, or in the next place when this one never answered.
  void lookForScreen() {
    screenAtMs_ = nowMs_ + kScreenRetryMs;
    while (screenVisits_) {
      const ScreenPlace& p = kScreenPlaces[screenAt_];
      if (!screenOpen_) {
        const Status s = ctx().bus->openI2c(0, static_cast<uint8_t>(p.sda),
                                            static_cast<uint8_t>(p.scl), kScreenHz, kScreenOwner);
        screenOpen_ = s == Status::Ok;
        screenLooks_ = kScreenLooks;
        if (screenOpen_) {
          // The first call on a bus just opened goes lost: measured on the ESP8266, wherever
          // the screen is. Its pins are driven while the bus is set up, which a
          // chip can take for the start of a call. One is spent here.
          ctx().bus->i2c(0)->probe(LMP_METEOWIFI_ADDRESS);
        } else {
          // The pins are of somebody else, or are not two pins.
          screenWhy_ = s;
          LMP_LOGW(ctx().log, "screen: bus not opened on %d and %d: %s", p.sda, p.scl,
                   lmp::statusName(s));
        }
      }
      if (screenOpen_) {
        const Status s = screen_.begin(ctx().bus->i2c(0), LMP_METEOWIFI_ADDRESS);
        if (s == Status::Ok) {
          screenVisits_ = 0;
          LMP_LOGI(ctx().log, "screen at 0x%02X, on pins %d and %d", LMP_METEOWIFI_ADDRESS, p.sda,
                   p.scl);
          if (screenPlace_ != screenAt_) {
            screenPlace_ = screenAt_;
            markUnsaved();   // the next start comes here at once
          }
          markChanged();
          paint();
          return;
        }
        screenWhy_ = s;
        if (--screenLooks_) return;   // asked here again, in a while
        closeScreenBus();
      }
      // Nothing in this place: the next one, at once.
      --screenVisits_;
      screenAt_ = placeAfter(screenAt_);
    }
    LMP_LOGI(ctx().log, "no screen at 0x%02X: %s", LMP_METEOWIFI_ADDRESS,
             lmp::statusName(screenWhy_));
  }

  // The town of the position of the device, as its service found it. Empty when not known.
  const char* deviceCity() const {
#if LMP_FEATURE_GEO
    return kernel_ ? kernel_->config().geo.city.c_str() : "";
#else
    return "";
#endif
  }

  bool where(double& lat, double& lon) {
    if (!settings_->place.empty()) {
      lat = settings_->lat;
      lon = settings_->lon;
      return true;
    }
#if LMP_FEATURE_GEO
    const lmp::IGeoService* geo = ctx().geo;
    if (!geo || !geo->known()) return false;
    lat = geo->latitude();
    lon = geo->longitude();
    return true;
#else
    return false;   // no position of the device on this build: a city is chosen
#endif
  }

  // A city written in the settings ("place") is a name, not a position: without this the
  // weather was asked for the coordinates of before, 0 and 0 on a new board, a point in the
  // ocean, whatever the city. The name is looked for as
  // the command "location" does, and the settings take the town found with its coordinates.
  // A place saved with its coordinates, by a search or written with them, is taken as it is.
  void followPlace() {
    const Cfg& cfg = settings_.value();
    const bool samePlace = strcmp(cfg.place.c_str(), goodPlace_.c_str()) == 0;
    const bool sameSpot = cfg.lat == goodLat_ && cfg.lon == goodLon_;
    // A name at latitude 0 and longitude 0 has no coordinates of its own, whatever came before.
    const bool nowhere = !cfg.place.empty() && cfg.lat == 0 && cfg.lon == 0;
    if (samePlace && sameSpot && !nowhere) return;
    if (nowhere || (!samePlace && sameSpot && !cfg.place.empty())) {
      if (strcmp(search_.c_str(), cfg.place.c_str()) == 0) return;   // being looked for
      if (searchWaits_ && static_cast<int32_t>(nowMs_ - searchAgainMs_) < 0) return;
      searchWaits_ = false;
      search_.assign(cfg.place.c_str());
      searched_ = "";
      searchRef_ = 0;
      wholeName_ = false;
      langIndex_ = 0;
      fromSettings_ = true;
      searchLang_.assign(ctx().kernel->config().sys.lang.c_str());
      return;
    }
    keepPlace();
  }

  // The place of the settings is good: a name with its own coordinates, or no name.
  void keepPlace() {
    goodPlace_.assign(settings_->place.c_str());
    goodLat_ = settings_->lat;
    goodLon_ = settings_->lon;
  }

  Status choose(const char* name, const char* lang, uint32_t ref) {
    if (!name || !*name) {
      settings_->place.clear();
      settings_.touch();
      keepPlace();
      search_.clear();
      forced_ = true;
      markChanged();
      return Status::Ok;
    }
    // One search at a time; the one of the settings gives way to who asks now.
    if (!search_.empty() && !fromSettings_) return Status::Busy;
    if (!search_.assign(name)) return Status::Invalid;
    searched_ = "";
    searchRef_ = ref;
    wholeName_ = false;
    langIndex_ = 0;
    fromSettings_ = false;
    searchLang_.assign(lang && strlen(lang) == 2 ? lang : ctx().kernel->config().sys.lang.c_str());
    return Status::Pending;
  }

  // Tells who asked for the city how it went.
  void searchDone(Status outcome) {
    if (searchRef_) kernel_->events().publish(lmp::commandDone(searchRef_, outcome));
    searchRef_ = 0;
  }

  void askForecast(double lat, double lon) {
    char la[16], lo[16];
    coordinate(la, sizeof(la), lat);
    coordinate(lo, sizeof(lo), lon);
    snprintf(url_, sizeof(url_),
             "%s?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,"
             "is_day,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max"
             "&timezone=auto&forecast_days=%u%s",
             settings_->api.c_str(), la, lo, kDays,
             settings_->fahrenheit ? "&temperature_unit=fahrenheit" : "");
    forced_ = false;
    asked_ = true;
    ask([this](const lmp::HttpAnswer& a) { onForecast(a); }, LMP_HTTPC_TIMEOUT_MS);
  }

  // "Milano", "Milano, IT" or "Milano IT": the two letters of a country narrow the search,
  // which otherwise gives the first town with that name. Written without the comma the two
  // letters may be part of the name: when nothing is found that way, the whole text is the name.
  void askPlace() {
    char city[49], country[3] = "";
    strncpy(city, search_.c_str(), sizeof(city) - 1);
    city[sizeof(city) - 1] = 0;
    bySpace_ = false;
    if (!wholeName_) {
      char* cut = strrchr(city, ',');
      if (!cut) {
        cut = strrchr(city, ' ');
        bySpace_ = cut != nullptr;
      }
      if (cut) {
        const char* cc = cut + 1;
        while (*cc == ' ') ++cc;
        const bool letters = strlen(cc) == 2 && isalpha(static_cast<unsigned char>(cc[0])) &&
                             isalpha(static_cast<unsigned char>(cc[1]));
        if (letters && cut != city) {
          country[0] = static_cast<char>(toupper(static_cast<unsigned char>(cc[0])));
          country[1] = static_cast<char>(toupper(static_cast<unsigned char>(cc[1])));
          *cut = 0;
          // "Roma , IT": no spaces left at the end of the name.
          for (char* end = cut; end > city && end[-1] == ' ';) *--end = 0;
        } else {
          bySpace_ = false;
        }
      }
    }
    // The service looks for the name in the language asked, and only there: "Roma" in English
    // is not the capital, which is "Rome". So the name is
    // looked for in the language of who asks, in the one of the country written (or of the
    // country the device is in), and in English; the best town of all the answers is taken.
    char langs[3][3] = {"", "", ""};
    uint8_t count = 0;
    const auto add = [&langs, &count](const char* code) {
      if (!code || strlen(code) != 2) return;
      char low[3] = {static_cast<char>(tolower(static_cast<unsigned char>(code[0]))),
                     static_cast<char>(tolower(static_cast<unsigned char>(code[1]))), 0};
      for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(langs[i], low) == 0) return;
      }
      if (count < 3) strcpy(langs[count++], low);
    };
    add(searchLang_.c_str());
#if LMP_FEATURE_GEO
    add(country[0] ? country : kernel_->config().geo.country.c_str());
#else
    add(country);
#endif
    add("en");
    langCount_ = count;
    if (langIndex_ >= count) langIndex_ = 0;

    char name[150];
    encode(name, sizeof(name), city);
    snprintf(url_, sizeof(url_), "%s?name=%s&count=10&language=%s&format=json%s%s",
             settings_->geoApi.c_str(), name, langs[langIndex_],
             country[0] ? "&countryCode=" : "", country);
    if (langIndex_ == 0) {
      finder_.reset(city);
      answers_ = 0;
    } else {
      finder_.again();
    }
    ask([this](const lmp::HttpAnswer& a) { onPlace(a); }, kSearchTimeoutMs, true);
  }

  void ask(lmp::HttpDone done, uint32_t timeoutMs, bool search = false) {
    lmp::HttpFetch what;
    what.url = url_;
    what.timeoutMs = timeoutMs;
    if (search) {
      what.sink = [this](lmp::Span<const uint8_t> data) { return finder_.feed(data); };
    } else {
      what.out = answer_;
      what.cap = kAnswer;
    }
    if (ctx().http->fetch(what, done) == Status::Ok) {
      asking_ = true;
    } else {
      nextMs_ = nowMs_ + kRetryMs;
    }
  }

  void onForecast(const lmp::HttpAnswer& a) {
    asking_ = false;
    if (!a.good() || a.truncated) {
      LMP_LOGW(ctx().log, "weather not received: %s, answer %d%s", lmp::statusName(a.result),
               a.status, a.truncated ? ", too long" : "");
      failed(a.good() ? "answer" : "server");
      return;
    }
    const lmp::JsonValue doc = lmp::JsonValue::parse(lmp::StringView(answer_, a.length));
    const lmp::JsonValue cur = doc["current"];
    if (!cur.isObject()) {
      failed("answer");
      return;
    }
    now_.temp = static_cast<float>(cur["temperature_2m"].asDouble());
    now_.feels = static_cast<float>(cur["apparent_temperature"].asDouble());
    now_.wind = static_cast<float>(cur["wind_speed_10m"].asDouble());
    now_.humidity = static_cast<uint8_t>(cur["relative_humidity_2m"].asInt());
    now_.code = static_cast<int16_t>(cur["weather_code"].asInt(-1));
    now_.day = cur["is_day"].asInt(1) != 0;

    const lmp::JsonValue daily = doc["daily"];
    const lmp::JsonValue dates = daily["time"];
    dayCount_ = static_cast<uint8_t>(dates.size() < kDays ? dates.size() : kDays);
    for (uint8_t i = 0; i < dayCount_; ++i) {
      Day& d = days_[i];
      dates.at(i).getString(d.date, sizeof(d.date));
      d.code = static_cast<int16_t>(daily["weather_code"].at(i).asInt(-1));
      d.hi = static_cast<float>(daily["temperature_2m_max"].at(i).asDouble());
      d.lo = static_cast<float>(daily["temperature_2m_min"].at(i).asDouble());
      d.rain = static_cast<uint8_t>(daily["precipitation_probability_max"].at(i).asInt());
    }
    have_ = true;
    gotMs_ = nowMs_;
    nextMs_ = nowMs_ + settings_->everyMin * 60000u;
    problem_ = "";
    markChanged();
    paint();
  }

  void onPlace(const lmp::HttpAnswer& a) {
    asking_ = false;
    if (a.good()) ++answers_;
    // The next language: the search goes on at the next round, search_ is still there.
    if (++langIndex_ < langCount_) return;
    langIndex_ = 0;
    // "Bagno a Ripoli FI" found nothing as Bagno a Ripoli in FI: once more, the whole text.
    if (answers_ && !finder_.found() && bySpace_ && !wholeName_) {
      wholeName_ = true;
      return;
    }
    search_.clear();   // a search that failed is not repeated by itself
    const bool answered = answers_ > 0;
    if (!finder_.found()) {
      searched_ = answered ? "not-found" : "failed";
      if (fromSettings_) {
        if (answered) {
          // No town with the name written in the settings: the place of before comes back, so
          // that the name shown is always the one of the weather shown.
          settings_->place.assign(goodPlace_.c_str());
          settings_->lat = goodLat_;
          settings_->lon = goodLon_;
          settings_.touch();
        } else {
          searchAgainMs_ = nowMs_ + kRetryMs;   // the service did not answer: once more, later
          searchWaits_ = true;
        }
      }
      markChanged();
      searchDone(answered ? Status::NotFound : Status::Io);
      return;
    }
    const Town& town = finder_.at(0);
    char label[49];
    snprintf(label, sizeof(label), town.country[0] ? "%s, %s" : "%s", town.name, town.country);
    settings_->place.assign(label);
    settings_->lat = town.lat;
    settings_->lon = town.lon;
    settings_.touch();
    keepPlace();
    LMP_LOGI(ctx().log, "place %s", label);
    forced_ = true;
    problem_ = "";
    searched_ = "found";
    markChanged();
    searchDone(Status::Ok);
  }

  void failed(const char* why) {
    nextMs_ = nowMs_ + kRetryMs;
    forced_ = false;
    problem(why);
  }

  void problem(const char* why) {
    if (strcmp(problem_, why) == 0) return;
    problem_ = why;
    markChanged();
  }

  // ---- the screen: the sky, the temperature and the humidity, as large as they fit
  void paint() {
    if (!screen_.ready() || !settings_->screen) return;
    screen_.clear();
    char temp[8] = "--", hum[8] = "--";
    if (have_) {
      screen_.draw(kIcons[static_cast<uint8_t>(skyOf(now_.code, now_.day))], 0, 8);
      snprintf(temp, sizeof(temp), "%ld", lround(now_.temp));
      snprintf(hum, sizeof(hum), "%u", now_.humidity);
    }
    // Right of the picture, 78 pixels: the temperature on top, the humidity below.
    constexpr int kLeft = 50, kRoom = Oled::kWidth - kLeft;
    const char unit[2] = {settings_->fahrenheit ? 'F' : 'C', 0};
    const int digits = Oled::textWidth(kBig, kBigChars, temp, 2);
    const int degree = Oled::textWidth(kBig, kBigChars, "o", 0);
    const int letter = Oled::textWidth(kMid, kMidChars, unit, 0);
    const bool withUnit = digits + 2 + degree + 1 + letter <= kRoom;
    const int wide = digits + 2 + degree + (withUnit ? 1 + letter : 0);
    int x = kLeft + (kRoom - wide) / 2;
    x += screen_.text(kBig, kBigChars, x, 2, temp, 2) + 2;
    x += screen_.text(kBig, kBigChars, x, 2, "o", 0) + 1;
    if (withUnit) screen_.text(kMid, kMidChars, x, 2, unit, 0);

    char line[12];
    snprintf(line, sizeof(line), "%s%%", hum);
    const int text = Oled::textWidth(kMid, kMidChars, line, 2);
    x = kLeft + (kRoom - (kDrop.w + 3 + text)) / 2;
    screen_.draw(kDrop, x, 43);
    screen_.text(kMid, kMidChars, x + kDrop.w + 3, 42, line, 2);
    screen_.show();
  }

  lmp::Kernel* kernel_ = nullptr;
  Settings settings_;
  Oled screen_;
  PlaceFinder finder_;
  Now now_;
  Day days_[kDays];
  lmp::FixedString<48> search_;   // a city to look for
  lmp::FixedString<2> searchLang_;
  uint32_t searchRef_ = 0;        // who waits for the outcome of the search
  bool bySpace_ = false;          // the country was taken after a space, not after a comma
  bool wholeName_ = false;        // second attempt: the whole text is the name
  bool fromSettings_ = false;     // the city being looked for was written in the settings
  // The place the weather is asked for: a name and its coordinates, as they were last good.
  lmp::FixedString<48> goodPlace_;
  float goodLat_ = 0, goodLon_ = 0;
  uint32_t searchAgainMs_ = 0;
  bool searchWaits_ = false;      // a search of the settings failed: the next one at searchAgainMs_
  uint8_t langIndex_ = 0;         // the language of the search in progress, among langCount_
  uint8_t langCount_ = 0;
  uint8_t answers_ = 0;           // how many of the languages got an answer
  char url_[400] = "";
  char* answer_ = nullptr;
  const char* problem_ = "";
  const char* searched_ = "";
  uint32_t nowMs_ = 0, gotMs_ = 0, nextMs_ = 0;   // nextMs_: when to ask again
  // The search of the screen: the place where the bus is, or goes next, among kScreenPlaces.
  uint32_t screenAtMs_ = 0;       // when to look again
  uint8_t screenAt_ = 0;
  uint8_t screenPlace_ = 0;       // where the screen answered the last time: kept on file
  uint8_t screenVisits_ = 0;      // places still to visit, this one too. 0: no search
  uint8_t screenLooks_ = 0;       // looks left in this place
  bool screenOpen_ = false;       // the bus is open, by this module, on the place
  Status screenWhy_ = Status::NotFound;
  uint8_t dayCount_ = 0;
  bool have_ = false, asked_ = false, asking_ = false, forced_ = false;
};

void Settings::applied() {
  if (owner) owner->onSettings();
}

}  // namespace
}  // namespace meteowifi

LMP_REGISTER_MODULE(meteowifi, meteowifi::MeteoWifi)

#endif  // LMP_FEATURE_HTTPCLIENT && LMP_FEATURE_BUS
