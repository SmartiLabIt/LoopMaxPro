<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/architecture.svg" alt="Architecture" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="boards.md">← Boards</a> &nbsp;·&nbsp;
<a href="hal.md">Hardware abstraction layer →</a>
</p>

---

This page explains how LoopMaxPro is built: the ideas behind it, its layers and where each one
lives in the source tree, how the system starts and runs, how it uses memory and how it protects
the device. Read it before you change the system or write a module; the pages that follow go
into each layer.

- [Principles](#principles)
- [Layers](#layers)
- [Source tree](#source-tree)
- [The path of a command](#the-path-of-a-command)
- [How the system starts](#how-the-system-starts)
- [How the system runs](#how-the-system-runs)
- [Operating modes](#operating-modes)
- [Memory, tiers and editions](#memory-tiers-and-editions)
- [Security](#security)

## Principles

LoopMaxPro is a modular embedded OS for ESP32 and ESP8266 boards, built with PlatformIO. It is
the re-engineering of LoopMax v1. A firmware is the fixed system (kernel, services, web console)
plus the modules chosen at build time, which give the device its purpose.

Three ideas shape everything else.

**Native C++ data in the engine, JSON only at the borders.** Inside the firmware the state is
made of structs, arrays and enums, and the parts call each other with C++ types. JSON is written
when a document leaves the device and checked when a request enters it; it is never the working
format. Reading a value is a field access, memory has a fixed size, and a wrong field name is a
compile error. See [Data model and report](data-model.md).

**One OS on many boards.** Kernel, services, SDK and modules are written against a set of C++
interfaces, the [hardware abstraction layer](hal.md). A build compiles one implementation of it:
the ESP32 family (the same code with the Arduino core and with ESP-IDF), the ESP8266, or a PC.
A board with less memory gets smaller limits and fewer optional services, not different code.

**A module declares, the system carries.** A module declares its state and its commands once.
The system turns the state into a document, keeps the settings on file, and makes the same
commands reachable from the web console over HTTP, from an MQTT broker, from the serial command
line and from a cloud bridge. Services and modules declare no HTTP route and no MQTT topic for
that: a few generic ones carry the documents and the commands of every target. See
[Modules and SDK](modules-sdk.md).

The rules that follow from them:

| Rule | Why |
|------|-----|
| Dependencies point down: modules, SDK, services, kernel, HAL interfaces | no include cycles, no object that knows everything |
| Nothing above the HAL includes a concrete implementation of it | portability; the whole system builds and runs on a PC |
| C++17 without exceptions and without RTTI; errors are `Status` codes | predictable size and behaviour |
| Fixed-capacity strings, vectors, rings and callables ([src/base](../src/base)) instead of `std::string`, `std::vector` and `std::function` | predictable memory |
| One writer: the state of services and modules changes only on the application task | no races, no locks inside a module |
| Every service describes itself: name, dependencies, state document, commands | no manager that does everything |
| The start order is computed from the declared dependencies | no hard-wired order |
| Answers have a maximum size, the log is read in pages, changes arrive as events | no peaks of memory |
| A secret is accepted in input and never written in output | security |

## Layers

<p align="center">
<img src="assets/img/architecture.svg" alt="LoopMaxPro architecture: boards at the bottom, then the hardware abstraction layer, the kernel, the services, the SDK and the modules; beside them the web console, the HTTP API, the serial console, MQTT and a cloud bridge, which all lead to one command bus; in orange what a developer adds" width="900">
</p>

The same layers, with the folder of each one:

```
  Web console      web/              in the browser: pages of the system and of the modules
 ──────────────────────────────────────────────────────────────── HTTP and events ─────────
  Modules          src/modules/      what the device is for
  SDK              src/sdk/          Module, State, ModuleConfig, PinBank, PinSchedule,
                                     lights, things, tools
  Services         src/services/     system log serial storage config gpio clock reset bus
                                     lora net web httpclient probe mqtt ota geo cloud modules
  Drivers          src/drivers/      chips outside the MCU, reached through the HAL buses
  Kernel           src/kernel/       Kernel, ServiceRegistry, EventBus, CommandBus, Config,
                                     Logger, Worker, JSON writer and reader, field tables
  HAL interfaces   src/hal/api/      ISystem IClock ISerial IRtos IGpio IPwm IAdc II2c ISpi
                                     IFs IKv IOta INet IHttpServer IHttpClient IMqtt ...
 ───────────────────────────────────────────────────────────────────────────────────────────
  HAL              src/hal/esp32/    ESP32 family, Arduino and ESP-IDF
  (one per build)  src/hal/arduino/  ESP8266
                   src/hal/native/   PC: unit tests and simulator
```

`src/base/` sits beside the layers: status codes, fixed-capacity containers and delegates, with
no dependency of their own, used by every layer.

Who sees what:

- The **kernel** includes only `src/base` and `src/hal/api`.
- A **driver** reaches the hardware only through `src/hal/api`, with or without a library for
  its chip. It is portable code, the same for every framework and board: the HAL says which
  chip is on the board and how it is wired, the driver knows how to talk to it.
- A **service** uses the kernel, the HAL interfaces, the drivers and the public interfaces of
  the other services. It asks for another service with `Kernel::api<Interface>()`, which returns
  null when that service is not part of the build.
- A **module** is written with the SDK and the public interfaces of the services. It takes pins
  and buses from the `gpio` and `bus` services, never from a concrete HAL folder.
- [src/app/loopmax.h](../src/app/loopmax.h) is the only place that knows the concrete services:
  it puts together the kernel and the services of the build.
- The **console** knows the device only through the [HTTP API](http-api.md).

Which services are in a firmware is decided at build time by the `LMP_FEATURE_*` options of
[include/lmp_config.h](../include/lmp_config.h). The services are described one by one in
[Kernel and services](kernel-and-services.md).

## Source tree

```
platformio.ini         how a build is composed: common part, framework, MCU family, tier
boards/                the board environments (*.ini) and the ESP-IDF settings of each one
partitions/            partition tables of the ESP32 family
include/               lmp_config.h: compile-time configuration; lmp_version.h: generated
src/
  main.cpp             entry point for Arduino, ESP-IDF and the PC
  app/                 composition of the system: the kernel and the services of the build
  base/                status codes, fixed strings and containers, delegates
  hal/                 api/ the interfaces; esp32/, arduino/, native/, common/ the implementations
  kernel/              kernel, service registry, events, commands, settings, log, JSON,
                       field tables, worker
  drivers/             portable drivers of external chips
  services/<name>/     one folder for each service
  sdk/                 what a module is written with
  modules/<name>/      the free modules: module.json, src/, ui/, README.md
web/                   the console: kit/, console/, device/, i18n/, styles/
tools/pio/             the build scripts PlatformIO runs
tools/web/build.py     the build of the console
```

[Build and flash](build.md) explains how an environment is chosen and how the modules of a
firmware are listed.

## The path of a command

The same command, switching a relay or changing the language, always reaches the same handler,
whoever sends it.

```
 web console ─── HTTP: POST /api/cmd/<target>/<command> ───┐
 MQTT broker ─── <base>/cmd/<target>/<command> ────────────┤
 serial line ─── cmd <target> <command> name=value ────────┼──▶ CommandBus ──▶ handler of the
 cloud bridge ── a request of type "cmd" ──────────────────┤    checks the     service or module
 another module ───────────────────────────────────────────┘    arguments,            │
                                                                logs the source       ▼
                                                                              native state changes
       web console, MQTT broker, cloud bridge ◀────────── event ◀─────────────────────┘
```

- A **target** is `sys.<service>` or `mod.<module>`, for example `sys.net` or `mod.zappy2`. Each
  target has a state document and a list of commands.
- The command bus checks every argument against its declared type and limits before the handler
  runs. The handler receives native values.
- Every interface gives back the same answer: `{"result":{...},"ok":true,"status":"ok"}`, with
  `field` when a value was refused and `ref` when the command ends later.
- The way back is the same for everybody: the state changes, an event is published, and every
  connected interface is told.

The details are in [HTTP API](http-api.md), [Serial console](serial-console.md),
[Discovery, MQTT and Home Assistant](network-and-home-assistant.md) and [Cloud bridge](cloud.md).

## How the system starts

[src/main.cpp](../src/main.cpp) is the same sequence for every framework:

```
hal::boot()              early hardware setup: the objects of the board are created
hal::board()             the table of the board: one pointer for each capability
board.serial->begin()    the console, at LMP_SERIAL_BAUD
LoopMax app(board)       the kernel and the services of this build
app.begin()              first lines of the log: firmware, chip, why the board restarted
                         how the previous start ended, when the board could keep it
                         every service is registered
                         Kernel::begin():
                           the start order is computed; a cycle of dependencies stops here
                           each service gets its target sys.<name> and declares its commands
kernel.startTask()       the application task; without tasks, loop() calls Kernel::tick()
```

Nothing has started yet at this point. The services come up in the following rounds of the
loop: the registry starts a service when the current mode allows it and all its dependencies
are running. A service that cannot finish at once answers `Pending` and reports later; none is
allowed to block. When every service that can run has finished starting, well or badly, the log
says `boot done` with the time it took.

Modules follow the same idea in two steps: `onInit()` when the base services are ready (pins,
buses, settings, commands), `onStart()` when the network is ready, as an access point or on a
network.

A service that fails is started again with growing delays, from 2 to 60 seconds, according to
its restart policy. A service marked critical that keeps failing restarts the board.

## How the system runs

```
 Wi-Fi and TCP/IP ────┐
 HTTP server task ────┼── invoke(), events, inboxes ──▶  lmp_app     services, modules,
 MQTT client task ────┘                                              commands, events

                                                         lmp_worker  what takes long: requests
                                                                     to servers, downloads
```

**One application task.** Everything that changes the state of services and modules runs on one
context, the task `lmp_app` (on chips with more cores it is pinned to the second one). The other
tasks never touch that state. They enter in three ways:

- `Kernel::invoke()` runs a function on the application task and waits for it: the web service
  carries every HTTP request there;
- `EventBus::publish()` can be called from any task and only queues the event; the handlers run
  on the application task;
- a driver that receives data, such as the MQTT client, leaves it in a small inbox that the
  owning service empties at its next tick.

The logger is the only kernel component that any task may call directly.

**The loop.** One round of `Kernel::tick()` does, in order:

1. the call another task asked for with `invoke()`;
2. one job of the worker, on the boards where the worker has no task;
3. the registry: one step of the state machine of each service, then the ticks that are due.
   Each service declares its own period; a tick longer than `LMP_TICK_WARN_MS` (50 ms) is
   written in the log with the name of the service;
4. the delivery of the queued events to the services, and through the `modules` service to the
   modules;
5. the checks for a restart that was asked for, and the watchdog.

With tasks, `Kernel::run()` repeats the round and then waits at most 5 ms, or less when another
task asks for a call. Without tasks, on the ESP8266, the Arduino `loop()` calls `Kernel::tick()`.

**The worker.** A request to a server or a download lasts seconds. It runs as a job of the
`Worker`, in the task `lmp_worker`, created with the first job, while the application task keeps
going. A job never touches the state of services and modules: it works on data of its own and
leaves the result where the application task picks it up. On the boards without tasks a job runs
inside the round of the system, which waits for it.

**Asynchronous commands.** A command that cannot finish at once answers `pending` with a
reference; its end arrives later as an event that carries the same reference.

On the ESP32 family a guard watches the application task: when it does not come round for
`LMP_STALL_GUARD_S` seconds (60) the board restarts, and the next start writes in the log the
last lines of the previous one.

## Operating modes

| Mode | Network | When |
|------|---------|------|
| `AP` | the access point of the device, named `LoopMax-` plus the end of its MAC address, at `192.168.4.1`, with a captive portal | first start, after a factory reset |
| `LAN` | client of the configured Wi-Fi network | once a network has been saved |
| `LAN`, fallback | the access point comes up again while the device keeps trying its network | after `LMP_NET_FALLBACK_AFTER` failed attempts (5) |

Each service declares the modes it runs in. `httpclient`, `mqtt`, `probe` and `cloud` run only
in `LAN`; the others, and the modules, in both. When the mode changes the registry stops what is
not allowed any more and starts what now is. Internet reachable and time synchronized are not
modes: they are events.

## Memory, tiers and editions

Memory is taken when the system starts and kept: the log ring, the answer buffer of the web
service, the tables of targets and commands all have a fixed size. That size comes from the
**tier** of the board, set by its environment with `LMP_TIER`.

| | Tier 1, MIN | Tier 2, MID | Tier 3, MAX |
|---|---|---|---|
| Boards of the project | ESP8266 | ESP32, ESP32-C3, ESP32-C6, ESP32-S2, ESP32-S3 without PSRAM | ESP32-S3 with PSRAM, the PC |
| Log entries kept | 32 | 100 | 200 |
| Longest log message | 96 | 128 | 160 |
| Largest HTTP answer | 3 KB | 8 KB | 16 KB |
| Sessions of the console | 2 | 4 | 8 |
| Modules | 4 | 8 | 16 |
| Commands | 48 | 96 | 160 |

The tier also decides which optional services are built. `mqtt`, `ota`, `geo`, `cloud` and
`probe` need tier 2. On tier 1 the HTTP client is built only when a module of the firmware asks
for it, and for plain HTTP: there is no memory for a secure connection.

More about memory:

- The large buffers go to the external RAM (PSRAM) on the boards that have it, so the internal
  RAM stays free for the radio, the network and the tasks.
- `LMP_MODULE_MEM` (1 small, 2 medium, 3 large) tells a module how much it can keep in its own
  lists and buffers on this board.
- Every value of [include/lmp_config.h](../include/lmp_config.h) can be overridden with
  `-DLMP_xxx=value`. A module that needs a different value on one board asks for it in the
  `tune` table of its `module.json`: the build applies it only to the firmwares with that
  module on that board, so one module never changes the limits of the others.

**Two editions.** The normal edition, reported as `full`, and the **light** edition are the same
system and the same console. The light edition spends less memory:

| | Normal | Light |
|---|---|---|
| Log entries kept for the console | by tier | 16; the serial port has them all |
| Help texts of the commands | in the manifest | left out: the console shows names and arguments |
| Browsers following the events | as many as the sessions | 1 |
| Name `<host>.local` on the network (mDNS) | yes | no |
| `bus` service | always | only when a module of the firmware requires it |
| `probe` service | tier 2 and up | no |

The ESP8266 runs the light edition; another board asks for it with `-DLMP_LIGHT=1`, as the
`esp32s2_mini` environment does. The report says which one is running in `system.edition`.

## Security

**Access.** A browser connected to the access point of the device is in without a login: it
already passed the password of the access point. From the network, the API gives only the
public state of the device without a login; documents and commands need the password of the
console. The login gives back a token made with the hardware random generator; sessions live in
memory, expire, and end with a restart. After `LMP_WEB_LOGIN_ATTEMPTS` wrong passwords (5) an
address has to wait, 30 seconds the first time and twice as long at every new lock, up to 15
minutes.

**Secrets never leave.** A field marked secret (a Wi-Fi password, the password of the broker)
is accepted in input and never written in a document: in its place the device writes
`<key>Set`, true or false. The arguments of a command are never written in the log. The Wi-Fi
credentials belong to the settings of the system and are never handed to the radio driver to
keep.

**Every command leaves a trace.** The log records target, command, outcome and where the command
came from: `local`, `remote`, `mqtt`, `serial` or `internal`.

**Connections.** On the ESP32 family the requests to `https` addresses are verified against the
certificates of the public authorities that the framework carries, and MQTT uses TLS by default.
The ESP8266 has no memory for a secure connection: its HTTP client speaks plain HTTP only, and it
has no MQTT.

**Updates.** A firmware image is checked against its size and its SHA-256 fingerprint before it
is accepted. A new firmware is on trial: it is confirmed after it has run for
`LMP_OTA_CONFIRM_SEC` seconds (60), and where the board allows it a firmware that does not hold
goes back to the one before. See [Versions and updates](updates.md).

**Changes that could lose the device.** A change of Wi-Fi network is tried first: when the new
network does not work within the allowed time, the device goes back by itself to the network it
had.

**Identity.** Each device creates a random key at its first start and keeps it for ever, also
through a factory reset. The key is not part of the public state: it is written only in the
report given to an authenticated request and in what the device sends through a cloud bridge.

Access from outside the local network is not part of the system itself: it is the job of a
[cloud bridge](cloud.md), which carries the same documents and commands to a server.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="boards.md">← Boards</a> &nbsp;·&nbsp;
<a href="hal.md">Hardware abstraction layer →</a>
</p>
