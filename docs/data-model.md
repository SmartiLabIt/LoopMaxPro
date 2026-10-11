<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/data-model.svg" alt="Data model and report" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="kernel-and-services.md">← Kernel and services</a> &nbsp;·&nbsp;
<a href="modules-sdk.md">Modules and SDK →</a>
</p>

---

This page describes how LoopMaxPro holds its data and how that data leaves the device: the
native structs and their field tables, how a document becomes JSON, the system report and its
parts, what the data of a module looks like, and when the device sends something by itself.
It is for who writes a module, a service, or a program that reads a LoopMaxPro device.

- [Native data inside, JSON at the borders](#native-data-inside-json-at-the-borders)
- [Field tables](#field-tables)
- [From native data to JSON](#from-native-data-to-json)
- [The documents of a device](#the-documents-of-a-device)
- [The system report](#the-system-report)
- [The data of a module](#the-data-of-a-module)
- [System settings](#system-settings)
- [Events](#events)
- [When and how data is sent](#when-and-how-data-is-sent)

## Native data inside, JSON at the borders

Inside the firmware there is no JSON. The state of a service or of a module is a C++ struct;
services and modules call each other with C++ types; an event is four small numbers. JSON is
written only when a document leaves the device, and read only when a request enters it.

```
 in                                   engine                                   out
 JSON ── read and checked ──▶  structs, arrays, enums, events  ── written ──▶  JSON
 HTTP, MQTT, cloud, file            direct C++ calls            HTTP, events, MQTT, cloud, file
```

| Border | Format |
|--------|--------|
| HTTP API and events for the web console | JSON |
| MQTT | JSON in the payload |
| Cloud bridge | JSON, in an envelope |
| Settings of a module on the filesystem | JSON, one file for each module |
| Serial command line | text in (`name=value`), JSON out |
| System settings in storage | no JSON: one key/value entry for each field |

What this buys on a microcontroller: reading a value is a field access and not a search by
name; memory has a fixed size and does not fragment; a value is checked once, when it enters;
a wrong field name stops the build instead of failing in the field.

## Field tables

The link between a struct and its JSON form is written once, with a **field table**
([src/kernel/fields/fields.h](../src/kernel/fields/fields.h)):

```cpp
struct RelayCfg {
  lmp::FixedString<32> deviceName;
  uint16_t             saveDelayMs;
  bool                 restoreOnBoot;
};

LMP_FIELDS(RelayCfg,
  LMP_F(deviceName,    "deviceName",    Str(1, 32, "Sockets")),
  LMP_F(saveDelayMs,   "saveDelayMs",   U16(0, 60000, 5000)),
  LMP_F(restoreOnBoot, "restoreOnBoot", Bool(true)))
```

Each `LMP_F` line names the member, its JSON key, and a spec with limits and default. From this
one declaration the system gets:

| Function | What it does |
|----------|--------------|
| `setDefaults(object)` | sets every field to its default |
| `writeFields(writer, object)` | writes the members into the JSON object that is open |
| `readFields(json, object)` | checks and copies the members of a JSON object |
| `writeSchema<T>(writer)` | writes the schema, from which the console builds a form |

The kinds of field, each with the C++ type it must have:

| Spec | C++ member | Name in the schema |
|------|------------|--------------------|
| `Bool(def)` | `bool` | `bool` |
| `U8`, `U16`, `U32` `(min, max, def)` | `uint8_t`, `uint16_t`, `uint32_t` | `u8`, `u16`, `u32` |
| `I8`, `I16`, `I32`, `I64` `(min, max, def)` | `int8_t` ... `int64_t` | `i8`, `i16`, `i32`, `i64` |
| `F32`, `F64` `(min, max, def)` | `float`, `double` | `f32`, `f64` |
| `Str(minLen, maxLen, def)` | `lmp::FixedString<N>` | `str` |

A spec can carry a flag:

| Flag | Effect |
|------|--------|
| `.secret()` | accepted in input, never written in output |
| `.readOnly()` | written in output, ignored in input |
| `.locked()` | shown with its default, never changed |

The rules of reading:

- **All or nothing.** Every value is checked first; when one is not valid nothing is changed,
  and the caller gets the key of the field that failed. On the HTTP API that key comes back as
  `field`.
- Members that are missing keep their value. Members that the table does not know are ignored.
- A number must be inside its limits; a text must respect its lengths and fit in the member.
  A boolean also accepts `0` and `1`.

The table is checked by the compiler: a spec that does not match the type of the member, or a
member of a type the tables do not support, stops the build. `LMP_FIELDS` is used at global
scope, and the struct must be standard layout.

The schema of the example above:

```json
[ { "key": "deviceName",    "type": "str",  "min": 1, "max": 32,    "def": "Sockets" },
  { "key": "saveDelayMs",   "type": "u16",  "min": 0, "max": 60000, "def": 5000 },
  { "key": "restoreOnBoot", "type": "bool", "def": true } ]
```

**Secrets.** A secret field never appears in a document. When a document is written for a
reader, `"<key>Set": true` or `false` takes its place and says only whether a value exists. The
real value is written in one place only: the storage of the device.

## From native data to JSON

Two small classes do all the work, without a document tree and without the heap.

**`JsonWriter`** ([src/kernel/json/json_writer.h](../src/kernel/json/json_writer.h)) writes
straight into a sink, usually a buffer of fixed size. Texts are always escaped. This is how the
storage service writes the `fs` member of its state (shortened):

```cpp
void StorageService::describe(JsonWriter& w) const {
  w.beginObject("fs");
  w.boolean("mounted", fs_ && fs_->mounted());
  w.u64("total", space.totalBytes);
  w.u64("used", space.usedBytes);
  w.endObject();
}
```

When the text does not fit in the sink the writer says so (`ok()` and `complete()` become
false) and the caller answers with an error instead of a broken document. That is how the size
of an answer stays bounded: on the HTTP API an answer longer than `LMP_HTTP_RESP_MAX` becomes
the error `too_large`, and the caller asks for less, for example the report one part at a time.

**`JsonValue`** ([src/kernel/json/json_reader.h](../src/kernel/json/json_reader.h)) reads. It is
a view over the text that arrived: `parse()` validates the whole text, `get("key")` and
`at(index)` look a member up, and the typed getters copy the value out. Nothing is allocated.

A service or a module rarely uses the two classes directly. A service writes the members of its
state in `describe(JsonWriter&)`; a module declares field tables and lets the SDK do the rest.

## The documents of a device

Everything that a device tells has one of a few fixed shapes. Every service and every module is
a **target**, named `sys.<service>` or `mod.<module>`.

| Document | Content | HTTP |
|----------|---------|------|
| State | name, host, board, edition, firmware version, mode, whether a login is needed, the modules. Given without a login | `GET /api/state` |
| Report | all the data of the system and of the services, with a fixed schema | `GET /api/report` |
| Document of a target | the state of one service or module | `GET /api/doc/<target>` |
| Settings of a target | the settings of one service or module, without secrets | `GET /api/cfg/<target>` |
| Manifest | the list of targets; for a target, its commands with arguments and limits and, for a module with settings, their schema | `GET /api/manifest`, `GET /api/manifest/<target>` |
| Log | entries of the log, in pages, from a sequence number on | `GET /api/log` |

The other interfaces give the same documents: the serial command line has `report`,
`doc <target>`, `man <target>` and `log`; MQTT gives the report and the document of a target
on request. Requests, parameters and errors are in [HTTP API](http-api.md),
[Serial console](serial-console.md) and
[Discovery, MQTT and Home Assistant](network-and-home-assistant.md).

A page of the log looks like this:

```json
{ "seqLast": 412, "bootDoneMs": 640, "dropped": 0,
  "items": [ { "seq": 411, "ms": 86400123, "lvl": "warn", "src": "net", "msg": "..." },
             { "seq": 412, "ms": 86400200, "lvl": "info", "src": "cmd",
               "msg": "mod.zappy2 set from local: ok" } ] }
```

## The system report

The report is the document that is always the same, whatever the device does. It has the same
schema on every board and with every module, so a program can read any LoopMaxPro device
without knowing its modules. Each part is written by the service that owns the data, with the
same code that writes the document of that service.

An example, shortened, with values made up for the page:

```json
{
  "v": 2,
  "device": {
    "key": "<device key>", "code": "", "name": "Garden", "host": "loopmax-eeff",
    "board": "esp32dev", "fw": "1.0.12", "os": "1.0.12", "build": "2026-06-01",
    "lang": "en", "mode": "LAN", "layout": "9f1c2a7b04d3e685", "base": "idf4"
  },
  "system": {
    "chip": "ESP32-D0WD-V3", "edition": "full", "cores": 2, "revision": 3, "cpuMhz": 240,
    "flash": 4194304, "flashHz": 80000000, "sdk": "v4.4.7",
    "heap": { "free": 142344, "min": 120120, "total": 281656, "largest": 110592 },
    "stackMin": 3512, "uptime": 86400, "resetReason": "software",
    "starts": 14,
    "lastStarts": [ { "why": "software", "at": 1790000000 },
                    { "why": "poweron", "at": 1789996400, "ran": 3590 } ],
    "mode": "LAN", "debug": false
  },
  "storage": { "fs": { "mounted": true, "total": 917504, "used": 210000 }, "kv": { "ok": true } },
  "clock": {
    "unix": 1790086400, "synced": true, "source": "ntp", "syncedAt": 1790000060,
    "tz": "UTC", "posix": "UTC0", "ntpServer": "pool.ntp.org",
    "ntp": { "available": true, "running": true, "answers": 24 },
    "local": "2026-09-22 14:13:20", "offset": 0, "weekday": 2
  },
  "net": {
    "mode": "LAN", "state": "connected",
    "sta": { "connected": true, "ssid": "Home", "ip": "10.0.0.25", "gateway": "10.0.0.1",
             "mask": "255.255.255.0", "dns": "10.0.0.1", "rssi": -58, "channel": 6,
             "mac": "AA:BB:CC:DD:EE:FF", "failures": 0 },
    "ap": { "up": false, "ssid": "LoopMax-EEFF", "ip": "192.168.4.1", "clients": 0,
            "captive": false, "pswDefault": false },
    "internet": true, "internetChecked": true, "mdns": true, "host": "loopmax-eeff.local",
    "join": { "phase": "none", "last": "none" },
    "scan": { "state": "idle", "networks": [] },
    "near": { "state": "idle", "devices": [] }
  },
  "geo": {
    "known": true, "source": "lookup", "lat": 51.5074, "lon": -0.1278,
    "city": "London", "region": "England", "country": "GB", "asked": false,
    "publicIp": "203.0.113.7", "isp": "Example ISP",
    "lookup": { "configured": true, "state": "done" }
  },
  "modules": {
    "count": 1, "max": 8,
    "list": [ { "name": "zappy2", "title": "Zappy 2", "icon": "<emoji>", "kind": "hardware",
                "version": "1.0.1", "code": "", "state": "Ready", "reason": "",
                "tickMs": 50, "settings": false } ]
  },
  "services": [
    { "name": "net", "icon": "<emoji>", "state": "Ready", "reason": "", "deps": ["config", "log"],
      "tickMs": 100, "starts": 1, "failures": 0, "tickUs": 180, "tickMaxUs": 4200, "heap": 36764 }
  ]
}
```

The parts:

| Part | Written by | Content |
|------|-----------|---------|
| `device` | the kernel | identity: key, device code, name, host name, board, firmware version and build date, language, mode. On the ESP32 family also `layout`, a fingerprint of the partition table, and `base`, the major version of ESP-IDF underneath: together they tell whether a firmware image fits the board. [A service of the product](kernel-and-services.md#a-service-of-the-product) can add members of its own |
| `system` | `sys.system` | chip, edition (`full` or `light`), flash, heap, external RAM as `psram` where the board has it, lowest free stack, uptime in seconds, reason of the last restart, how many times the board started and its last starts |
| `storage` | `sys.storage` | filesystem mounted, total and used bytes; health of the key/value store |
| `clock` | `sys.clock` | Unix time, whether and from where it was set (`ntp`, `client`, `cloud`, `none`), time zone, state of the time server; local time, offset and weekday once the clock is set |
| `net` | `sys.net` | mode and state of the network; the client (`sta`) and the access point (`ap`), separately; Internet reachable; mDNS and host name; the change of network in progress (`join`); the last scan; the other LoopMaxPro devices found on the network (`near`) |
| `geo` | `sys.geo` | position and place of the device and where they come from, public address and provider |
| `cloud` | `sys.cloud` | whether a cloud bridge is attached and connected, counters, what is waiting to be sent |
| `modules` | `sys.modules` | how many modules, and for each one name, title, icon, kind, version, state and reason |
| `services` | the registry | one entry for each service: state, reason, dependencies, tick period, starts, failures, last and longest tick in microseconds, heap taken at start |

Good to know:

- **Parts can be asked one by one.** `GET /api/report?parts=system,net` gives only those. A board
  with a small answer buffer cannot give the whole report in one answer: ask it in two or more.
- **A part exists only when its service is in the build.** A firmware of the smallest tier has
  no `geo` and no `cloud`.
- **The key** is written only for an authenticated request. It is never in the public state.
- **`starts` and `lastStarts`** survive a loss of power. `lastStarts` holds the last 6 starts,
  the newest first: `why` uses the words of `resetReason` (`poweron`, `external`, `software`,
  `panic`, `watchdog`, `brownout`, `deepsleep`, `unknown`); `at` is the Unix time of the start,
  present once the clock had the time; `ran` is how many seconds that start lasted, present
  when the next start could tell, so it is missing after a power cut.
- **States of a service** are `Stopped`, `Waiting`, `Starting`, `Ready`, `Degraded` and
  `Error`; `reason` says why when the state is not `Ready`.
- **`icon`** is an emoji, the same one the console shows.

## The data of a module

A module keeps its state in structs and declares one field table for each. Take a module that
reads a temperature:

```cpp
struct ThermoState { float celsius; uint32_t readAt; bool ok; };
LMP_FIELDS(ThermoState,
  LMP_F(celsius, "celsius", F32()),
  LMP_F(readAt,  "readAt",  U32()),
  LMP_F(ok,      "ok",      Bool()))

struct ThermoCfg { lmp::FixedString<32> place; uint16_t everyMin; };
LMP_FIELDS(ThermoCfg,
  LMP_F(place,    "place",    Str(0, 32, "")),
  LMP_F(everyMin, "everyMin", U16(1, 1440, 30)))

lmp::sdk::State<ThermoState>      state_;   // what the module is doing now
lmp::sdk::ModuleConfig<ThermoCfg> cfg_;     // what the user chose
```

In `onInit()` the module takes the two in with `use(state_)` and `useSettings(cfg_)`. After
that its code works with plain members:

```cpp
state_->celsius = 21.5f;
state_.touch();                 // tells the system that the state changed
if (cfg_->everyMin > 10) { ... }
```

From those two tables the system builds everything else.

**The state document**, `GET /api/doc/mod.thermo`. The members of the state are at the top, the
settings under `config`, with secrets replaced by `<key>Set`:

```json
{ "celsius": 21.5, "readAt": 1790086400, "ok": true,
  "config": { "place": "Garden", "everyMin": 30 } }
```

**The settings**, `GET /api/cfg/mod.thermo`: `{ "place": "Garden", "everyMin": 30 }`.

**The schema**, in the manifest of the target under `config`: the console draws the form of the
settings from it, with no code written for it.

```json
"config": [ { "key": "place",    "type": "str", "min": 0, "max": 32,   "def": "" },
            { "key": "everyMin", "type": "u16", "min": 1, "max": 1440, "def": 30 } ]
```

**Two commands**, `config.set` and `config.reset`, added to the module by its settings.
`config.set` takes one or more of the members and applies them all or none.

**The file**, `/cfg/m/thermo.json`. Only what must survive a restart is written: the settings,
not the state.

```json
{ "v": 1, "config": { "place": "Garden", "everyMin": 30 } }
```

What to know about the file:

- It is written about two seconds after a change (`LMP_CONFIG_SAVE_DELAY_MS`), so a burst of
  changes is one write. Before a restart the pending files are written at once.
- It holds the secrets too: it is where they live.
- It cannot be longer than `LMP_MODULE_CFG_MAX` bytes.
- A file that cannot be read, or a value that is no longer valid after a firmware change,
  leaves the module with its defaults.
- A factory reset removes it.

Ready-made parts of the SDK, such as a bank of pins or its schedules, work the same way: each
one adds its members to the document, its commands to the target and what it keeps to the file.
A module can also write members by hand, with the writer, in its own `describe()`. It never has
to build JSON text, a route or a topic to make its data and its commands reachable. See
[Modules and SDK](modules-sdk.md) and [Writing a module](writing-a-module.md).

## System settings

The settings of the system are native structs too, in sections: `sys`, `net`, `web`, `clock`,
`cloud`, `mqtt`, `geo` ([src/kernel/config/config.h](../src/kernel/config/config.h)). Each
section has a field table and belongs to a service: `sys` to `sys.config`, the others to the
service of the same name. That service shows the section with `GET /api/cfg/<target>` and
changes it with its commands.

In storage there is no JSON: each field is one entry of the key/value store, under the
namespace of its section. Only the values that changed are written, because flash wears out, and
the write happens two seconds after the last change. Passwords are marked secret, so no document
ever carries them.

A factory reset takes every section back to its defaults. The key of the device is kept.

## Events

An event is a small native value: an id and three numbers, `a`, `b` and `value`
([src/kernel/event/event.h](../src/kernel/event/event.h)). Any task can publish one; the
handlers always run on the application task. An event becomes JSON only when it leaves:

```json
{ "id": "svc", "a": 8, "b": 3, "value": 0, "service": "net", "state": "Ready" }
```

The main ones:

| `id` | Meaning | Numbers |
|------|---------|---------|
| `svc` | a service changed state | `service` and `state` are written in clear |
| `config` | settings changed | `a`: the section |
| `mode` | the operating mode changed | `a`: the new mode |
| `net.ap.up`, `net.ap.down`, `net.sta.up`, `net.sta.lost`, `net.fallback` | access point and client of the network | |
| `net.join` | a change of network ended | `b`: 1 when the new network was accepted |
| `internet.up`, `internet.down` | Internet reachable or not | |
| `time` | the clock was set | `value`: Unix time |
| `module.state` | a module changed state | `a`: index of the module, `b`: the state |
| `module.data` | the document of a module changed | `a`: index of the module |
| `cmd` | a command that answered `pending` ended | `value`: its reference, `b`: the outcome |
| `ota.progress`, `ota.done` | a firmware update | `b`: percent, or the outcome |
| `reset` | a restart was asked | `a`: 1 for a factory reset |
| `mqtt.up`, `mqtt.lost`, `cloud.up`, `cloud.lost` | connections | |

An event says **what** changed, not the new data. Who listens reads the matching document again.
The index of a module is its place in the `modules` list of the state.

## When and how data is sent

A device answers when asked, on every interface. It also sends by itself when something
changes, so nobody has to ask again and again.

**A change in a module.** The module calls `touch()` on its state, or `markChanged()`. Changes
closer than `LMP_MODULE_DATA_DELAY_MS` (200 ms) become one `module.data` event. From there each
interface does its part.

**The web console.** It reads the state and one report when it opens, then follows
`GET /api/events`, a stream of server-sent events with three kinds of message:

| Message | Content |
|---------|---------|
| `event` | one system event, as in the table above, as soon as it is delivered |
| `log` | one new log entry |
| `ping` | every 15 seconds, to keep the connection alive |

For each event the console reads again only the document that changed.

**MQTT.** When a broker is configured, under the base topic of the device:

| Topic | Direction | Content |
|-------|-----------|---------|
| `<base>/status` | out, retained | `online`, and `offline` as last will |
| `<base>/evt` | out | every system event, when the `events` setting is on |
| `<base>/get/<target>` | in | asks for a document; the target `report` asks for the report |
| `<base>/doc/<target>` | out | the document that was asked |
| `<base>/cmd/<target>/<command>` | in | a command, with its arguments as JSON |
| `<base>/ack/<target>/<command>` | out | the answer of the command |

The values that the modules show to Home Assistant are published under `<base>/home/` when they
change, and kept by the broker.

**Cloud.** When a cloud bridge is attached, the `cloud` service decides what to send and when;
the bridge only carries it:

| What | When |
|------|------|
| Whole report | at every connection, and on request |
| Partial report, with the parts that changed | at every change of a service, of the network, of the clock or of the settings, grouped within 2 seconds |
| Periodic report, with `system`, `clock` and `net` | every `heartbeatMin` minutes: 15 by default, 0 turns it off |
| Document of a module | at the connection and at every change, grouped within half a second |
| Events | as they happen, the ones waiting together in one message |
| Log entries | on request; pushed by themselves only when `pushLogLevel` is set, and it is off by default |
| Outcome of a command | at once; for a command that ends later, again at its end |

Without a bridge the service does nothing and the device works on its own. See
[Cloud bridge](cloud.md).

**Serial port.** Nothing is sent by itself except the log, line by line. Documents are printed
on request.

**The answer of a command** has the same shape everywhere:

```json
{ "result": { }, "ok": true, "status": "ok" }
```

`status` is one of `ok`, `pending`, `busy`, `invalid`, `not_found`, `no_mem`, `io`, `timeout`,
`unsupported`, `denied`, `error`. When a value was refused, `field` names it. A command that
ends later answers `pending` with a `ref`; the `cmd` event that closes it carries the same
number in `value`.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="kernel-and-services.md">← Kernel and services</a> &nbsp;·&nbsp;
<a href="modules-sdk.md">Modules and SDK →</a>
</p>
