<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/writing-a-module.svg" alt="Writing a module" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="modules-sdk.md">← Modules and SDK</a> &nbsp;·&nbsp;
<a href="web-console.md">Web console →</a>
</p>

---

A walkthrough from an empty folder to a working module. The smallest case is `demo`, the module
of the simulator: two outputs, a button, three settings and one command in about a hundred
lines. The real case is MeteoWifi: a module that uses the network and owns a component on the
board, an OLED display. The page is for anyone who wants to give a board a purpose of their
own; the reference for everything used here is [Modules and SDK](modules-sdk.md).

- [What you write](#what-you-write)
- [The folder](#the-folder)
- [The smallest module: demo](#the-smallest-module-demo)
- [MeteoWifi, step by step](#meteowifi-step-by-step)
- [Pins](#pins)
- [Build it into a firmware](#build-it-into-a-firmware)
- [Try it on the PC](#try-it-on-the-pc)
- [Checklist](#checklist)

## What you write

How much there is to write depends on how much of the module is already in the SDK.

| The module | You write | Example |
|------------|-----------|---------|
| Is made of parts of the SDK: channels, schedules, lights | A manifest and one function, `onInit`, which takes the parts in | [Zappy 2](../src/modules/zappy2/src/zappy2.cpp), 47 lines |
| Has a state, settings and commands of its own | Two structs with their field tables, the commands, a periodic `onTick` | [demo](../test/modules/demo/src/demo.cpp) |
| Uses services and owns a component | The life cycle, requests to the services, the driver of the component | [MeteoWifi](../src/modules/meteowifi/src/meteowifi.cpp) |

In all three cases the system gives the rest: the JSON document, the file of the settings, the
commands on every interface, the page in the console.

## The folder

```
src/modules/meteowifi/
├── module.json            the manifest
├── README.md              the page of the module
├── CHANGES.md             its versions
├── src/
│   ├── meteowifi.cpp      the module
│   ├── oled.h, oled.cpp   the driver of the display
│   └── art.h              icons and digits, written by tools/make_art.py
├── tools/make_art.py      draws the pictures
└── ui/meteowifi.js        one more tab in the console
```

Only `module.json` and one source file are needed. Choose the name first: lower-case letters,
digits and underscore, a letter first, at most 24 characters. It is written in four places that
must agree: the folder, `name` in `module.json`, and the two macros `LMP_MODULE` and
`LMP_REGISTER_MODULE`.

Every `.cpp` under `src/modules/` is compiled in every build, for every board. Two things
follow: a module that needs a service some builds do not have guards its code (step 2 below),
and a module does not call a framework or a chip directly: it goes through the SDK and the
services.

## The smallest module: demo

`demo` lives in [test/modules/demo](../test/modules/demo), because it exists for the tests and
the simulator, but it is written as any module. Its manifest:

```json
{
  "name": "demo",
  "kind": "hardware",
  "title": "Demo",
  "version": "1.0.0",
  "icon": "🧪",
  "uiApi": 1,
  "ui": "ui/demo.js",
  "sources": ["src"],
  "params": { "pins": [22, 23], "button": -1 },
  "requires": ["gpio", "config"],
  "optional": ["clock"],
  "online": []
}
```

And its source, without the includes:

```cpp
// Parameters of the build: custom_lmp_modules = demo:pins=22|23;button=0
#ifndef LMP_DEMO_PINS
#define LMP_DEMO_PINS {22, 23}
#endif
#ifndef LMP_DEMO_BUTTON
#define LMP_DEMO_BUTTON -1
#endif

namespace {
constexpr uint8_t kChannels = 2;

struct DemoCfg { lmp::FixedString<32> label; uint16_t pulseMs; bool buttonToggles; };
struct DemoState { uint32_t presses; uint32_t pulses; };

constexpr lmp::ArgSpec kPulseArgs[] = { lmp::Arg::u8("channel", 0, kChannels - 1) };
}  // namespace

LMP_FIELDS(DemoCfg,
  LMP_F(label,         "label",         Str(0, 32, "Demo")),
  LMP_F(pulseMs,       "pulseMs",       U16(50, 60000, 1000)),
  LMP_F(buttonToggles, "buttonToggles", Bool(true)))

LMP_FIELDS(DemoState,
  LMP_F(presses, "presses", U32()),
  LMP_F(pulses,  "pulses",  U32()))

namespace {

class Demo final : public lmp::sdk::Module {
  LMP_MODULE("demo", "Demo", "\xF0\x9F\xA7\xAA", "1.0.0", lmp::sdk::ModuleKind::Hardware, 20)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const uint8_t map[kChannels] = LMP_DEMO_PINS;
    LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
    LMP_TRY(useSettings(cfg_));
    LMP_TRY(use(state_));
    LMP_TRY(use(pins_));
    LMP_TRY(use(schedules_));

    c.commands->add("pulse", LMP_HELP("Turns a channel on for the time of the settings"),
                    kPulseArgs, [this](const lmp::Args& a, lmp::CmdContext&) -> lmp::Status {
                      return pulse(a.u8("channel"));
                    });

    if (LMP_DEMO_BUTTON >= 0 && c.gpio) {
      LMP_TRY(button_.begin(*c.gpio, static_cast<uint8_t>(LMP_DEMO_BUTTON), true,
                            manifest().name));
    }
    return lmp::Status::Ok;
  }

  void onTick(uint32_t nowMs) override {
    nowMs_ = nowMs;
    for (uint8_t i = 0; i < kChannels; ++i) {
      if (pulseEnd_[i].due(nowMs)) pins_.set(i, false);
    }
    if (button_.poll(nowMs) == lmp::sdk::ButtonEvent::Pressed) {
      ++state_->presses;
      state_.touch();
      if (cfg_->buttonToggles) pins_.toggle(0);
    }
  }

  void onStop() override { button_.end(); }

 private:
  lmp::Status pulse(uint8_t channel) {
    LMP_TRY(pins_.set(channel, true));
    pulseEnd_[channel].arm(nowMs_, cfg_->pulseMs);
    ++state_->pulses;
    state_.touch();
    return lmp::Status::Ok;
  }

  lmp::sdk::ModuleConfig<DemoCfg> cfg_;
  lmp::sdk::State<DemoState> state_;
  lmp::sdk::PinBank<kChannels> pins_;
  lmp::sdk::PinSchedule<kChannels> schedules_{pins_};
  lmp::sdk::Button button_;
  lmp::sdk::Debounced pulseEnd_[kChannels];
  uint32_t nowMs_ = 0;
};

}  // namespace

LMP_REGISTER_MODULE(demo, Demo)
```

Reading it from the top:

1. **The build parameters** of `params` arrive as `LMP_DEMO_PINS` and `LMP_DEMO_BUTTON`. The
   source gives them a default, so that it compiles also in a firmware that does not name the
   module.
2. **Two structs and their tables** are the settings and the state. `LMP_FIELDS` is used at
   global scope, outside the anonymous namespace.
3. **`LMP_MODULE`** says what the module is and asks for a tick every 20 ms.
4. **`onInit`** declares the pins, which claims them for the module (`"Line %u"` gives the
   names the channels are born with), then takes the parts in. A bank is declared before it is
   taken in. `LMP_TRY` returns at once when a step fails: the module then goes to `Error` and
   what it had taken is given back.
5. **One command** of its own, `pulse`, with one checked argument.
6. **`onTick`** never waits: a `Debounced` remembers when a pulse ends, a `Button` is read at
   every round.
7. **`LMP_REGISTER_MODULE`** closes the file.

With those lines the device has the target `mod.demo` with:

| What | Comes from |
|------|------------|
| Commands `set`, `toggle`, `all`, `flip`, `name`, `invert`, `restore` | `PinBank` |
| Commands `sched.add`, `sched.update`, `sched.remove`, `sched.clear` | `PinSchedule` |
| Commands `config.set`, `config.reset`, and the settings form | `ModuleConfig` |
| Command `pulse`, shown as a button on each channel because its only argument is `channel` | The module |
| `presses` and `pulses` in the document, and the card "Values" of the console | `State` |
| The file `/cfg/m/demo.json`: settings, names, levels, schedules | The parts |
| Two switches in Home Assistant, `ch0` and `ch1`, where the build has MQTT | `PinBank` |

## MeteoWifi, step by step

[MeteoWifi](../src/modules/meteowifi/README.md) asks Open-Meteo for the weather of a place and
the forecast of seven days, shows them in the console and draws sky, temperature and humidity
on a 0.96" display. It is built on one idea: **the system gives the services, the module owns
the component.** The OS does not know the display exists: it gives the I2C bus, shared and with
its pins checked, and the driver is in the module. The same for the network: the module opens
no socket, it asks the service of the requests.

### Step 1. The manifest

```json
"params": { "sda": -1, "scl": -1, "sda2": -1, "scl2": -1, "address": 60 },
"requires": ["net", "httpclient", "bus", "config"],
"optional": ["geo", "clock"],
"online": ["api.open-meteo.com", "geocoding-api.open-meteo.com"]
```

- `params` become `LMP_METEOWIFI_SDA`, `LMP_METEOWIFI_SCL`, `LMP_METEOWIFI_SDA2`,
  `LMP_METEOWIFI_SCL2` and `LMP_METEOWIFI_ADDRESS`.
- `requires` turns on what the small boards leave out: `bus` defines `LMP_NEEDS_BUS` and
  `httpclient` defines `LMP_NEEDS_HTTPCLIENT`. An ESP8266 without this module pays for neither.

Every field is in [Modules and SDK](modules-sdk.md#the-manifest-modulejson).

### Step 2. Guards and build parameters

The module needs the client of web services and the bus, and a build that does not have them
must still compile the file:

```cpp
#include "lmp_config.h"

#if LMP_FEATURE_HTTPCLIENT && LMP_FEATURE_BUS

#ifndef LMP_METEOWIFI_SDA
#define LMP_METEOWIFI_SDA -1          // -1: no display
#endif
// ... the same for SCL, SDA2, SCL2 and ADDRESS (60)

// ... the whole module ...

LMP_REGISTER_MODULE(meteowifi, meteowifi::MeteoWifi)

#endif  // LMP_FEATURE_HTTPCLIENT && LMP_FEATURE_BUS
```

The registration is inside the guard too. So a module must be named only in the firmwares where
its features exist: that is what `families` and `editions` in the manifest tell.

### Step 3. The class

```cpp
class MeteoWifi final : public lmp::sdk::Module {
  LMP_MODULE("meteowifi", "MeteoWifi", "\xF0\x9F\x8C\xA4", "1.0.3",
             lmp::sdk::ModuleKind::Online, 1000)                 // one tick a second

 public:
  Status onInit(lmp::sdk::ModuleContext& c) override;            // settings, memory, commands
  Status onStart() override;                                     // looks for the display
  void onStop() override;                                        // gives the bus back
  void onTick(uint32_t nowMs) override;                          // decides when to ask
  void describe(lmp::JsonWriter& w) const override;              // the document
  void save(lmp::JsonWriter& w) const override;                  // where the display answered
  void load(const lmp::JsonValue& doc) override;
  void things(lmp::sdk::ThingSink& out) const override;          // for home automation
  Status thingSet(const char* id, const lmp::sdk::ThingAsk&) override;
};
```

Everything runs on the application task: no locks. `onInit` begins from nothing every time,
because a module can be started again: MeteoWifi clears its flags and its searches there before
anything else.

### Step 4. Settings

A native struct and its table. The file, the commands `config.set` and `config.reset` and the
form in the console come by themselves.

```cpp
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

LMP_FIELDS(meteowifi::Cfg,
  LMP_F(api,        "api",        Str(0, 96, "http://api.open-meteo.com/v1/forecast")),
  LMP_F(geoApi,     "geoApi",     Str(0, 96, "http://geocoding-api.open-meteo.com/v1/search")),
  LMP_F(place,      "place",      Str(0, 48, "")),
  LMP_F(lat,        "lat",        F32(-90, 90, 0)),
  LMP_F(lon,        "lon",        F32(-180, 180, 0)),
  LMP_F(everyMin,   "everyMin",   U16(5, 1440, 15)),
  LMP_F(fahrenheit, "fahrenheit", Bool(false)),
  LMP_F(screen,     "screen",     Bool(LMP_METEOWIFI_SDA >= 0 && LMP_METEOWIFI_SCL >= 0)))
```

A default can depend on the build: `screen` is on from the factory only where the build gives
the pins of a display. To follow the settings at once, the module derives the part and overrides
`applied()`:

```cpp
class Settings final : public lmp::sdk::ModuleConfig<Cfg> {
 public:
  MeteoWifi* owner = nullptr;

 protected:
  void applied() override;        // asks the weather again, turns the display on or off
};
```

### Step 5. Commands

```cpp
const lmp::ArgSpec kUnitArgs[] = {lmp::Arg::boolean("fahrenheit")};
const lmp::ArgSpec kPlaceArgs[] = {lmp::Arg::str("name", 0, 48).optional(),
                                   lmp::Arg::str("lang", 2, 2).optional()};

c.commands->add("location", LMP_HELP("Weather of a city; without a name, of the device"),
                kPlaceArgs, [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                  return choose(a.str("name"), a.str("lang"), ctx.ref);
                },
                lmp::kCmdAsync);
c.commands->add("units", LMP_HELP("Degrees Fahrenheit, or Celsius"), kUnitArgs, /* ... */);
c.commands->add("refresh", LMP_HELP("Asks the weather now"), /* ... */);
```

- The three work the same from the console, the HTTP API, the serial port and MQTT:
  `cmd mod.meteowifi location name=Dublin`.
- The search of a city takes a moment. So `location` is `kCmdAsync`: it answers
  `Status::Pending`, and when the search ends the module publishes
  `lmp::commandDone(ref, outcome)`. Who asked is told found, not found or failed.
- A text argument named `lang` is filled by the console with its own language.
- The handlers do not do the long work: `refresh` only sets a flag, and `onTick` asks the
  weather.

### Step 6. Data from the network

```cpp
lmp::HttpFetch what;
what.url = url_;
what.timeoutMs = timeoutMs;
if (search) {
  what.sink = [this](lmp::Span<const uint8_t> data) { return finder_.feed(data); };
} else {
  what.out = answer_;       // the whole answer in a buffer
  what.cap = kAnswer;
}
if (ctx().http->fetch(what, done) == Status::Ok) {
  asking_ = true;
} else {
  nextMs_ = nowMs_ + kRetryMs;
}
```

- **Ask only with Internet.** `onTick` checks `ctx().http->ready()` and `ctx().net->internet()`
  first, and says `problem: network` in the document otherwise.
- **One request at a time**, and after a failure the next one a minute later, not at every tick.
- **The answer arrives later**, in `done`, on the application task. Every text and buffer of the
  request must stay valid until then: they are members of the module.
- **Memory is taken once.** The forecast is about 1 KB and goes in a buffer of 1400 bytes taken
  in `onInit` with `lmp::hal::allocLarge`. The search of a city arrives in pieces through
  `sink`: the module reads one town at a time in 640 bytes and keeps the two most likely, so ten
  results do not cost ten times the memory. `sink` runs in the context of the request, not on
  the application task.
- **The answer is read with `lmp::JsonValue`**, a view over the text that takes no heap.
- **Plain HTTP**, which Open-Meteo allows, so that the ESP8266 reaches it too.

### Step 7. The document the console draws

The module writes no interface for its main page. In `describe` it gives its document two
shapes the console knows how to draw, `now` and `days`:

```json
"units": "C", "ageSec": 42, "screen": true,
"now": { "icon": "moon", "value": 18.2, "unit": "°C", "label": "Dublin, IE",
         "code": 0, "sky": "night",
         "items": [ { "key": "feels", "value": 20.1, "unit": "°C", "icon": "thermometer" },
                    { "key": "humidity", "value": 90, "unit": "%", "icon": "droplets" },
                    { "key": "wind", "value": 2.5, "unit": "km/h", "icon": "wind" } ] },
"days": [ { "date": "2026-03-14", "icon": "sun", "hi": 27.3, "lo": 15.8, "unit": "°C",
            "code": 1, "sky": "sun",
            "items": [ { "key": "rain", "value": 0, "unit": "%", "icon": "umbrella" } ] } ]
```

- `now` becomes a block with a large icon, the value and the small values; `days` a row of
  boxes. The icons are names of the kit of the console.
- The plain members (`units`, `ageSec`, `screen`, `place`, `problem`) go in the card "Values".
- `code` and `sky` are for who reads the document with a program: the console leaves them.
- When something is missing the document says why, in one word: `problem` is `network`, `place`,
  `server` or `answer`. Call `markChanged()` only when the word changes.

The document is written with `lmp::JsonWriter` (`w.str`, `w.f64`, `w.beginObject`,
`w.beginArray`): no JSON text is built by hand. The standard view is described in
[Web console](web-console.md#the-standard-view-of-a-module). MeteoWifi also brings one tab of
its own, `ui/meteowifi.js`, where the browser searches the city: see
[a page of its own](web-console.md#a-page-of-its-own-for-a-module).

### Step 8. The component on the board

```cpp
const Status s = ctx().bus->openI2c(0, sda, scl, 400000, "meteowifi");   // the bus of the system
// ...
screen_.begin(ctx().bus->i2c(0), LMP_METEOWIFI_ADDRESS);                 // the driver of the module
// ...
ctx().bus->closeI2c(0, "meteowifi");                                     // no display: the bus is free
```

- **A small driver.** [oled.cpp](../src/modules/meteowifi/src/oled.cpp) is about 130 lines: the start
  sequence of the SSD1306 and the image sent in pieces of 32 bytes, on top of
  `lmp::sdk::I2cDevice`, which counts the errors and knows whether the chip is there. It uses
  the bus object the system gives, never the I2C of a framework: the pins keep one owner and
  the module is the same on every board.
- **The image in memory** is 1 KB, taken only when the display answers.
- **A display that is not there is not an error.** The module asks for it up to three times in
  each place, a second apart; with no answer it closes the bus, says so once in the log and
  goes on without.
- **Pictures in flash.** Icons and digits are in `art.h`, kept in flash with `PROGMEM` on the
  Arduino builds. The header is written by a script, so the pictures are changed without an
  image editor: `python src/modules/meteowifi/tools/make_art.py --show` also prints them as
  text.
- **Draw only when the data change.** `paint()` is called when a forecast arrives, when the
  settings change and when the display is found, not at every tick.

### Step 9. What is kept between two starts

Some NodeMCU boards that look the same carry the display on pins 4 and 5, others on 14 and 12.
The module has a second place for it (`sda2`, `scl2`) and remembers which one answered. That is
not a setting, so it goes in the file through `save` and `load`:

```cpp
void save(lmp::JsonWriter& w) const override { w.u32("screenPlace", screenPlace_); }

void load(const lmp::JsonValue& doc) override {
  const int64_t place = doc["screenPlace"].asInt(0);
  screenPlace_ = place >= 0 && place < kScreenPlaceCount ? static_cast<uint8_t>(place) : 0;
}
```

When the display answers in a new place the module calls `markUnsaved()`, and the file follows.
**The file is read after `onInit`**: when `onInit` runs at the start of the device, the settings
still have their defaults and `load` has not run yet. That is why MeteoWifi looks for the
display in `onStart`.

### Step 10. Things for home automation

```cpp
void things(lmp::sdk::ThingSink& out) const override {
  const char* deg = settings_->fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
  out.number("temperature", "Temperature", now_.temp, deg, "temperature", 1, have_);
  out.range(0, 100);
  out.number("humidity", "Humidity", now_.humidity, "%", "humidity", 0, have_);
  out.button("refresh", "Refresh the weather");
  out.settable(0, 48);
  out.text("place", "City", settings_->place.c_str());
  out.settable();
  out.choice("units", "Degrees", settings_->fahrenheit ? "F" : "C", kUnits);
}
```

Five of the seven things of the module, with one name each (the source gives every name in
English and in Italian). `have_` is false until the first forecast: the value is then "not
known", which is not zero. The humidity says its limits. The city and the degrees can be set
from outside, as with the commands of step 5: `settable` says so, and what arrives in
`thingSet` was checked already, a city of 48 characters at most and one of the two values of
`kUnits`. The other half, `thingSet`, and what Home Assistant shows are in
[Modules and SDK](modules-sdk.md#things-what-a-module-tells-home-automation).

**Every module tells its things**: a home system, and a server that reads the device as data,
know nothing else of it.

## Pins

The pins of a module are not fixed in its code: they are parameters of the build, and the
manifest knows them board by board.

```json
"params":      { "sda": -1, "scl": -1, "sda2": -1, "scl2": -1, "address": 60 },
"pinParams":   { "sda": ["SDA"], "scl": ["SCL"],
                 "sda2": ["SDA, second place"], "scl2": ["SCL, second place"] },
"boardPins":   { "esp8266": { "sda": 4, "scl": 5 },
                 "board:esp8266_nodemcu": { "sda2": 14, "scl2": 12 },
                 "native": { "sda": 30, "scl": 31, "sda2": 32, "scl2": 33 } },
"pinOptional": ["sda", "scl", "sda2", "scl2"]
```

| Field | What to write |
|-------|---------------|
| `params` | A pin as `-1`, several pins as a list of `-1`: "the pins of the board" |
| `pinParams` | Which parameters are pins, with a label for each pin |
| `boardPins` | The pins by chip (`esp32`, `esp32c3`, `esp8266`...) and by board (`board:<name>`, the value of `LMP_BOARD`). The board wins over the chip. `native` gives the pins on the PC, for the tests and the simulator |
| `pinOptional` | The pins the module works without |

The build resolves every pin at -1 from `boardPins` and passes real numbers to the code. A pin
given in the build wins: `meteowifi:sda=21;scl=22`. So the code always receives a real pin, or
-1 when the board has none, and decides:

- MeteoWifi works without a display. On the ESP32 family it has no pins of its own, so that
  they stay free for the other modules: the display is used only when the build gives them.
- Zappy 2 cannot work without its relays. It logs what is missing and how to give it, and
  returns `Status::Invalid` from `onInit`:

```cpp
static const int given[kChannels] = LMP_ZAPPY2_PINS;
static uint8_t map[kChannels];
for (uint8_t i = 0; i < kChannels; ++i) {
  if (given[i] < 0) {
    LMP_LOGE(c.log, "no pin for line %u on this board: give them in the build, zappy2:pins=a|b", i + 1);
    return lmp::Status::Invalid;
  }
  map[i] = static_cast<uint8_t>(given[i]);
}
LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
```

Every pin has one owner. `PinBank::declare`, `Lights::declare`, `Button::begin` and the bus
claim their pins for the module and answer `Busy` when a pin belongs to someone else: two
modules of one firmware cannot drive the same pin by mistake. A pin taken by hand goes through
`ctx().gpio->claim(pin, mode, owner, label)`.

## Build it into a firmware

The environments of the boards name no module. Add an environment of your own, on top of the
one of your board, and name the modules in `custom_lmp_modules`:

```ini
[env:meteowifi_nodemcu]
extends = env:esp8266_nodemcu
custom_lmp_modules = meteowifi
```

```
pio run -e meteowifi_nodemcu                  build
pio run -e meteowifi_nodemcu -t upload        build and write the firmware
pio run -e meteowifi_nodemcu -t uploadfs      ESP8266 only: the console
```

On the ESP32 family the console travels inside the firmware and `upload` is all. On the ESP8266
it is in the filesystem image, and `uploadfs` replaces every file of the board, the settings
too: do it at the first installation.

The syntax of the option:

```ini
custom_lmp_modules = zappy2:pins=16|17, meteowifi:sda=21;scl=22
```

- Modules are separated by spaces, commas or new lines.
- The parameters of one module follow a colon and are separated by semicolons, with no space
  before them: in `platformio.ini` a semicolon after a space begins a comment.
- The values of a list are separated by a vertical bar.

To try a module without touching any file, put the same text in the environment variable
`LMP_MODULES`, which takes the place of the option:

```
LMP_MODULES="meteowifi:sda=21;scl=22" pio run -e esp32dev -t upload              # Linux, macOS
$env:LMP_MODULES = "meteowifi:sda=21;scl=22"; pio run -e esp32dev -t upload      # Windows PowerShell
```

What the build tells you:

| Line | Meaning |
|------|---------|
| `LoopMaxPro modules: meteowifi` | The modules of this firmware |
| `LoopMaxPro modules: <name>: no pin '<pin>' for <board> (<chip>): give it in the build, ...` | A pin the module needs has no value on this board. The build goes on |
| `LoopMaxPro tunings: ...` | The values of the OS a module asked for on this board, and for whom |
| `LoopMaxPro modules: module '<name>' has no parameter '<x>'. It has: ...` | The build stops: so it does for a module that is not there or is listed twice, a manifest whose `name` or `kind` is wrong, a value of the wrong type, two modules that do not go together |

More in [Build and flash](build.md#choosing-the-modules).

## Try it on the PC

The whole system runs on the PC, on a HAL that simulates the board. It is where a module is
written and tested before any board is connected.

### The simulator

```
pio run -e native
python tools/web/build.py --sim
.pio/build/native/program
```

(`program.exe` on Windows.) Start it from the root of the project, then open
`http://localhost:8088`. The `native` environment names `demo`, so its page is there with the
two channels, the `pulse` buttons, the schedules and the settings. What you type in the
terminal reaches the command line of the system:

```
man mod.demo
cmd mod.demo pulse channel=0
doc mod.demo
```

To see your module there, name it in `LMP_MODULES` before the build: for the `native`
environment the build also looks in `test/modules/`, and the pins come from the key `native`
of `boardPins`.

```
LMP_MODULES="demo:pins=40|41;button=42 meteowifi" pio run -e native              # Linux, macOS
$env:LMP_MODULES = "demo:pins=40|41;button=42 meteowifi"; pio run -e native      # Windows PowerShell
```

Nothing leaves the PC: a request to a server is answered only for the addresses the program
describes, in [src/main.cpp](../src/main.cpp), and with `404` for the others. So a module that
lives on a web service shows its page, its settings and its commands in the simulator, and gets
its answers in the tests. More on the simulator in [Build and flash](build.md#running-on-a-pc)
and [Web console](web-console.md#working-with-the-pc-simulator).

### The tests

A test starts a whole system with the module, decides what is on the network, lets the time
pass and reads the document. Condensed from
[test/test_meteowifi](../test/test_meteowifi/test_meteowifi.cpp):

```cpp
#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;

lmp::sdk::Module& lmp_module_meteowifi();

void withProbe(Config& config) { config.net.probeUrl.assign(kProbeUrl); }

void test_weather_of_a_city() {
  OnlineSystem sys{true, withProbe, 1000, {&lmp_module_meteowifi()}};
  sys.pc().httpClient.when(kProbeUrl, 204, "");                                  // Internet is there
  // kWeather: an answer of the service, as a text of the test
  sys.pc().httpClient.when("http://api.open-meteo.com/v1/forecast*", 200, kWeather);

  sys.command("mod.meteowifi", "config.set", "{\"place\":\"Roma\",\"lat\":41.8919,\"lon\":12.5113}");
  sys.advance(3000);                                                             // three seconds pass

  Doc doc(sys.kernel(), "mod.meteowifi");
  TEST_ASSERT_TRUE(doc.v["now"]["icon"].stringEquals("moon"));
}
```

```
pio test -e native -f test_meteowifi
```

The tests of MeteoWifi cover what a board makes hard to try: a server that fails and is asked
again a minute later, an answer that arrives in small pieces, a city that is not found, a
display on its second place, pins taken by someone else. The tests of the parts of the SDK are
in [test/test_sdk](../test/test_sdk/test_sdk.cpp), those of the life of a module in
[test/test_modules](../test/test_modules/test_modules.cpp).

## Checklist

Before calling a module done:

**Names and files**

- [ ] The name is the same in the folder, in `module.json`, in `LMP_MODULE` and in
      `LMP_REGISTER_MODULE`; `kind`, `title`, `version` and `icon` are the same in the manifest
      and in the macro.
- [ ] `module.json` says the truth: `requires`, `online`, `families`, `editions`, every pin
      parameter in `pinParams`, the pins of every board you tried in `boardPins`.
- [ ] A `README.md` and a `CHANGES.md`, as the other modules have.

**Code**

- [ ] The file compiles in every environment, also where the module does not run: what needs a
      service is inside `#if LMP_FEATURE_...`.
- [ ] Headers of LoopMaxPro only from `sdk/`, `services/`, `kernel/`, `base/` and `hal/api/`;
      nothing of a framework or of a chip outside a guard (MeteoWifi has one, for `PROGMEM`).
- [ ] Nothing waits: no `delay()`, no loop that waits for a chip or a server. When the ticks of
      the modules take more than 50 ms the log says so (`service 'modules' tick took ...`).
- [ ] Memory is taken once, at the start, with fixed sizes. Where the size depends on the
      board, it follows `LMP_MODULE_MEM`.
- [ ] `onInit` begins from nothing, and `onStop` gives back what the parts do not give back by
      themselves: the module survives `sys.modules restart`.
- [ ] The state is native. The document is written with `JsonWriter`, read with `JsonValue`.
- [ ] Commands, not routes. Every argument has its limits.
- [ ] Secrets are declared with `.secret()`.
- [ ] Every pin is a parameter of the build, and every pin is claimed.

**Behaviour**

- [ ] Without network, without Internet and without the time the module still starts, and its
      document says what is missing.
- [ ] A component that is not there, or pins that are missing on the board, give one clear line
      in the log.
- [ ] `markChanged()` only when something changed: every call is an event to the console and
      to the broker.
- [ ] The file of the module stays under 2 KB at tier MIN and 4 KB elsewhere.
- [ ] The module tells its things, and their ids are final.
- [ ] Every command that only reads says `kCmdReadOnly`; one that acts outside the device says
      `kCmdSensitive`.
- [ ] The page of the module uses only `ctx`, and has its texts in English and in Italian.

**The OS**

- [ ] LoopMaxPro was not changed for the module. A value of the OS the module needs on a board
      is in its `tune`: see
      [the tune](modules-sdk.md#the-tune-values-of-the-os-for-one-module-on-one-board).

**Tried**

- [ ] Tests on the PC for what the module decides.
- [ ] The page in the simulator, on a phone-sized window too.
- [ ] A real board of every family in `families`: first start as an access point, on the
      network, after a restart, after `sys.modules restart`, after a factory reset.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="modules-sdk.md">← Modules and SDK</a> &nbsp;·&nbsp;
<a href="web-console.md">Web console →</a>
</p>
