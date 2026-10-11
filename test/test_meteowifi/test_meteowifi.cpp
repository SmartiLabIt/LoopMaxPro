// Tests on the PC of MeteoWifi, the free module of the weather: what it asks the services,
// how it reads their answers, the document the console draws. No screen on the PC.
#include "../support/online_system.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace lmp;
using namespace lmp::test;

lmp::sdk::Module& lmp_module_meteowifi();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kForecast = "http://api.open-meteo.com/v1/forecast*";
constexpr const char* kSearch = "http://geocoding-api.open-meteo.com/v1/search*";

// Night, clear; tomorrow a storm, then rain.
constexpr const char* kWeather =
    "{\"current\":{\"temperature_2m\":18.2,\"relative_humidity_2m\":90,"
    "\"apparent_temperature\":20.1,\"weather_code\":0,\"is_day\":0,\"wind_speed_10m\":2.5},"
    "\"daily\":{\"time\":[\"2026-09-29\",\"2026-09-30\",\"2026-10-01\"],"
    "\"weather_code\":[1,95,61],\"temperature_2m_max\":[27.3,22.4,19.0],"
    "\"temperature_2m_min\":[15.8,14.1,12.0],\"precipitation_probability_max\":[0,70,85]}}";

// Three towns: a small one with the very name, a larger one whose name only begins with it,
// the city with the very name.
constexpr const char* kTowns =
    "{\"results\":["
    "{\"name\":\"Milano\",\"latitude\":30.71,\"longitude\":-96.86,\"country_code\":\"US\","
    "\"population\":421,\"postcodes\":[\"76556\"]},"
    "{\"name\":\"Milan\\\"\\u00f3wek\",\"latitude\":52.12,\"longitude\":20.67,"
    "\"country_code\":\"PL\",\"population\":16000},"
    "{\"name\":\"Milano\",\"latitude\":45.46427,\"longitude\":9.18951,\"country_code\":\"IT\","
    "\"population\":1371498}],\"generationtime_ms\":0.5}";

void withProbe(Config& config) { config.net.probeUrl.assign(kProbeUrl); }

struct Meteo {
  OnlineSystem sys{true, withProbe, 1000, {&lmp_module_meteowifi()}};
  Meteo() {
    sys.pc().httpClient.when(kProbeUrl, 204, "");
    sys.pc().httpClient.when(kForecast, 200, kWeather);
    sys.pc().httpClient.when(kSearch, 200, kTowns);
    TEST_ASSERT_TRUE(sys.command("mod.meteowifi", "config.reset", "{}") == Status::Ok);
  }
  // The last address asked that begins this way.
  std::string last(const char* begins) {
    std::string found;
    for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
      if (s.url.compare(0, strlen(begins), begins) == 0) found = s.url;
    }
    return found;
  }
  size_t asked(const char* begins) {
    size_t n = 0;
    for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
      if (s.url.compare(0, strlen(begins), begins) == 0) ++n;
    }
    return n;
  }
};

bool has(const std::string& text, const char* piece) { return text.find(piece) != std::string::npos; }

// True when one of the addresses asked that begin this way has the piece.
bool anyAsked(Meteo& m, const char* begins, const char* piece) {
  for (const hal::native::HttpSeen& s : m.sys.pc().httpClient.seen()) {
    if (s.url.compare(0, strlen(begins), begins) == 0 && has(s.url, piece)) return true;
  }
  return false;
}

// An answer of the service as it really is, saved next to this file (2026-10-01).
std::string real(const char* name) {
  std::string path(__FILE__);
  path = path.substr(0, path.find_last_of("/\\") + 1) + "real/" + name + ".json";
  std::ifstream in(path, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(in.good(), path.c_str());
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

constexpr const char* kGeo = "http://geocoding-api.open-meteo.com";

constexpr uint8_t kScreen = 0x3C;

// A screen wired on two pins. The bus of the PC does not know pins, its chips answer wherever
// it is opened: the chip is put there only while the bus is on those pins.
void wireScreen(Meteo& m, uint8_t sda, uint8_t scl) {
  hal::native::I2c& bus = m.sys.pc().i2c[0];
  if (bus.up() && bus.sda() == sda && bus.scl() == scl) {
    bus.addDevice(kScreen);
  } else {
    bus.removeDevice(kScreen);
  }
}

}  // namespace

void test_without_a_place_it_waits() {
  Meteo m;
  m.sys.advance(5000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["problem"].stringEquals("place"));
  TEST_ASSERT_FALSE(doc.v["now"].valid());
  TEST_ASSERT_EQUAL(0, m.asked("http://api.open-meteo.com"));
}

void test_weather_of_a_city_in_the_shapes_of_the_console() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.8919,\"lon\":12.5113}") ==
                   Status::Ok);
  m.sys.advance(3000);
  const std::string url = m.last("http://api.open-meteo.com");
  TEST_ASSERT_TRUE(has(url, "latitude=41.8919&longitude=12.5113"));
  TEST_ASSERT_TRUE(has(url, "forecast_days=7"));
  TEST_ASSERT_FALSE(has(url, "fahrenheit"));

  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_FALSE(doc.v["problem"].valid());
  const JsonValue now = doc.v["now"];
  TEST_ASSERT_TRUE(now["icon"].stringEquals("moon"));   // clear, at night
  TEST_ASSERT_EQUAL_DOUBLE(18.2, now["value"].asDouble());
  TEST_ASSERT_TRUE(now["unit"].stringEquals("\xC2\xB0" "C"));
  TEST_ASSERT_TRUE(now["label"].stringEquals("Roma"));
  TEST_ASSERT_EQUAL(3, now["items"].size());
  TEST_ASSERT_TRUE(now["items"].at(1)["key"].stringEquals("humidity"));
  TEST_ASSERT_EQUAL(90, now["items"].at(1)["value"].asInt());

  const JsonValue days = doc.v["days"];
  TEST_ASSERT_EQUAL(3, days.size());
  TEST_ASSERT_TRUE(days.at(0)["date"].stringEquals("2026-09-29"));
  TEST_ASSERT_TRUE(days.at(0)["icon"].stringEquals("sun"));
  TEST_ASSERT_TRUE(days.at(1)["icon"].stringEquals("cloud-lightning"));
  TEST_ASSERT_TRUE(days.at(2)["icon"].stringEquals("cloud-rain"));
  TEST_ASSERT_EQUAL_DOUBLE(22.4, days.at(1)["hi"].asDouble());
  TEST_ASSERT_EQUAL(85, days.at(2)["items"].at(0)["value"].asInt());
}

void test_asked_again_only_at_its_time() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.9,\"lon\":12.5,\"everyMin\":5}") ==
                   Status::Ok);
  m.sys.advance(3000);
  const size_t first = m.asked("http://api.open-meteo.com");
  TEST_ASSERT_EQUAL(1, first);
  m.sys.advance(4 * 60000);
  TEST_ASSERT_EQUAL(1, m.asked("http://api.open-meteo.com"));
  m.sys.advance(62000);
  TEST_ASSERT_EQUAL(2, m.asked("http://api.open-meteo.com"));
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "refresh", "{}") == Status::Ok);
  m.sys.advance(2000);
  TEST_ASSERT_EQUAL(3, m.asked("http://api.open-meteo.com"));
}

void test_the_city_with_the_very_name_and_the_most_people() {
  Meteo m;
  m.sys.pc().httpClient.setChunk(7);   // the answer arrives in small pieces
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Milano\",\"lang\":\"it\"}",
                                 nullptr, &ref) == Status::Pending);
  m.sys.advance(4000);
  // In the language of who asks, then in English: the service knows a name in one language only.
  TEST_ASSERT_TRUE(anyAsked(m, kGeo, "name=Milano&count=10&language=it"));
  TEST_ASSERT_TRUE(anyAsked(m, kGeo, "name=Milano&count=10&language=en"));
  TEST_ASSERT_FALSE(anyAsked(m, kGeo, "countryCode"));
  // Who asked is told that the city was found.
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(m.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);

  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["place"].stringEquals("Milano, IT"));
  TEST_ASSERT_TRUE(doc.v["search"].stringEquals("found"));
  TEST_ASSERT_EQUAL_FLOAT(45.46427f, static_cast<float>(doc.v["config"]["lat"].asDouble()));
  // Then the weather of that place.
  TEST_ASSERT_TRUE(has(m.last("http://api.open-meteo.com"), "latitude=45.4643"));
}

void test_a_country_narrows_the_search() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"San Marco, IT\"}") ==
                   Status::Pending);
  m.sys.advance(4000);
  const std::string search = m.last("http://geocoding-api.open-meteo.com");
  TEST_ASSERT_TRUE(has(search, "name=San%20Marco&"));
  TEST_ASSERT_TRUE(has(search, "&countryCode=IT"));
  // The language of the device, and the one of the country written.
  TEST_ASSERT_TRUE(anyAsked(m, kGeo, "language=en"));
  TEST_ASSERT_TRUE(anyAsked(m, kGeo, "language=it"));
  TEST_ASSERT_EQUAL(2, m.asked(kGeo));
}

// The ESP32-C3 on 2026-10-01, with the real answers of the service. The device speaks English,
// where the capital is "Rome": asked for "Roma" in English the service gives three hamlets, and
// the capital only in Italian. Each town comes with a long list of postcodes.
void test_roma_and_milano_as_the_service_really_answers() {
  struct Case {
    const char* written;
    const char* name;
    const char* english;
    const char* italian;
    const char* place;
    float lat;
  };
  const Case cases[] = {
      {"Roma IT", "Roma", "roma_en_it", "roma_it_it", "Roma, IT", 41.89193f},
      {"Milano, IT", "Milano", "milano_en_it", "milano_it_it", "Milano, IT", 45.46427f},
  };
  for (const Case& c : cases) {
    Meteo m;
    m.sys.pc().httpClient.setChunk(64);
    const std::string asked = std::string("http://geocoding-api.open-meteo.com/v1/search?name=") +
                              c.name + "&count=10&language=";
    m.sys.pc().httpClient.when((asked + "en*").c_str(), 200, real(c.english));
    m.sys.pc().httpClient.when((asked + "it*").c_str(), 200, real(c.italian));
    const std::string args = std::string("{\"place\":\"") + c.written + "\"}";
    TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set", args.c_str()) == Status::Ok);
    m.sys.advance(6000);
    Doc doc(m.sys.kernel(), "mod.meteowifi");
    TEST_ASSERT_TRUE_MESSAGE(doc.v["place"].stringEquals(c.place), doc.text());
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, c.lat,
                                     static_cast<float>(doc.v["config"]["lat"].asDouble()), c.written);
  }
}

// As the owner wrote them on the ESP32-C3, 2026-10-01: "Roma IT", "Milano IT", "Helsinki FI".
// Sent whole as a name the service found nothing, and the weather stayed the one of before.
void test_a_country_written_without_the_comma() {
  const char* written[] = {"Milano IT", "Milano it", "Milano,IT", "Milano , IT", "  Milano IT"};
  for (const char* text : written) {
    Meteo m;
    const std::string args = std::string("{\"name\":\"") + text + "\"}";
    uint32_t ref = 0;
    TEST_ASSERT_TRUE_MESSAGE(m.sys.command("mod.meteowifi", "location", args.c_str(), nullptr,
                                           &ref) == Status::Pending, text);
    m.sys.advance(4000);
    const std::string search = m.last("http://geocoding-api.open-meteo.com");
    TEST_ASSERT_TRUE_MESSAGE(has(search, "name=Milano&") || has(search, "name=%20%20Milano&"), text);
    TEST_ASSERT_TRUE_MESSAGE(has(search, "&countryCode=IT"), text);
    Doc doc(m.sys.kernel(), "mod.meteowifi");
    TEST_ASSERT_TRUE_MESSAGE(doc.v["place"].stringEquals("Milano, IT"), text);
    TEST_ASSERT_TRUE_MESSAGE(has(m.last("http://api.open-meteo.com"), "latitude=45.4643"), text);
    Status outcome = Status::Error;
    TEST_ASSERT_TRUE_MESSAGE(m.sys.done(ref, outcome) && outcome == Status::Ok, text);
  }
}

// Two letters at the end can be part of the name: nothing found in that "country", the whole
// text is tried as the name.
void test_two_letters_that_are_not_a_country() {
  Meteo m;
  m.sys.pc().httpClient.when("http://geocoding-api.open-meteo.com/v1/search?name=Milano&*", 200,
                             "{\"generationtime_ms\":0.3}");
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Milano Xy\"}", nullptr,
                                 &ref) == Status::Pending);
  m.sys.advance(8000);
  // Milano in the country "XY", in two languages; then the whole text.
  TEST_ASSERT_EQUAL(3, m.asked("http://geocoding-api.open-meteo.com"));
  TEST_ASSERT_TRUE(anyAsked(m, kGeo, "name=Milano&count=10&language=en&format=json&countryCode=XY"));
  const std::string second = m.last("http://geocoding-api.open-meteo.com");
  TEST_ASSERT_TRUE(has(second, "name=Milano%20Xy&"));
  TEST_ASSERT_FALSE(has(second, "countryCode"));
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(m.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
}

// The ESP32-C3 on 2026-10-01: the city written in the settings, where it was saved as a name
// with the coordinates of before, 0 and 0. Helsinki, Milano and Roma all had the weather of
// a point in the ocean.
void test_a_city_written_in_the_settings_is_looked_for() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set", "{\"place\":\"Milano, IT\"}") ==
                   Status::Ok);
  m.sys.advance(5000);
  const std::string search = m.last("http://geocoding-api.open-meteo.com");
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE_MESSAGE(has(search, "name=Milano&"), (search + " | " + doc.text()).c_str());
  TEST_ASSERT_TRUE(has(search, "&countryCode=IT"));
  TEST_ASSERT_TRUE(doc.v["place"].stringEquals("Milano, IT"));
  TEST_ASSERT_EQUAL_FLOAT(45.46427f, static_cast<float>(doc.v["config"]["lat"].asDouble()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.18951f, static_cast<float>(doc.v["config"]["lon"].asDouble()));
  // The weather is the one of the city, never the one of latitude 0 and longitude 0.
  TEST_ASSERT_TRUE(has(m.last("http://api.open-meteo.com"), "latitude=45.4643&longitude=9.1895"));
  for (const hal::native::HttpSeen& s : m.sys.pc().httpClient.seen()) {
    TEST_ASSERT_FALSE_MESSAGE(has(s.url, "latitude=0.0000&longitude=0.0000"), s.url.c_str());
  }
  // Another city, then: looked for once, not at every round.
  const size_t searches = m.asked("http://geocoding-api.open-meteo.com");
  m.sys.advance(20000);
  TEST_ASSERT_EQUAL(searches, m.asked("http://geocoding-api.open-meteo.com"));
  // Other settings do not start a search.
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set", "{\"everyMin\":30}") == Status::Ok);
  m.sys.advance(3000);
  TEST_ASSERT_EQUAL(searches, m.asked("http://geocoding-api.open-meteo.com"));
}

void test_a_city_of_the_settings_that_is_not_there() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.9,\"lon\":12.5}") == Status::Ok);
  m.sys.advance(3000);
  m.sys.pc().httpClient.when(kSearch, 200, "{\"generationtime_ms\":0.3}");
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set", "{\"place\":\"Nowhere\"}") ==
                   Status::Ok);
  m.sys.advance(5000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  // The name shown is the one of the weather shown: the place of before comes back.
  TEST_ASSERT_TRUE(doc.v["place"].stringEquals("Roma"));
  TEST_ASSERT_TRUE(doc.v["search"].stringEquals("not-found"));
  TEST_ASSERT_TRUE(has(m.last("http://api.open-meteo.com"), "latitude=41.9000&longitude=12.5000"));
  m.sys.advance(20000);
  TEST_ASSERT_EQUAL(1, m.asked("http://geocoding-api.open-meteo.com"));
}

// A board that was left with a name and no coordinates by the firmware of before heals itself.
void test_a_place_saved_without_coordinates_heals() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Milano, IT\",\"lat\":0,\"lon\":0}") == Status::Ok);
  m.sys.advance(5000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_EQUAL_FLOAT(45.46427f, static_cast<float>(doc.v["config"]["lat"].asDouble()));
  TEST_ASSERT_TRUE(has(m.last("http://api.open-meteo.com"), "latitude=45.4643"));
}

// On from the factory only where the build gives the pins of a screen: on the ESP32-C3, which
// has none, the console showed the switch on (2026-10-01).
void test_the_screen_is_on_only_where_there_is_one() {
  Meteo m;
  Doc doc(m.sys.kernel(), "mod.meteowifi");
#if defined(LMP_METEOWIFI_SDA) && defined(LMP_METEOWIFI_SCL)
  const bool wired = LMP_METEOWIFI_SDA >= 0 && LMP_METEOWIFI_SCL >= 0;
#else
  const bool wired = false;
#endif
  TEST_ASSERT_TRUE(wired == doc.v["config"]["screen"].asBool());
}

// The screen of the board. On the PC the module has its pins as on a board with two places
// (module.json, "native"): 30 and 31, then 32 and 33.
void test_the_screen_is_found_on_the_pins_of_the_board() {
  Meteo m;
  {
    // Nothing answers yet: the module works without a screen.
    Doc doc(m.sys.kernel(), "mod.meteowifi");
    TEST_ASSERT_FALSE(doc.v["screen"].asBool(true));
  }
  m.sys.pc().i2c[0].addDevice(kScreen);
  m.sys.advance(1500);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["screen"].asBool(false));
  TEST_ASSERT_EQUAL_UINT8(30, m.sys.pc().i2c[0].sda());
  TEST_ASSERT_EQUAL_UINT8(31, m.sys.pc().i2c[0].scl());
  TEST_ASSERT_TRUE(m.sys.logHas("screen at 0x3C, on pins 30 and 31"));
}

// Boards that look the same carry the screen on different pins (NodeMCU V3: 4 and 5, or 14
// and 12): the module asks in the first place, then in the second, and keeps where it answered.
void test_a_board_with_the_screen_in_its_second_place() {
  Meteo m;
  for (int second = 0; second < 8 && !m.sys.logHas("screen at"); ++second) {
    wireScreen(m, 32, 33);
    m.sys.advance(1000);
  }
  {
    Doc doc(m.sys.kernel(), "mod.meteowifi");
    TEST_ASSERT_TRUE(doc.v["screen"].asBool(false));
    TEST_ASSERT_EQUAL_UINT8(32, m.sys.pc().i2c[0].sda());
    TEST_ASSERT_EQUAL_UINT8(33, m.sys.pc().i2c[0].scl());
    TEST_ASSERT_TRUE(m.sys.logHas("screen at 0x3C, on pins 32 and 33"));
  }
  // The place is on file a moment later. A module that starts again goes there at once: the
  // chip of the PC answers on any pins, so the first place asked is the one that finds it.
  m.sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 1000);
  TEST_ASSERT_TRUE(m.sys.command("sys.modules", "restart", "{\"module\":\"meteowifi\"}") ==
                   Status::Ok);
  m.sys.advance(200);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["screen"].asBool(false));
  TEST_ASSERT_EQUAL_UINT8(32, m.sys.pc().i2c[0].sda());
  TEST_ASSERT_EQUAL_UINT8(33, m.sys.pc().i2c[0].scl());
}

// No screen anywhere: the module says so once and leaves the bus to who wants to look.
void test_without_a_screen_the_bus_is_left_free() {
  Meteo m;
  m.sys.advance(8000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_FALSE(doc.v["screen"].asBool(true));
  TEST_ASSERT_TRUE(m.sys.logHas("no screen at 0x3C: not_found"));
  TEST_ASSERT_FALSE(m.sys.pc().i2c[0].up());
  TEST_ASSERT_TRUE(m.sys.command("sys.bus", "i2cOpen", "{\"sda\":32,\"scl\":33}") == Status::Ok);
  // And it works all the same.
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Milano\"}") ==
                   Status::Pending);
}

// Pins that are of somebody else: said, and the other place is asked.
void test_a_place_whose_pins_are_taken() {
  Meteo m;
  m.sys.advance(8000);
  TEST_ASSERT_TRUE(m.sys.logHas("no screen at 0x3C"));
  // The bus is free now: somebody else takes it, on other pins.
  TEST_ASSERT_TRUE(m.sys.command("sys.bus", "i2cOpen", "{\"sda\":34,\"scl\":35}") == Status::Ok);
  TEST_ASSERT_TRUE(m.sys.command("sys.modules", "restart", "{\"module\":\"meteowifi\"}") ==
                   Status::Ok);
  m.sys.advance(3000);
  TEST_ASSERT_TRUE(m.sys.logHas("screen: bus not opened on 30 and 31: busy"));
  TEST_ASSERT_TRUE(m.sys.logHas("screen: bus not opened on 32 and 33: busy"));
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_FALSE(doc.v["screen"].asBool(true));
}

void test_nothing_found() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.9,\"lon\":12.5}") == Status::Ok);
  m.sys.advance(3000);
  m.sys.pc().httpClient.when(kSearch, 200, "{\"generationtime_ms\":0.3}");
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Nowhere\"}", nullptr,
                                 &ref) == Status::Pending);
  m.sys.advance(4000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["search"].stringEquals("not-found"));
  // The place of before stays, and who asked is told that nothing was found.
  TEST_ASSERT_TRUE(doc.v["place"].stringEquals("Roma"));
  Status outcome = Status::Ok;
  TEST_ASSERT_TRUE(m.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::NotFound);
  // A failed search is not repeated by itself.
  m.sys.advance(10000);
  TEST_ASSERT_EQUAL(1, m.asked("http://geocoding-api.open-meteo.com"));
  // The next one starts.
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Nowhere\"}") ==
                   Status::Pending);
}

void test_a_search_service_that_does_not_answer() {
  Meteo m;
  m.sys.pc().httpClient.whenFails(kSearch, Status::Timeout);
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "location", "{\"name\":\"Milano IT\"}", nullptr,
                                 &ref) == Status::Pending);
  m.sys.advance(4000);
  Status outcome = Status::Ok;
  TEST_ASSERT_TRUE(m.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Io);
  TEST_ASSERT_EQUAL(2, m.asked("http://geocoding-api.open-meteo.com"));   // two languages
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["search"].stringEquals("failed"));
}

void test_fahrenheit() {
  Meteo m;
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.9,\"lon\":12.5}") == Status::Ok);
  m.sys.advance(3000);
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "units", "{\"fahrenheit\":true}") ==
                   Status::Ok);
  m.sys.advance(2000);
  TEST_ASSERT_TRUE(has(m.last("http://api.open-meteo.com"), "&temperature_unit=fahrenheit"));
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["units"].stringEquals("F"));
  TEST_ASSERT_TRUE(doc.v["now"]["unit"].stringEquals("\xC2\xB0" "F"));
  TEST_ASSERT_TRUE(doc.v["config"]["fahrenheit"].asBool());
}

void test_a_server_that_fails_is_asked_again_a_minute_later() {
  Meteo m;
  m.sys.pc().httpClient.when(kForecast, 500, "");
  TEST_ASSERT_TRUE(m.sys.command("mod.meteowifi", "config.set",
                                 "{\"place\":\"Roma\",\"lat\":41.9,\"lon\":12.5}") == Status::Ok);
  m.sys.advance(3000);
  Doc doc(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["problem"].stringEquals("server"));
  TEST_ASSERT_EQUAL(1, m.asked("http://api.open-meteo.com"));
  m.sys.pc().httpClient.when(kForecast, 200, kWeather);
  m.sys.advance(30000);
  TEST_ASSERT_EQUAL(1, m.asked("http://api.open-meteo.com"));
  m.sys.advance(32000);
  TEST_ASSERT_EQUAL(2, m.asked("http://api.open-meteo.com"));
  Doc after(m.sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_FALSE(after.v["problem"].valid());
  TEST_ASSERT_TRUE(after.v["now"].valid());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_a_place_it_waits);
  RUN_TEST(test_weather_of_a_city_in_the_shapes_of_the_console);
  RUN_TEST(test_asked_again_only_at_its_time);
  RUN_TEST(test_the_city_with_the_very_name_and_the_most_people);
  RUN_TEST(test_a_country_narrows_the_search);
  RUN_TEST(test_roma_and_milano_as_the_service_really_answers);
  RUN_TEST(test_a_city_written_in_the_settings_is_looked_for);
  RUN_TEST(test_a_city_of_the_settings_that_is_not_there);
  RUN_TEST(test_a_place_saved_without_coordinates_heals);
  RUN_TEST(test_the_screen_is_on_only_where_there_is_one);
  RUN_TEST(test_the_screen_is_found_on_the_pins_of_the_board);
  RUN_TEST(test_a_board_with_the_screen_in_its_second_place);
  RUN_TEST(test_without_a_screen_the_bus_is_left_free);
  RUN_TEST(test_a_place_whose_pins_are_taken);
  RUN_TEST(test_a_country_written_without_the_comma);
  RUN_TEST(test_two_letters_that_are_not_a_country);
  RUN_TEST(test_nothing_found);
  RUN_TEST(test_a_search_service_that_does_not_answer);
  RUN_TEST(test_fahrenheit);
  RUN_TEST(test_a_server_that_fails_is_asked_again_a_minute_later);
  return UNITY_END();
}
