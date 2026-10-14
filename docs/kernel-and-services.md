<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/kernel-and-services.svg" alt="Kernel and services" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="hal.md">← Hardware abstraction layer</a> &nbsp;·&nbsp;
<a href="data-model.md">Data model and report →</a>
</p>

---

The kernel is the small core that every LoopMaxPro firmware shares: it starts and watches the
services, carries commands and events, keeps the log and the settings. The services are the
system itself: network, web server, storage, clock, updates, the host of the modules. This page
explains how the kernel works and lists every service with its target name, its commands and
its settings. Read it before you write a module or a service, and keep it as a reference when
you drive a board through the [HTTP API](http-api.md), MQTT or the
[serial console](serial-console.md).

- [What the kernel does](#what-the-kernel-does)
- [Life cycle of a service](#life-cycle-of-a-service)
- [Life cycle of a module](#life-cycle-of-a-module)
- [Documents and commands](#documents-and-commands)
- [Events](#events)
- [Worker](#worker)
- [Logging](#logging)
- [System settings](#system-settings)
- [The services](#the-services)
- [A service of the product](#a-service-of-the-product)

## What the kernel does

`Kernel` ([src/kernel/kernel.h](../src/kernel/kernel.h)) owns six parts and nothing else:

| Part | Class | Source | Job |
|------|-------|--------|-----|
| Service registry | `ServiceRegistry` | [service/registry.h](../src/kernel/service/registry.h) | start order, state machines, ticks, restart policy |
| Command bus | `CommandBus` | [command/command.h](../src/kernel/command/command.h) | targets, documents, validated commands |
| Event bus | `EventBus` | [event/event_bus.h](../src/kernel/event/event_bus.h) | small native events, queued and delivered in order |
| Worker | `Worker` | [worker/worker.h](../src/kernel/worker/worker.h) | the place for what takes seconds |
| Logger | `Logger` | [log/logger.h](../src/kernel/log/logger.h) | ring of fixed size entries, sources, levels, sinks |
| Settings | `Config` | [config/config.h](../src/kernel/config/config.h) | the system settings, as native structs |

### One context: the application task

Everything that changes the state of services and modules runs on one context, the
**application task**. On the ESP32 family it is a task of its own, `lmp_app`
(`LMP_APP_STACK`, 8192 bytes); on boards without tasks, such as the ESP8266, the same code runs
from the main loop. One round of the system is `Kernel::tick()`:

1. the call another task asked for with `invoke()`;
2. one job of the worker, where the worker has no task of its own;
3. the state machines of the services, then the ticks that are due;
4. the events that were queued.

Other contexts (the HTTP server, the drivers of the network) never touch a service directly.
They enter the application task with `Kernel::invoke(fn, timeoutMs)`, which runs the function
there and waits for it, or they leave a note that the service reads in its next tick. The logger
is the only part that can be called from any task.

Two guards watch the round:

- a tick of a service longer than `LMP_TICK_WARN_MS` (50 ms) is written in the log as
  `service '<name>' tick took N ms`;
- on the ESP32 family, an application task that does not come round for `LMP_STALL_GUARD_S`
  (60 s) restarts the board, and the next start says so in the log.

### Restart

`Kernel::requestRestart(reason, factoryReset, delayMs)` restarts the board a moment later, so
that the answer to the request can leave. Before the restart the services are stopped in
reverse order and the settings are saved; a factory reset takes the settings back to their
defaults first. The reason is kept in the last words of the board
([Logging](#logging)) and the next start writes it in the log.

## Life cycle of a service

A service is a class derived from `Service`
([src/kernel/service/service.h](../src/kernel/service/service.h)). It describes itself with a
`ServiceInfo`: id, name, icon, the modes it runs in, restart policy, tick period, dependencies.

```cpp
const ServiceInfo& ResetService::info() const {
  static const ServiceInfo kInfo{ServiceId::Reset, "reset", "...", Mode::Any,
                                 RestartPolicy::Retry, 20,
                                 DepList(ServiceId::Gpio, ServiceId::Config, ServiceId::Log)};
  return kInfo;
}
```

The name is short and lower case: it is the second half of the target `sys.<name>`. The tick
period is in milliseconds; `0` means the service is never ticked.

| Method | When it is called |
|--------|-------------------|
| `start(Kernel&)` | dependencies running and mode allowed. Must not block. `Ok`: ready. `Pending`: the service calls `ready()` or `fail(reason)` later |
| `stop()` | a dependency stopped, the mode changed, or the board is about to restart |
| `tick(nowMs)` | every `tickMs` while the service is `Ready` or `Degraded` |
| `onEvent(event)` | for every event, while the service is running |
| `describe(JsonWriter&)` | somebody asks for the state document of `sys.<name>` |
| `commands(CommandSet&)` | once, at the start of the system: the service declares its commands |
| `reportPart()` | the name of the part of the device report filled by `describe()`, or null |
| `api()` | the interface offered to other services and to modules, or null |

Another part of the system gets that interface with `kernel.api<INetService>()`: the result is
null when the service is not in the build.

### States

```
              dependencies running          start() Ok,
              and mode allowed              or ready()
   Waiting ----------------------> Starting ------------> Ready <----> Degraded
      ^                             |    ^                  |              |
      |                 fail(), or  |    | retry            |              |
      |                 30 s        v    |                  |              |
      |                            Error                    |              |
      |                                                     |              |
      +------------ dependency lost, or mode changed -------+--------------+
```

| State | Meaning |
|-------|---------|
| `Stopped` | not started yet, stopped before a restart, or a dependency is not in this build (reason `missing dependency`) |
| `Waiting` | waits for a dependency or for the mode; the reason says which (`waiting for a dependency`, `not used in this mode`) |
| `Starting` | `start()` answered `Pending`; after 30 seconds the registry gives up with `start timeout` |
| `Ready` | working |
| `Degraded` | working in part; the reason says why (`time not set`, `no network`, `a module did not start`) |
| `Error` | failed, with its reason. The restart policy decides what happens next |

The registry computes the start order from the declared dependencies; a cycle stops the start
of the system. A dependency counts as running when it is `Ready` or `Degraded`.

| Restart policy | In `Error` |
|----------------|------------|
| `Never` | the service stays in error |
| `Retry` | started again after 2 s, then 4, 8 ... up to 60 s between attempts |
| `Critical` | as `Retry`; after 5 failed attempts in a row the kernel restarts the board (`critical service '<name>' keeps failing`) |

`system`, `log`, `storage` and `config` are critical; every other service retries.

Each change of state is written in the log and published as a `ServiceState` event. When every
service allowed in the current mode has finished starting, well or not, the kernel writes
`boot done in N ms`.

### Operating mode

The device is in one of two modes, kept in the settings (`sys.mode`):

| Mode | Value | Meaning |
|------|-------|---------|
| `AP` | 1 | the device is its own access point |
| `LAN` | 2 | the device is a client of a Wi-Fi network |

A service declares `Mode::Ap`, `Mode::Lan` or `Mode::Any`. The services that need a network
(`httpclient`, `probe`, `mqtt`, `cloud`) are `Mode::Lan`: in mode `AP` they wait. A change of mode
publishes `ModeChanged`.

## Life cycle of a module

The modules of a firmware are chosen by the build and hosted by the `modules` service. Each one
gets the target `mod.<name>`, its settings file `/cfg/m/<name>.json`, its ticks and the events.
A module knows nothing of consoles, brokers or servers.

| Phase | When | What happens |
|-------|------|--------------|
| `onInit` | the `modules` service starts | the module takes pins and buses and declares its commands; then its file is read |
| `onStart` | the network service is running (as access point or client), or the board has no network | the module works |
| `onTick` | at the period of the manifest; `0` means never | periodic work |
| `onEvent` | for every event, while the module works | Internet, time, MQTT, other modules |
| `onStop` | the service stops, `sys.modules restart`, the module failed | pins and buses go back to the system |

A module is `Stopped`, `Init`, `Ready`, `Degraded` or `Error`. A module that does not start goes
to `Error` with its reason, gives back what it took and lets the others work; the service
becomes `Degraded`. The contract and the reusable parts are in
[Modules and SDK](modules-sdk.md).

## Documents and commands

Everything the system shows or accepts has one of two forms:

| Form | What it is | Who writes it |
|------|------------|---------------|
| **Document** | the state of a target, read only, as JSON | `describe()` of a service or of a module |
| **Command** | an action with checked arguments and an outcome | a handler declared by a service or by a module |

Services and modules use the same mechanism. Who receives is called a **target**:

| Target | Examples |
|--------|----------|
| `sys.<service>` | `sys.net`, `sys.log`, `sys.config`, `sys.system` |
| `mod.<module>` | `mod.zappy2`, `mod.meteowifi` |

A document is written at the moment it is asked for; nothing is kept in memory. The schema of
the documents and of the device report is in [Data model and report](data-model.md).

### Declaring a command

```cpp
static constexpr ArgSpec kSetArgs[] = { Arg::u8("channel", 0, 5), Arg::boolean("value") };

set.add("set", LMP_HELP("Switches one channel"), kSetArgs,
        [this](const Args& a, CmdContext&) { return write(a.u8("channel"), a.boolean("value")); });
```

- The argument table must have static storage: the bus keeps a pointer to it.
- `Arg::boolean`, `u8`, `u16`, `u32`, `i32`, `i64`, `f64`, `str` and `secret` build a spec with
  its limits; `.optional()` makes an argument optional. A `secret` never appears in logs or
  documents.
- `LMP_HELP("...")` keeps the help text out of the light edition of the firmware.
- The last parameter of `add` is a set of flags:

| Flag | Meaning |
|------|---------|
| `kCmdConfirm` | the web console asks for confirmation before it sends the command. The device does not check it |
| `kCmdAsync` | the handler answers `Pending`; the outcome arrives later with a `CommandDone` event |
| `kCmdAnyArgs` | the command takes any name: the handler reads the request itself, from `ctx.raw` |
| `kCmdReadOnly` | the command only reads. The manifest says `readOnly` |
| `kCmdSensitive` | the command acts outside the device, or undoes what does not come back. The manifest says `sensitive` |
| `kCmdSecret` | the answer of the command carries something to keep. The manifest says `secret` |
| `kCmdUi` | the command serves the page of the module only. The manifest says `ui` |

The last four change nothing in how the bus runs a command: they tell who offers the commands
of a device without knowing its modules what each one is. A command without them is taken as
one that changes something.

### What the bus does

For every request the bus:

1. finds the target and the command (`not_found` otherwise);
2. checks type and limits of each argument; the first one refused is named in `field`;
3. calls the handler with native values;
4. writes one line in the log, source `cmd`: target, command, where it came from, outcome.
   The arguments are never logged;
5. gives the outcome back.

The handler is the same wherever the request comes from, so the same action gives the same
result from every interface:

| Source | Way in |
|--------|--------|
| `local` | `POST /api/cmd/<target>/<command>`, see [HTTP API](http-api.md) |
| `remote` | the cloud bridge, see [Cloud bridge](cloud.md) |
| `mqtt` | `<base>/cmd/<target>/<command>`, see [Discovery, MQTT and Home Assistant](network-and-home-assistant.md) |
| `serial` | `cmd <target> <command> name=value`, see [Serial console](serial-console.md) |
| `internal` | a module that calls another module |

### The answer

Every interface gives back the same JSON object:

```json
{"result":{"ssid":"Home","timeoutSec":180},"ok":true,"status":"pending","ref":7}
```

| Member | Meaning |
|--------|---------|
| `result` | what the handler wrote; empty for most commands |
| `ok` | `true` for `ok` and `pending` |
| `status` | one of the names below |
| `field` | the argument that was refused, when there is one |
| `ref` | only with `pending`: the `CommandDone` event that closes the command carries the same number |

The status codes are the same everywhere in the system
([src/base/status.h](../src/base/status.h)). Events carry the number, documents and answers the
name.

| # | Name | Meaning |
|---|------|---------|
| 0 | `ok` | done |
| 1 | `pending` | accepted, the result arrives later |
| 2 | `busy` | another operation is in progress |
| 3 | `invalid` | bad argument or malformed input |
| 4 | `not_found` | no such target, command, source, file |
| 5 | `no_mem` | a fixed capacity is exhausted |
| 6 | `io` | hardware, storage or network failure |
| 7 | `timeout` | no answer in time |
| 8 | `unsupported` | not on this board or in this build |
| 9 | `denied` | not allowed now |
| 10 | `error` | anything else |

### Manifest

The bus can describe itself: the list of the targets, and for each target its commands with
help text, flags and the spec of every argument (type, limits, default, `required`). The web
console builds its forms from it. It is `GET /api/manifest` and `GET /api/manifest/<target>`
over HTTP, `targets` and `man <target>` on the serial console.

| Limit | Build option | Default |
|-------|--------------|---------|
| Targets, services plus modules | `LMP_MAX_TARGETS` | 20 + `LMP_MAX_MODULES` |
| Commands of all the targets | `LMP_MAX_COMMANDS` | 48, 96 or 160 by tier |
| Arguments of one command | `LMP_CMD_MAX_ARGS` | 8 |
| Room for the text arguments of one call | `LMP_CMD_TEXT` | 256 bytes |

## Events

An event is four native values ([src/kernel/event/event.h](../src/kernel/event/event.h)):
`id`, two bytes `a` and `b`, and a 32 bit `value`. `publish()` can be called from any task and
only queues the event; the handlers always run on the application task. The queue holds
`LMP_EVENT_QUEUE` events (24): one more is lost and counted. Events become JSON only when they
leave the device, to the web console, to MQTT or to the cloud bridge:

```json
{"id":"svc","a":8,"b":3,"value":0,"service":"net","state":"Ready"}
```

| Event | Name outside | Carries | Published by |
|-------|--------------|---------|--------------|
| `ServiceState` | `svc` | `a` service id, `b` new state; outside also `service` and `state` | registry |
| `ConfigChanged` | `config` | `a` section: 0 `sys`, 1 `net`, 2 `web`, 3 `clock`, 4 `cloud`, 5 `mqtt`, 6 `geo` | the service that changed it |
| `ModeChanged` | `mode` | `a` new mode: 1 `AP`, 2 `LAN` | kernel |
| `NetApUp`, `NetApDown` | `net.ap.up`, `net.ap.down` | | net |
| `NetStaConnected`, `NetStaLost` | `net.sta.up`, `net.sta.lost` | | net |
| `NetFallback` | `net.fallback` | | net |
| `NetJoinResult` | `net.join` | `a` 1 for a test, `b` 1 when accepted, `value` the `ref` of the command | net |
| `NetNear` | `net.near` | `b` how many other LoopMaxPro devices are on the network | net |
| `InternetUp`, `InternetDown` | `internet.up`, `internet.down` | | net |
| `TimeSynced` | `time` | `a` source: 1 client, 2 NTP, 3 cloud; `value` Unix time | clock |
| `ModuleState` | `module.state` | `a` index of the module, `b` new state | modules |
| `ModuleData` | `module.data` | `a` index of the module | modules |
| `CommandDone` | `cmd` | `value` the `ref` of the command, `b` status | the service that ended an asynchronous command |
| `OtaProgress` | `ota.progress` | `b` percent, `value` bytes received | ota |
| `OtaDone` | `ota.done` | `b` status, `value` bytes received | ota |
| `ResetRequested` | `reset` | `a` 0 restart, 1 factory reset | kernel |
| `MqttConnected`, `MqttLost` | `mqtt.up`, `mqtt.lost` | | mqtt |
| `CloudConnected`, `CloudLost` | `cloud.up`, `cloud.lost` | | cloud |
| `CloudUpdate` | `cloud.update` | the update offered through the bridge changed | a cloud bridge |
| `LoraReceived` | `lora.rx` | `a` length in bytes, `value` sequence number of the packet | lora |
| `LoraSent` | `lora.tx` | `b` status, `value` sequence number of the packet | lora |
| `ProductChanged` | the word of that service | `a` a state of its own, `b` 1 when the device is blocked | [a service of the product](#a-service-of-the-product), in a firmware that has one |

Service states are numbered `Stopped` 0, `Waiting` 1, `Starting` 2, `Ready` 3, `Degraded` 4,
`Error` 5; module states `Stopped` 0, `Init` 1, `Ready` 2, `Degraded` 3, `Error` 4.

## Worker

A request to a server or a download lasts seconds: it must not hold the application task. Such
work is posted to the worker, `kernel.worker().post(job)`. Where the board has tasks the worker
has one of its own, `lmp_worker` (`LMP_WORKER_STACK`, 10240 bytes), created with the first job;
on boards without tasks a job runs inside the round of the system, which waits for it. The queue
holds `LMP_WORKER_QUEUE` jobs (4); `post` answers `busy` when it is full.

A job never touches the state of services and modules: it works on data of its own and leaves
the result where the application task picks it up. The system uses the worker for the requests
of `httpclient`, for the search of the other devices on the network and for the writes of the
settings files of the modules.

## Logging

The logger keeps fixed size entries in a ring: nothing is allocated after the start. An entry
has a sequence number that never repeats, the uptime in milliseconds, a level, a source and the
text. A text longer than the entry is cut and marked.

| | Build option | Default |
|---|--------------|---------|
| Entries kept | `LMP_LOG_RING` | 32, 100 or 200 by tier; 16 in the light edition |
| Longest text | `LMP_LOG_MSG_LEN` | 96, 128 or 160 bytes by tier |
| Sources | `LMP_LOG_MAX_SOURCES` | 32 |
| Sinks | `LMP_LOG_MAX_SINKS` | 4 |
| Levels compiled in | `LMP_LOG_LEVEL` | 3 (`info`); 4 with `LMP_DEBUG` |

**Levels**: `error` 1, `warn` 2, `info` 3, `debug` 4, `trace` 5; `off` 0. Calls above
`LMP_LOG_LEVEL` are removed by the compiler, arguments included, so a release firmware has no
`debug` and `trace` lines at all. At run time the level can be changed for every source or for
one, with `sys.log setLevel`; the change lasts until the restart.

**Sources**: `sys` is always there. Each service registers its own name, each module the name
of the module, and the command bus writes as `cmd`.

**Writing**, from a service or a module:

```cpp
src_ = kernel.log().addSource(info().name, info().icon);
LMP_LOGI(src_, "filesystem %lu KB, used %lu KB", total, used);
LMP_LOGW(src_, "connection to '%s' lost", ssid);
```

`LMP_LOGE`, `LMP_LOGW`, `LMP_LOGI`, `LMP_LOGD` and `LMP_LOGT` take a source and a `printf`
format.

**Where the entries go**:

| Reader | How |
|--------|-----|
| Serial port | every entry is printed as it is written, see [Serial console](serial-console.md) |
| Web console | reads the ring with `GET /api/log` and follows the new entries as events |
| Cloud bridge | errors, or errors and warnings, when `sys.cloud` is set to push them |
| Last words | on the ESP32 family the last 12 entries of level `info` or higher are kept in a memory that a restart does not clear and a power loss does. After a restart nobody asked for (a panic, a watchdog, a guard of the system) the next start writes them in the log as `before: ...` lines |

## System settings

The settings of the system are native structs in memory, grouped in sections; in storage each
field is one entry of the key/value store. A change is written `LMP_CONFIG_SAVE_DELAY_MS` (2 s)
after the last one, or at once with `sys.config save` and before a restart. A value missing in
storage takes its default. In documents a secret is replaced by `<key>Set`, `true` or `false`.

| Section | Key | Default | Changed with |
|---------|-----|---------|--------------|
| `sys` | `key` | created at the first start, 30 characters | never: it is the identity of the device |
| | `hostname` | `loopmax-` and the last four hex digits of the MAC address | `sys.config set` |
| | `deviceName` | empty | `sys.config set` |
| | `lang` | `en` | `sys.config set` |
| | `mode` | `1` (`AP`) | `sys.net setMode`, `sys.net join` |
| `net` | `apSsid` | `LoopMax-` and the last four hex digits of the MAC address | `sys.net setAp` |
| | `apPsw` | `loopmaxap` | `sys.net setAp` |
| | `staSsid`, `staPsw` | empty | `sys.net join` |
| | `prevSsid`, `prevPsw` | empty | by the system: the network to go back to |
| | `captive` | `true` | - |
| | `rollbackSec` | `180` | - |
| | `probeUrl` | `http://connectivitycheck.gstatic.com/generate_204` | - |
| `web` | `adminPsw` | empty: the password of the access point is used | `sys.web setPassword` |
| | `sessionTtlMin` | `1440` | - |
| `clock` | `tz`, `posixTz` | `UTC`, `UTC0` | `sys.clock set` |
| | `ntpServer` | `pool.ntp.org` | `sys.clock set` |
| `cloud` | `heartbeatMin` | `15` | `sys.cloud set` |
| | `pushLogLevel` | `0` | `sys.cloud set` |
| `mqtt` | `enabled` | `false` | `sys.mqtt set` |
| | `host`, `user`, `psw` | empty | `sys.mqtt set` |
| | `port`, `tls` | `8883`, `true` | `sys.mqtt set` |
| | `base` | empty: `lmp/<hostname>` | `sys.mqtt setTopics` |
| | `keepAliveSec`, `events` | `60`, `true` | `sys.mqtt setTopics` |
| | `home`, `homePrefix` | `true`, `homeassistant` | `sys.mqtt setTopics` |
| `geo` | `lat`, `lon`, `city`, `region`, `country` | not known | `sys.geo set`, or a lookup |
| | `lookupUrl` | `https://ipwho.is/` | `sys.geo set` |

The keys marked `-` have no command: they keep the default of the firmware.

The settings of a module are not here. Each module has its own file, `/cfg/m/<name>.json`,
written about two seconds after a change and before a restart.

**Factory reset** (`sys.system factoryReset`, or the reset button held): every section goes
back to its defaults, the settings files of the modules and the settings of the LoRa radio are
removed. The device key is kept. The device comes back as an access point.

## The services

`LoopMax::begin()` ([src/app/loopmax.cpp](../src/app/loopmax.cpp)) adds to the kernel the
services the build has. Tier 1 is the ESP8266 family, tier 2 and 3 the ESP32 family; the light
edition is the ESP8266 family and the boards built with `LMP_LIGHT=1`. See [Boards](boards.md).

| Service | Target | Modes | Depends on | In the build | What it does |
|---------|--------|-------|------------|--------------|--------------|
| [system](#system) | `sys.system` | any | - | always | chip, memory, uptime, why the board started, restart |
| [log](#log) | `sys.log` | any | system | always | the ring of the log, levels, sources |
| [serial](#serial) | `sys.serial` | any | log | always | log on the serial port, command line |
| [storage](#storage) | `sys.storage` | any | log | always | filesystem and key/value store |
| [config](#config) | `sys.config` | any | storage, log | always | loads and saves the settings |
| [gpio](#gpio) | `sys.gpio` | any | log | always | the pins, each with its owner |
| [bus](#bus) | `sys.bus` | any | gpio, log | `LMP_FEATURE_BUS`: always, in the light edition when a module needs it | shared I2C and SPI buses |
| [clock](#clock) | `sys.clock` | any | config, log | always | time, time zone, NTP |
| [reset](#reset) | `sys.reset` | any | gpio, config, log | always | the reset button |
| [net](#net) | `sys.net` | any | config, log | `LMP_FEATURE_NET` | access point and Wi-Fi client, fallback, safe change of network |
| [web](#web) | `sys.web` | any | net, config, storage, log | `LMP_FEATURE_NET` | HTTP server, console files, API, login, events |
| [httpclient](#httpclient) | `sys.httpclient` | LAN | net, log | `LMP_FEATURE_HTTPCLIENT`: tier 2 and 3; on tier 1 when a module needs it | requests to servers |
| [probe](#probe) | `sys.probe` | LAN | net, log | `LMP_FEATURE_PROBE`: tier 2 and 3, not in the light edition | ping, TCP ports, Wake-on-LAN |
| [mqtt](#mqtt) | `sys.mqtt` | LAN | net, config, log | `LMP_FEATURE_MQTT`: tier 2 and 3 | broker connection, Home Assistant |
| [ota](#ota) | `sys.ota` | any | storage, log | `LMP_FEATURE_OTA`: tier 2 and 3 | firmware update |
| [geo](#geo) | `sys.geo` | any | config | `LMP_FEATURE_GEO`: tier 2 and 3 | where the device is |
| [cloud](#cloud) | `sys.cloud` | LAN | net, config, log | `LMP_FEATURE_CLOUD`: tier 2 and 3 | what goes to a cloud bridge, and when |
| [modules](#modules) | `sys.modules` | any | config, gpio, bus, clock, storage, log | `LMP_FEATURE_MODULES` | hosts the modules |
| [lora](#lora) | `sys.lora` | any | storage, gpio, log | `LMP_FEATURE_LORA`: boards with a radio | LoRa packets, radio to radio |

A firmware can add one more service of its own to these: see
[A service of the product](#a-service-of-the-product).

In the tables below `?` marks an optional argument, **(c)** a command the web console confirms
first, **(a)** an asynchronous command. A service without a table has no commands: only its
document.

### system

Chip, flash, memory, uptime, the reason of the last reset. It also counts the starts of the
board and keeps the last six, with reason, time and duration, where a power loss does not reach.
Part `system` of the report.

Document: `chip`, `edition`, `cores`, `revision`, `cpuMhz`, `flash`, `flashHz`, `sdk`,
`heap` (`free`, `min`, `total`, `largest`), `psram`, `stackMin`, `uptime`, `resetReason`,
`starts`, `lastStarts` (`why`, `at`, `ran`), `mode`, `debug`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `restart` | | **(c)** restarts the device |
| `factoryReset` | | **(c)** back to the factory settings; the device key is kept |

### log

Document and commands of the kernel logger.

Document: `count`, `capacity`, `dropped`, `seqLast`, `bootDoneMs`, `level`, and `sources`
with `name`, `icon` and `level` of each one.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `clear` | | deletes the stored entries; the sequence numbers keep growing |
| `setLevel` | `level`, `source?` | `level` is `off`, `error`, `warn`, `info`, `debug`, `trace` or a digit from 0 to 5. Without `source`: every source |

### serial

Prints the log on the console serial port at `LMP_SERIAL_BAUD` (115200) and, with
`LMP_FEATURE_CLI=1` (the default), reads the command line described in
[Serial console](serial-console.md). The command line has no commands of its own: it reads
documents and sends commands through the same bus as every other interface.

Document: `baud`, `ready`, `cli`, `level`.

### storage

Mounts the filesystem (LittleFS) and opens the key/value store. Other parts read and write files
through `IStorage`: `writeFile` writes to a temporary file and renames it, so the old content
survives a power loss. A filesystem that cannot be mounted is formatted: the device works, the
files of the console have to be loaded again. Part `storage` of the report.

Document: `fs` (`mounted`, `total`, `used`), `kv` (`ok`).

### config

Loads the [system settings](#system-settings), saves them after a change and gives the device
its identity at the first start: the device key, the name of the access point and the host name.

Document: `dirty`, `firstBoot`, and one object for each section.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `lang?`, `hostname?`, `deviceName?` | at least one. `lang`: two lower case letters. `hostname`: 1 to 32 letters, digits and hyphens, no hyphen at the ends |
| `save` | | writes the settings to storage now |

### gpio

Every pin in use has an owner: two parts of the system cannot drive the same pin. Services and
modules use it through `IGpio`
([src/services/gpio/gpio_service.h](../src/services/gpio/gpio_service.h)): `claim`, `release`,
`write`, `read`, PWM (`pwm`, `duty`), lines of addressable LEDs (`pixels`, `show`), timed
pulses (`pulseOut`, `pulseWatch`, `pulseTake`) and counted edges (`edgeWatch`, `edgeTake`).
A pin that belongs to somebody else answers `busy`.

Document: `used`, `capacity` (`LMP_MAX_PINS`, 24), and `pins` with `pin`, `owner`, `label`,
`mode`, `level` and, for a PWM pin, `duty` in thousandths.

### bus

Buses for the hardware outside the board. A bus is shared: the first part that opens it chooses
pins and speed, who comes later joins it, and other pins are refused with `busy`. The bus closes
when its last user leaves. A board with chips of its own on I2C opens bus 0 at the start
(`LMP_I2C0_SDA`, `LMP_I2C0_SCL`). SPI is opened by the module that uses it; the document says
pins and users.

Document: `i2c` (`bus`, `open`, `sda`, `scl`, `hz`, `users`), `scan` (`state`, `bus`, `found`,
`more`), `spi`, `spiBus`, `uart`, `oneWire`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `i2cOpen` | `bus?`, `sda`, `scl`, `hz?` | `bus` 0 by default; `hz` from 10000 to 1000000, 100000 by default |
| `i2cClose` | `bus?` | closes the bus opened with `i2cOpen` |
| `i2cScan` | `bus?` | **(a)** looks for chips from `0x08` to `0x77`, eight addresses each round; the list, 16 at most, is in `scan.found` |

### clock

Wall clock and time zone. The time comes from who has it: a command (the browser, in mode `AP`),
or the NTP server of the settings as soon as the device is on a network, and again at every
reconnection. Nothing waits. An answer before September 2020 or after 2100 is ignored. Until
the time is known the service is `Degraded` with `time not set`. Part `clock` of the report.

Document: `unix`, `synced`, `source` (`none`, `client`, `ntp`, `cloud`), `syncedAt`, `tz`,
`posix`, `ntpServer`, `ntp` (`available`, `running`, `answers`), `local`, `offset`, `weekday`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `unix?`, `tz?`, `posix?`, `ntp?` | at least one of `unix`, `posix`, `ntp`. `tz` is the name shown to the user and needs `posix`, the rule the C library reads. An empty `ntp` asks nobody |
| `sync` | | **(a)** asks the NTP server now; ends with the first answer, or with `timeout` after 15 s |

### reset

Samples the reset button, `LMP_RESET_PIN`, which connects the pin to ground. Held for
`LMP_RESET_HOLD_MS` (10 s) it asks for a factory reset. A board without a button has the service
`Degraded` with `no reset button on this board`.

Document: `pin`, `holdMs`, `pressed`, `heldMs`.

### net

Access point and client of a network are two separate things: "connected" always means the
client. Part `net` of the report.

| | |
|---|---|
| State | `off`, `ap`, `connecting`, `connected`, `retrying`, `fallback` |
| Access point | address `192.168.4.1`; channel 1, 6 or 11, chosen from the MAC address (`LMP_NET_AP_CHANNEL` fixes it) |
| One attempt | 20 s at most to get an address |
| New attempts | after 2, 4, 8, 16 s |
| Fallback | after `LMP_NET_FALLBACK_AFTER` (5) failed attempts in a row the access point comes up; the mode stays `LAN` and the network is tried every `LMP_NET_FALLBACK_RETRY_SEC` (120 s) |
| Network found again | the access point stays up 30 s more, then goes down |
| Mode `LAN` without a network | the device goes back to mode `AP`: it would be unreachable |
| Scan | the strongest `LMP_NET_SCAN_MAX` networks (16; 8 on tier 1); `timeout` after 15 s |
| Internet check | 1.5 s after the connection, then every 5 minutes, every minute while it fails. One request to `probeUrl`; needs `httpclient` |
| Name on the network | `<hostname>.local` with mDNS; not in the light edition |
| Guard | in mode `LAN`, without network or Internet for `LMP_NET_GUARD_MIN` (15 min) and nobody on the access point, the board restarts; after such a restart it waits `LMP_NET_GUARD_AGAIN_MIN` (60 min) |

**Safe change of network**, command `join`: the new network is tried in memory and written in
the settings only after it gave an address. Until then the settings are the ones of before, even
through a power loss. When the address does not arrive within `rollbackSec`, or the network
refuses the device three times, the device goes back to the network it had and says why in
`join.last`: `joined`, `tested`, `not_found`, `refused`, `timeout`.

Document: `mode`, `state`, `sta` (`connected`, `ssid`, `ip`, `gateway`, `mask`, `dns`, `rssi`,
`channel`, `mac`, `failures`), `ap` (`up`, `ssid`, `ip`, `clients`, `channel`, `captive`,
`pswDefault`), `internet`, `internetChecked`, `mdns`, `host`, `join`, `scan`, `near`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `scan` | | **(a)** the list is in `scan.networks` |
| `near` | | **(a)** looks for the other LoopMaxPro devices on the network; the list is in `near.devices`. Only with mDNS |
| `test` | `ssid`, `psw?` | **(a)** tries the credentials for 30 s and keeps nothing. Only while the access point is up (state `ap` or `fallback`), otherwise `denied` |
| `join` | `ssid`, `psw?` | **(c)** **(a)** moves the device to a network; `psw` empty for an open network, otherwise 8 to 63 characters |
| `setAp` | `ssid?`, `psw?` | name and password of the access point, from the next restart; `psw` 8 to 63 characters |
| `setMode` | `mode` | **(c)** `AP` or `LAN`. `LAN` is `denied` while no network was ever joined |

How the devices find each other is in
[Discovery, MQTT and Home Assistant](network-and-home-assistant.md).

### web

The HTTP server on port `LMP_HTTP_PORT` (80): the files of the web console, the API under
`/api/`, the login and the live events. Routes and formats are in [HTTP API](http-api.md), the
console in [Web console](web-console.md).

| | |
|---|---|
| Access | free for who is connected to the access point of the device; with a login for who comes from the network. User `admin` |
| Password | `adminPsw`; while it is empty, the password of the access point |
| Sessions | `LMP_MAX_SESSIONS` (2, 4 or 8 by tier), each valid `sessionTtlMin` minutes; they end with a restart |
| Wrong passwords | after `LMP_WEB_LOGIN_ATTEMPTS` (5) from one address it has to wait, `LMP_WEB_LOGIN_LOCK_SEC` (30 s) the first time |
| Answers | one buffer of `LMP_HTTP_RESP_MAX` bytes (3072, 8192 or 16384 by tier). An answer that does not fit is an error `507`, never a cut answer |

Document: `server`, `port`, `running`, `api`, `auth`, `events`, `console`, `requests`,
`refused`, `failed`, `canUpload`, `uploads`, `answerMax`, `urls`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `setPassword` | `old`, `new` | `new` empty, or at least 8 characters. Closes every session |
| `logoutAll` | | closes every session |
| `files` | | the files of the console in the filesystem, with their size |
| `useProgramConsole` | | **(c)** removes the console files uploaded by hand: the console inside the firmware is used |
| `removeFile` | `name` | **(c)** removes one file of the console |

### httpclient

Requests to servers, for the other services and for the modules (`IHttpClientService::fetch`).
A request never runs on the application task: it travels in the worker and its answer arrives
later, on the application task. One request travels at a time; `LMP_HTTPC_QUEUE` (4) wait, and
one more is `busy`. The default wait for an answer is `LMP_HTTPC_TIMEOUT_MS` (10 s). Addresses
that begin with `https` work where the client of the board is secure (`secure` in the document).
Logs and documents carry only the host: an address can hold a key. A request that gives no sign
of life for its wait and 60 s more restarts the board.

Document: `available`, `secure`, `busy`, `waiting`, `waitingMax`, `requests`, `failures`,
`last` (`host`, `status`, `result`).

### probe

Questions to the local network, for the modules and for the diagnosis of a board. One question
travels at a time, on a task of the service; the outcome arrives later and the last one is in
the document.

Document: `available`, `busy`, `waiting`, `questions`, `failures`, `last` (`what`, `host`,
`port`, `result`, `rttMs`, `found`).

| Command | Arguments | Notes |
|---------|-----------|-------|
| `ping` | `host`, `timeoutMs?` | **(a)** `host` is an IPv4 address; `timeoutMs` from 100 to 10000, 1000 by default |
| `tcp` | `host`, `port`, `timeoutMs?` | **(a)** `ok` the port takes connections, `denied` a device is there and refuses, `timeout` nobody answers |
| `wake` | `mac`, `broadcast?` | **(a)** Wake-on-LAN; `mac` as `aa:bb:cc:dd:ee:ff` |

### mqtt

One more way in for the commands and one more way out for documents and events. Off by default:
without `enabled` and a broker the service is `Ready` and calls nobody, and the memory for the
messages is taken the first time the settings ask for a broker. The device connects as
`lmp-<hostname>`; the topics begin with the base, `lmp/<hostname>` unless the settings say
another.

| Topic | Direction | Content |
|-------|-----------|---------|
| `<base>/status` | out, retained | `online`, `offline` |
| `<base>/cmd/<target>/<command>` | in | the arguments as JSON |
| `<base>/ack/<target>/<command>` | out | the answer of the command |
| `<base>/get/<target>` | in | asks a document; the target `report` asks the report |
| `<base>/doc/<target>` | out | the document |
| `<base>/evt` | out | the events of the system, when `events` is on |

Document: `available`, `enabled`, `connected`, `host`, `port`, `tls`, `user`, `pswSet`, `base`,
`events`, `upSec`, `connections`, `sent`, `received`, `dropped`, `error`, `home`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `enabled?`, `host?`, `port?`, `tls?`, `user?`, `psw?` | at least one; connects again with the new values |
| `setTopics` | `base?`, `keepAliveSec?`, `events?`, `home?`, `homePrefix?` | `keepAliveSec` from 10 to 3600. `home`: the modules are told to Home Assistant; `homePrefix`: where it looks for them |
| `reconnect` | | leaves the broker and connects again |
| `publish` | `topic`, `payload?`, `retain?` | sends a message under the base. `cmd`, `ack`, `get`, `doc`, `evt` and `status` belong to the system |

Topics, discovery and Home Assistant are in
[Discovery, MQTT and Home Assistant](network-and-home-assistant.md).

### ota

Firmware update. An image arrives in pieces, from a file sent by the console
(`POST /api/upload/ota`) or from an address (`fromUrl`), one at a time.

| | |
|---|---|
| Checks | the declared length, the SHA-256 fingerprint computed while the image arrives, the check of the board on the whole image |
| Error page of a server | never taken for a firmware: a download whose answer is not a success is given up |
| After the installation | event `OtaDone`, then a restart 1.5 s later |
| New firmware on trial | confirmed after `LMP_OTA_CONFIRM_SEC` (60 s) without restarts, or with `confirm` |
| Progress | event `OtaProgress` every five points in a hundred; every 64 KB when the length is not known |

Document: `available`, `installed`, `build`, `state` (`idle`, `receiving`, `installed`,
`failed`), `target`, `from`, `received`, `size`, `percent`, `error`, `sha256`, `trial`,
`canRollback`, `fromUrl`, `installs`, `failures`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `fromUrl` | `url`, `sha256?`, `target?`, `size?` | **(c)** **(a)** `url` begins with `http://` or `https://`; `sha256` is 64 hex digits; `target` is `app` (default) or `fs`. Needs `httpclient` |
| `confirm` | | confirms the firmware that is running |
| `rollback` | | **(c)** goes back to the firmware of before and restarts, where the board can |
| `abort` | | gives up the image that is arriving |

Versions and the ways to update are in [Versions and updates](updates.md).

### geo

Where the device is. The position is written by who knows it, and the more precise source wins:
a module with a GPS (`gps`), then the owner (`manual`), then the network (`lookup`, `cloud`). A
less precise source gets `denied` and changes nothing. Part `geo` of the report.

| | |
|---|---|
| Lookup server | `lookupUrl`, by default the public service `https://ipwho.is/`. Empty: nobody is asked |
| When it is asked | once for each start, when Internet becomes reachable; with `lookup`; right after `clear` |
| Answers understood | JSON with `lat` or `latitude`, `lon`, `lng` or `longitude`, `city`, `region` or `regionName`, `countryCode` or `country_code`, `ip` or `query`, `isp` or `org`. `success: false` means no answer |
| Time zone | when the answer has `timezone.name` and `timezone.posix` and the device is still on `UTC`, the time zone is taken from it |
| GPS | the saved position moves only past `LMP_GEO_GPS_STEP` (about 50 m) and is written at most once in `LMP_GEO_GPS_SAVE_MS` (10 minutes) |
| Public address, provider | `publicIp` and `isp` are of this start: they are not saved |

Document: `known`, `source`, `lat`, `lon`, `city`, `region`, `country`, `asked`, `publicIp`,
`isp`, `lookup` (`configured`, `state`).

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `lat?`, `lon?`, `city?`, `region?`, `country?`, `lookupUrl?` | at least one. `lat` and `lon` together; `country` is two letters. `denied` when a GPS on the board has the position |
| `later` | | the owner does not say where the device is: the console does not ask again |
| `lookup` | | **(a)** asks the lookup server now |
| `clear` | | **(c)** forgets the position; GPS and server write it again |

### cloud

The public half of the link to a server: what the device tells and when, the form of the
envelope, and the requests that arrive, taken to the command bus with source `remote`. They are
the same documents and the same commands of the console. How an envelope travels belongs to a
**bridge**, a module that attaches itself to the service through `ICloudBridge`
([src/services/cloud/cloud_service.h](../src/services/cloud/cloud_service.h)). Without a bridge
the service is `Ready`, does nothing and takes no memory: the device works on its own. Part
`cloud` of the report. The whole subject is in [Cloud bridge](cloud.md).

Document: `attached`, `bridge`, `connected`, `upSec`, `connections`, `sent`, `received`,
`refused`, `dropped`, `failed`, `eventsLost`, `heartbeatMin`, `pushLogLevel`, `pending`, and
`detail`, written by the bridge.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `heartbeatMin?`, `pushLogLevel?` | at least one. `heartbeatMin` from 0 to 1440, 0 for no periodic report. `pushLogLevel`: 0 logs only on request, 1 errors, 2 errors and warnings |
| `report` | | sends the whole report now |
| `checkUpdate` | | asks through the bridge whether there is an update |
| `update` | | **(c)** installs the update the bridge offers, then restarts |

Without a bridge `report`, `checkUpdate` and `update` answer `unsupported`.

### modules

Hosts the modules of the firmware ([Life cycle of a module](#life-cycle-of-a-module)). A build
holds `LMP_MAX_MODULES` modules (4, 8 or 16 by tier). When a module changes its state the
service publishes `ModuleState`; when it changes its data, `ModuleData`,
`LMP_MODULE_DATA_DELAY_MS` (200 ms) after the first change, so that changes close together
become one. The document is also the part `modules` of the report.

Document: `count`, `max`, and `list` with `name`, `title`, `icon`, `kind`, `version`, `ui`,
`code`, `state`, `reason`, `tickMs`, `settings`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `restart` | `module` | **(c)** saves what the module changed, stops it and starts it again |

Besides its own commands, every module has some standard ones on its target `mod.<name>`:

| Command | Arguments | Notes |
|---------|-----------|-------|
| `config.set` | any setting of the module | modules with settings; the names are in the manifest of the target |
| `config.reset` | | **(c)** modules with settings: back to the ones the module was born with |
| `things` | `from?` | what the module shows and lets command; in the builds with `mqtt` or `cloud` |
| `things.values` | `since?`, `from?` | the values that changed since a revision |
| `thing.set` | `thing`, `value?`, `brightness?`, `r?`, `g?`, `b?`, `number?` | turns, presses or sets one thing; `value` is `ON`, `OFF`, `PRESS`, or the text or the choice to set; `number` is for a number that can be set |
| `things.kept` | `after?` | the measures made while the server of the device was away, 12 at a time, the oldest first: `seq`, `thing`, `part`, `value`, `at`. `after` is the last one the asker has: it is forgotten with those before it. Up to `LMP_THINGS_KEPT` measures are kept, 64, in memory |

See [Modules and SDK](modules-sdk.md) and [Writing a module](writing-a-module.md).

### lora

Packets between devices, radio to radio, without a network. The service owns the radio: modules
send and receive through `ILora` and never touch the chip. It is in the builds of the boards
that have a radio, and it refuses what the rules of the band do not allow. The whole subject is
in [LoRa](lora.md).

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `freq?`, `bw?`, `sf?`, `cr?`, `power?`, `sync?` | at least one. `freq` and `bw` in Hz, `sf` 7 to 12, `cr` 5 to 8 (4/5 to 4/8), `power` in dBm |
| `send` | `text` or `hex` | one of the two, 120 bytes at most; the end arrives with the event `LoraSent` |
| `listen` | `on` | hears the other devices when the radio is not sending |
| `enable` | `on` | turns the radio on or off |
| `clear` | | empties the received packets and the counters |

## A service of the product

A firmware can carry one more service, written outside the system, for what belongs to one
product and not to LoopMaxPro: a trial period, a subscription, a key to check. **No firmware of
this repository has one.** The system does not know what such a service does. It asks it four
things, through `IProduct`
([src/kernel/service/product.h](../src/kernel/service/product.h)):

| Function | What the system does with it |
|----------|------------------------------|
| `blocked()` | true: the modules of the device must not work now |
| `reason()` | one word, chosen by the service. It is the reason of a module stopped by the block, the `field` of a command refused for it, and the name outside of the event `ProductChanged` |
| `describeDevice(JsonWriter&)` | optional: members of its own in the part `device` of the report |
| `onServerRequest(type, body)` | optional: a request that arrives through a [cloud bridge](cloud.md) with a `type` the cloud service does not know. `Unsupported`: not its own. `Ok`: taken. Anything else: its own, and refused |

For the rest it is a service like the others: a name, a target with its document and its
commands, dependencies, a row in the Services tab of the console. When what it says changes, it
publishes `ProductChanged` with `b` 1 for a device that is blocked and 0 for one that is not.

While the device is blocked:

| Module | What happens |
|--------|--------------|
| Any module | It is stopped, as at any stop: its outputs go to the safe state and its commands answer no more. Its state is `Stopped`, with the word of the service as the reason. `sys.modules restart` answers `denied`, with that word as the `field` |
| A module of the kind `bridge` | Never stopped: what ends the block arrives through it |
| A module that returns true from `reducedWhenBlocked()` | Stays on, `Degraded`, and is told with `onBlocked(true)`: it stops its own work and keeps only what says why, a screen for example. `onBlocked(false)` when the block ends |

When the block ends the modules start again at once, without a restart of the board.

To add one, write a class that is both a `Service` and an `IProduct`, with the id
`ServiceId::Product`, and a function that returns it, in a source file of the firmware. Then
name the function in the environment of the firmware:

```cpp
class Guard : public lmp::Service, public lmp::IProduct {
 public:
  const lmp::ServiceInfo& info() const override;          // ServiceId::Product, name, dependencies
  lmp::Status start(lmp::Kernel& kernel) override;
  void stop() override;
  void* api() override { return static_cast<lmp::IProduct*>(this); }

  bool blocked() const override;
  const char* reason() const override { return "guard"; }
};

namespace lmp {
Service& my_product() {
  static Guard guard;
  return guard;
}
}  // namespace lmp
```

```ini
[env:my_device]
extends = env:esp32dev
build_flags =
  ${env:esp32dev.build_flags}
  -DLMP_PRODUCT_SERVICE=my_product
```

The program hands the service to the application before the start, and the `modules` service
starts after it. Without the flag nothing of this is in the firmware. A complete example, with a
module that keeps a reduced form, is in
[test/test_product/test_product.cpp](../test/test_product/test_product.cpp). What such a service
can add to the web console is in
[Web console](web-console.md#adding-to-the-loopmaxpro-space).

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="hal.md">← Hardware abstraction layer</a> &nbsp;·&nbsp;
<a href="data-model.md">Data model and report →</a>
</p>
