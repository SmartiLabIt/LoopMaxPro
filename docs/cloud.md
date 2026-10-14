<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/cloud.svg" alt="Cloud bridge" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="lora.md">← LoRa</a> &nbsp;·&nbsp;
<a href="updates.md">Versions and updates →</a>
</p>

---

LoopMaxPro has a cloud service, `sys.cloud`, that does not talk to any server by itself. It
decides what the device tells a server and when, puts it in one envelope, and takes the requests
of the server to the command bus. A **bridge**, written as a module, carries those envelopes to
a server of your choice. This page describes the interface between the two, for who wants to
write a bridge.

**No bridge is part of this repository, and a device needs none.** The console, the HTTP API,
MQTT and the modules work in full on the local network. Without a bridge the cloud service is
ready and does nothing.

- [The two parts](#the-two-parts)
- [The bridge interface](#the-bridge-interface)
- [The envelope](#the-envelope)
- [What the device sends](#what-the-device-sends)
- [What the device accepts](#what-the-device-accepts)
- [Document, commands and console](#document-commands-and-console)
- [The things of the modules, for a server](#the-things-of-the-modules-for-a-server)
- [Build options](#build-options)

## The two parts

| Part | Where | What it does |
|------|-------|--------------|
| Cloud service | [src/services/cloud/](../src/services/cloud/) | Chooses what is sent and when, writes the envelope, runs the requests through the command bus |
| Bridge | your module | The transport, the address of the server, how the device proves who it is |

The bridge carries any envelope without looking inside. A new module, or a new command, travels
through it with no change to the bridge: the documents and the commands are the same ones the
console and the [HTTP API](http-api.md) use.

| | |
|---|---|
| In the build | `LMP_FEATURE_CLOUD`: on by default with the network at tier 2 and above, so on every ESP32 board. Off on the ESP8266 boards (tier 1) |
| When it runs | In mode LAN, with the device on a network. In mode AP the service waits and the bridge is not polled |
| Without a bridge | Nothing is sent and no memory is taken for the envelopes |
| With a bridge | At `attach` the service takes one buffer of `LMP_CLOUD_OUT_MAX` bytes for what leaves and `LMP_CLOUD_INBOX` buffers of `LMP_CLOUD_IN_MAX` bytes for what arrives |

## The bridge interface

The interface is in [src/services/cloud/cloud_service.h](../src/services/cloud/cloud_service.h):

```cpp
using CloudReceiveFn = Delegate<void(StringView envelope)>;

class ICloudBridge {
 public:
  virtual const char* name() const = 0;
  virtual void poll(uint32_t nowMs) = 0;
  virtual bool connected() const = 0;
  virtual Status send(StringView envelope) = 0;
  virtual void onReceive(CloudReceiveFn fn) = 0;
  virtual void describe(JsonWriter& w) const {}
  virtual Status checkUpdate() { return Status::Unsupported; }
  virtual Status installUpdate() { return Status::Unsupported; }
};

class ICloudService {
 public:
  virtual Status attach(ICloudBridge& bridge) = 0;
  virtual void detach(ICloudBridge& bridge) = 0;
  virtual bool connected() const = 0;
};
```

| Method | What the bridge does |
|--------|----------------------|
| `name()` | A short name without secrets: it is shown in the document of the service and in the log |
| `poll(nowMs)` | Called on the application task at every round of the service (50 ms). The bridge moves its own work on. It must not wait for the network: long work goes in pieces, or on a task of the bridge |
| `connected()` | True while the server can be reached. Read at every round |
| `send(envelope)` | Takes one complete envelope, JSON text. The text is in the buffer of the service and the next envelope overwrites it: copy it if it does not leave at once. Called on the application task |
| `onReceive(fn)` | Keeps `fn` and calls it with the text of every envelope that arrives from the server. Called once at `attach`, and with an empty delegate at `detach` |
| `describe(w)` | Optional. Writes members for the document of the service, such as the server in use and the last problem. Never secrets |
| `checkUpdate()`, `installUpdate()` | Optional, for a bridge whose server can hand out a new firmware: the commands `checkUpdate` and `update` of the service call them. By default both answer `Unsupported` |

What `send` returns decides what happens to the envelope:

| Returned | The service |
|----------|-------------|
| `Status::Ok` | Counts it as sent |
| `Status::Busy` | Not now. Reports, documents of the modules, log entries, events and the outcome of a command that ended later stay due and are offered again at the next round. The direct answer to a request is not offered again |
| Anything else | Counts it as not sent and does not offer it again |

A small queue inside the bridge is the simple way to never lose an answer.

What changes when `connected()` changes:

| From | To | The service |
|------|----|-------------|
| false | true | Sends everything again: the whole report and the document of every module. Publishes the event `cloud.up` |
| true | false | Stops sending. Publishes the event `cloud.lost` |

`fn` can be called from any task: the text is copied at once and handled on the application
task, one request for each round. A text of `LMP_CLOUD_IN_MAX` bytes or more is refused.

### Attaching a bridge

A module reaches the service through the kernel it gets in its context, and attaches an object
that lives as long as the module. One bridge at a time: a second one gets `Status::Busy`.
`attach` answers `Status::NoMem` when there is no memory for the buffers.

```cpp
#include "sdk/module.h"
#include "services/cloud/cloud_service.h"

class MyBridge : public lmp::ICloudBridge {
 public:
  const char* name() const override { return "mybridge"; }
  void poll(uint32_t nowMs) override { /* move the connection on, never wait */ }
  bool connected() const override { return up_; }
  lmp::Status send(lmp::StringView envelope) override {
    // envelope.data, envelope.size: hand them to the transport, or copy them in a queue.
    return lmp::Status::Ok;
  }
  void onReceive(lmp::CloudReceiveFn fn) override { deliver_ = fn; }
  void describe(lmp::JsonWriter& w) const override { w.str("server", "example.org"); }

 private:
  // For the transport: a text arrived from the server.
  void arrived(const char* text, size_t length) {
    if (deliver_) deliver_(lmp::StringView(text, length));
  }
  lmp::CloudReceiveFn deliver_;
  bool up_ = false;
};

// In the module:
lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
  cloud_ = c.kernel->api<lmp::ICloudService>();   // null when the build has no cloud service
  return cloud_ ? cloud_->attach(bridge_) : lmp::Status::Unsupported;
}
void onStop() override {
  if (cloud_) cloud_->detach(bridge_);
}
```

Three things to know:

- Keep the bridge a separate object and not a second base class of the module: `Module` and
  `ICloudBridge` both have a `describe(JsonWriter&) const`, for two different documents.
- Put the code between `#if LMP_FEATURE_CLOUD` and `#endif`. The sources of every module are
  compiled in every firmware, and the header is empty where the service is not in the build.
- The module kind `bridge` of the SDK means something else: a module that talks to hardware
  outside the board or to other devices.

How a module is written and added to a firmware is in [Writing a module](writing-a-module.md).
The tests in [test/test_cloud/](../test/test_cloud/test_cloud.cpp) drive the service with a fake
bridge and show the behaviour described here.

## The envelope

One JSON object, in both directions:

```json
{ "v": 2, "type": "ack", "key": "<device key>", "target": "sys.cloud", "name": "set",
  "ref": "a81f", "ts": 1790000001,
  "body": { "result": {}, "ok": true, "status": "ok" } }
```

| Field | From the device | From the server |
|-------|-----------------|-----------------|
| `v` | Version of the envelope: 2 | Not read |
| `type` | Kind of envelope, see the tables below | Required |
| `key` | The key of the device, made at its first start | Optional. When present it must be the key of this device, or the envelope is refused: one transport may carry the envelopes of several devices |
| `code` | Kind of device: the build value `LMP_DEVICE_CODE`. Left out when empty, which is the default | Not read |
| `target` | `sys.<service>` or `mod.<module>`, when the envelope is about one | The same, where the request needs it |
| `name` | Name of the command, in the answer to one | Name of the command to run |
| `ref` | The `ref` of the request this envelope answers | Free text, at most 23 characters. The device repeats it in its answer |
| `ts` | Unix time of the device, 0 until it knows the time | Not read |
| `body` | The content, see below | Arguments of the request |

Every envelope carries the device key, which the device otherwise shows only to who has access
to its console. Use a transport that keeps it private.

## What the device sends

| `type` | `body` | Same content as |
|--------|--------|-----------------|
| `report` | The device report, whole or some of its parts | `GET /api/report` |
| `doc` | The document of `target` | `GET /api/doc/<target>` |
| `log` | `{ "seqLast": n, "items": [ { "seq", "ms", "lvl", "src", "msg" } ] }` | |
| `event` | `{ "items": [ { "id", "a", "b", "value" } ] }`: events as the console gets them | `GET /api/events` |
| `manifest` | The commands of `target`, or `{ "targets": [...] }` | `GET /api/manifest/<target>` |
| `cfg` | The settings of `target`, without secrets | `GET /api/cfg/<target>` |
| `ack` | The answer to a command | `POST /api/cmd/<target>/<name>` |
| `error` | `{ "error": "unknown_type" }` | |

The parts of the report are `device`, `system`, `clock`, `net`, `geo`, `storage`, `modules`,
`cloud` and `services`. The documents themselves are described in
[Data model and report](data-model.md) and in [HTTP API](http-api.md).

When each one leaves, while the bridge says it is connected:

| What | When | Grouping |
|------|------|----------|
| Whole report | At every connection, and on the command `report` of the service | |
| Document of every module | At every connection | One envelope for each module |
| Partial report | A service changes state (`services`); mode, access point, network or Internet change (`net`); the time is set (`clock`); a module changes state (`modules`); the settings of device, network, clock or position change (`device`, `net`, `clock`, `geo`) | Changes within `LMP_CLOUD_PARTIAL_MS` (2 s) become one envelope |
| Document of a module | The module says its data changed | Changes within `LMP_CLOUD_DATA_MS` (500 ms) become one round of envelopes, one for each module |
| Periodic report: `system`, `clock`, `net` | Every `heartbeatMin` minutes. Default 15; 0 turns it off | |
| Log entries | Only when `pushLogLevel` is not 0: 1 sends the errors, 2 the warnings too. The entries of the cloud service itself never leave, so a sending problem does not cause more sending | Up to `LMP_CLOUD_LOG_QUEUE` (4) entries for each round |
| Events | At the next round. The ones from before a connection are dropped: the whole report follows anyway | Up to `LMP_CLOUD_EVENT_QUEUE` (8) in one envelope. An event repeated, like the percent of an update, takes the place of the one waiting |
| Answers | At once, in the round that handles the request | |

The events that leave are the ones the console follows to read again what changed: services,
settings, mode, network, time, state of the modules, firmware update, MQTT, LoRa. `module.data`
does not leave (the `doc` does), nor `cmd` (the `ack` does), nor the events of the cloud itself.
The list is `forServer()` in
[cloud_service.cpp](../src/services/cloud/cloud_service.cpp).

### Envelopes that do not fit

The room for one envelope is `LMP_CLOUD_OUT_MAX` bytes: the largest answer of the HTTP API plus
256, so 8448 bytes at tier 2 and 16640 at tier 3. A report the device sends by itself that does
not fit leaves one part at a time, one envelope for each part. When a single part does not fit,
or any other envelope, such as a document or a report the server asked for, it leaves without
its content:

```json
{ "v": 2, "type": "doc", "key": "<device key>", "target": "mod.zappy2", "ref": "a81f",
  "body": { "error": "too_large" } }
```

## What the device accepts

| `type` | Fields | Answer |
|--------|--------|--------|
| `cmd` | `target`, `name`; `body`: the arguments, as a JSON object | `ack`, with `target`, `name` and `ref` |
| `doc` | `target` | `doc`; `{ "error": "not_found" }` for a target that does not exist |
| `report` | `body`: `"system,net"`, or `{ "parts": "system,net" }`, or nothing for the whole report | `report` |
| `log` | `body`: `{ "since": n, "max": n }`. `max` from 1 to 50, default 20 | `log` |
| `manifest` | `target`; without it, the list of the targets | `manifest`; `{ "error": "not_found" }` |
| `cfg` | `target` | `cfg`; `{ "error": "not_found" }` |
| `time` | `body`: `{ "unix": n }` | None. Taken only while the device does not know the time |
| anything else | | `error`, `{ "error": "unknown_type" }` |

In a firmware with [a service of the product](kernel-and-services.md#a-service-of-the-product),
a `type` that is not in the table is offered to that service first. When the service takes it
there is no answer: the next report tells what changed.

Commands:

- A command from the server goes through the command bus like any other, with the source
  `remote`: the same checks of the arguments, the same line in the log.
- The `body` of the `ack` is the answer of the HTTP API: `result`, `ok`, `status`, and `field`
  when an argument is wrong. A `cmd` without `target` or `name` gets an `ack` with
  `{ "error": "invalid" }`.
- A command that ends later answers with the status `pending`. When the request had a `ref`, the
  service follows the command and sends a second `ack` with the same `ref` when it ends:
  `{ "ok": true, "status": "ok" }`, or the status of the failure. It follows up to
  `LMP_CLOUD_WAITING` (4) commands at a time.

Limits:

- One request is handled for each round of the service.
- At most `LMP_CLOUD_INBOX` (2) requests wait. The ones that find no room are dropped and
  counted in `dropped`.
- Counted in `refused`, and not answered: a text too long, a text that is not a JSON object, a
  `key` of another device. Counted in `refused` and answered: a `cmd` without `target` or `name`,
  an unknown `type`.

## Document, commands and console

The document `sys.cloud`, also in the whole report as the part `cloud`:

```json
{ "attached": true, "bridge": "mybridge", "connected": true, "upSec": 42, "connections": 1,
  "sent": 12, "received": 3, "refused": 0, "dropped": 0, "failed": 0, "eventsLost": 0,
  "heartbeatMin": 15, "pushLogLevel": 0,
  "pending": { "report": false, "parts": 0, "modules": 0, "logs": 0, "events": 0 },
  "detail": { "server": "example.org" } }
```

`detail` holds what the bridge wrote in `describe`, and is there only with a bridge. `upSec` is
there only while connected. `pending` says what is still due.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `sys.cloud set` | `heartbeatMin` 0 to 1440, `pushLogLevel` 0 to 2; at least one | Saved with the settings |
| `sys.cloud report` | | The whole report now. `unsupported` without a bridge, `denied` while not connected |
| `sys.cloud checkUpdate` | | Calls `checkUpdate()` of the bridge. `unsupported` without a bridge |
| `sys.cloud update` | | Calls `installUpdate()` of the bridge. `unsupported` without a bridge |

In the [web console](web-console.md) the service has the tab **Cloud** of the LoopMaxPro space:
the state of the connection, the counters and the two settings. From `detail` it shows `server`
and `error`. Without a bridge the tab says that the firmware has no cloud connection and that the
device works on its own. With a bridge a cloud icon in the header shows whether the server is
connected.

## The things of the modules, for a server

A server that carries the envelopes knows nothing of the modules, and it does not need to.
Every module tells its **things**
([Modules and SDK](modules-sdk.md#things-what-a-module-tells-home-automation)): what who uses
the device reads and commands. They travel as data in what the device already sends: no
envelope is of the things alone, and a bridge has nothing to do for them.

<p align="center">
<img src="assets/img/things.svg" alt="LoopMaxPro things: a module tells its things once (switches, lights, numbers, yes or no, texts, choices, buttons); the system describes them, follows their values, checks what is set and keeps the measures with their time; Home Assistant, a server of yours and the console use them without knowing the module" width="900">
</p>

**In the document of a module**, which the server gets at every connection and at every change:

```json
"things": { "gen": 40213, "rev": 88,
            "values": { "level": 80, "pump": false, "mode": "eco",
                        "lamp": { "on": true, "brightness": 80, "r": 255, "g": 0, "b": 0 } },
            "at": { "level": 1790003600 },
            "parts": 2, "kept": 3 }
```

| Member | |
|--------|---|
| `gen` | Changes when the **description** changes: a thing or a part added, taken away, renamed, a limit or a choice that changes. The same after a restart |
| `rev` | Goes up when a **value** changes, and when a number is measured again |
| `values` | The things of the module itself, by id. Yes or no: `true`, `false`. A number: as it is shown, `null` when it is **not known**. A text or a choice: a string. A light: an object. A button has no value |
| `at` | When each number that says it was measured, as Unix seconds. Measured again with the same value, the time is another: a value that stands still is not a sensor that is silent |
| `parts` | How many parts the module has, as the nodes of a hub. Their values are read with `things.values` |
| `kept` | How many measures, made while the server was away, wait to be asked for with `things.kept` |

**Four commands**, the same on every module:

| Command | Arguments | Answer |
|---------|-----------|--------|
| `things` | `from` | What the things are: `id`, `kind` (`toggle`, `light`, `number`, `flag`, `text`, `select`, `button`), `name` (English and Italian, or one text for a name the owner gave) and, when they apply, `type`, `unit`, `decimals`, `min`, `max`, `step`, `options`, `writable`, `technical`. The things of a part once, `partThings`, and the parts a page at a time |
| `things.values` | `since`, `from` | The values of the parts changed since a `rev`, a page at a time, each with its `at` |
| `thing.set` | `thing`, and `value`, `number`, `brightness`, `r`, `g`, `b` | Turns, presses or sets one thing. What is set is checked on the device against what the thing says of itself: out of its limits or not among its values, the answer is `invalid` with the `field` |
| `things.kept` | `after` | The measures kept, the oldest first: `seq`, `thing`, `part`, `value`, `at`. With `after`, the device forgets the ones up to that `seq`: it forgets only what the server says it has |

**What a server can build on it**, with no code for each module:

| | From |
|---|------|
| A page, an app or a dashboard for a device it has never seen | The description: kinds, names, units, the limits for a gauge, the values of a choice for a list |
| Voice assistants and home systems, from cloud to cloud | The same list Home Assistant gets, with what can be set and within which limits |
| Thresholds and notifications | The values at every change; the limits say where a threshold makes sense |
| "No data for N minutes" | `at`: when each number was measured last |
| A history without holes | `things.kept`: what the device measured while the server could not be reached |
| Commands offered safely | The [manifest](http-api.md) says of each command whether it only reads, whether it reaches outside the device, whether its answer is to keep |

The measures are kept in memory, `LMP_THINGS_KEPT` of them (64), 20 bytes each: the room is
taken at the first one and given back when the server has them all. When it is full the oldest
one goes, and the answer of `things.kept` says how many went (`lost`). A restart loses them, and
a device that does not know the time keeps none: a measure without its time cannot be placed.
A firmware without a bridge keeps nothing.

## Build options

From [include/lmp_config.h](../include/lmp_config.h). Each one can be changed with
`-DLMP_...=value` in the build flags of an environment.

| Option | Default | Meaning |
|--------|---------|---------|
| `LMP_FEATURE_CLOUD` | network and tier 2 or more | 1: the service is in the build |
| `LMP_CLOUD_INBOX` | 2 | Requests of the server waiting for their round |
| `LMP_CLOUD_IN_MAX` | 1024 | Room for one request, in bytes |
| `LMP_CLOUD_OUT_MAX` | `LMP_HTTP_RESP_MAX + 256` | Room for one envelope to the server, in bytes |
| `LMP_CLOUD_LOG_QUEUE` | 4 | Log entries pushed between two rounds |
| `LMP_CLOUD_EVENT_QUEUE` | 8 | Events waiting for the next envelope |
| `LMP_CLOUD_WAITING` | 4 | Commands followed until they end |
| `LMP_CLOUD_PARTIAL_MS` | 2000 | Wait before a partial report leaves |
| `LMP_CLOUD_DATA_MS` | 500 | Wait before the documents of the modules leave |
| `LMP_THINGS_KEPT` | 64 | Measures kept while the server is away. 0: none |

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="lora.md">← LoRa</a> &nbsp;·&nbsp;
<a href="updates.md">Versions and updates →</a>
</p>
