<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/serial-console.svg" alt="Serial console" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="http-api.md">← HTTP API</a> &nbsp;·&nbsp;
<a href="network-and-home-assistant.md">Discovery, MQTT and Home Assistant →</a>
</p>

---

Every LoopMaxPro board talks on its serial port: it prints the log as it is written and reads a
small command line. With a USB cable and a serial monitor you can read the state of the board,
run any command of any service or module, and find out why a board restarted or stopped
answering. This page is for who has a board on the bench: how to open the console, how commands
are written, the commands as reference tables, how to read the log, and what to look for when
something is wrong. It is the same on every board and with every module.

- [Opening the console](#opening-the-console)
- [The commands of the console](#the-commands-of-the-console)
- [Writing a command](#writing-a-command)
- [Commands of the services](#commands-of-the-services)
- [Reading the log](#reading-the-log)
- [The start-up lines](#the-start-up-lines)
- [Why the board started](#why-the-board-started)
- [When a board misbehaves](#when-a-board-misbehaves)

## Opening the console

The console runs at 115200 baud (`LMP_SERIAL_BAUD`). Any serial monitor works; with PlatformIO:

```
pio device monitor -e esp32dev
pio device monitor -p <port> -b 115200 --echo
```

The first form takes speed and filters from the environment. `--echo` shows what you type: the
board does not echo. A line ends with Enter (CR, LF or both); Backspace works. A line is at most
199 characters long (`LMP_CLI_LINE`).

Which port it is depends on the board ([Boards](boards.md)):

| The console is on | Environments |
|-------------------|--------------|
| the USB-serial bridge of the board | `esp32dev`, `esp32c3`, `esp32c6_n16`, `heltec_v3`, `heltec_wifi_kit8`, `esp8266_nodemcu`, `mega_wifi_esp8266` |
| the USB port of the chip | `esp32s3_n8`, `esp32s2_mini`, `esp32c3_usb` |

### Open the port without restarting the board

This matters more than anything else on this page. The log is in the RAM of the board: a
restart clears it, and with it the explanation of what happened. On a board with a USB-serial
bridge the lines DTR and RTS of the port drive the reset and the boot pin of the chip, and a
program that raises them restarts the board. Open the port with both low:

```
pio device monitor -p <port> -b 115200 --dtr 0 --rts 0 --echo
```

- **Never leave DTR raised alone on a board with a bridge.** It holds the boot pin, GPIO0, low.
  On the boards whose reset button is on that pin (`heltec_v3`, `heltec_wifi_kit8`,
  `esp8266_nodemcu`) LoopMaxPro reads it as the button held, and after 10 seconds
  (`LMP_RESET_HOLD_MS`) that is a factory reset.
- On `esp32s2_mini` the console is the USB device of the chip, which sends only to a program
  that raised DTR: if the board stays silent, open the monitor with `--dtr 1`.
- One program at a time on the port: close the monitor before you upload a firmware.
- The serial console asks for no password: who has the cable has the board in hand.
- `info` and `report` print the **device key** (`device.key`), the identity of the device. Do
  not paste it in messages, issues or documents.

The command line can be left out of a build with `-DLMP_FEATURE_CLI=0`; the log is printed all
the same. `doc sys.serial` says `"cli":true` when it is there. The build for the PC, the
`native` environment, reads the same commands from its standard input.

## The commands of the console

There are eight, the same on every board. The command line has no commands of its own beyond
these: it reads documents and sends commands through the bus that every other interface uses
([Kernel and services](kernel-and-services.md#documents-and-commands)).

| Command | Answer |
|---------|--------|
| `help` or `?` | this list |
| `info` | who the board is and how it is: the parts `device` and `system` of the report |
| `report [parts]` | the device report, whole or the parts named, separated by commas: `device`, `system`, `storage`, `clock`, `net`, `geo`, `cloud`, `modules`, `services`. Example: `report system,net` |
| `targets` | who has a document and commands: the services (`sys.<name>`) and the modules (`mod.<name>`) |
| `doc <target>` | the state of a service or of a module. Example: `doc sys.net` |
| `man <target>` | the commands of that target, with arguments, limits and flags |
| `cmd <target> <command> [name=value ...]` | runs a command. Example: `cmd sys.log setLevel level=warn source=net` |
| `log [count]` | the last `count` entries of the log; 20 when not written |

The answers are JSON on one line. A mistake in the line itself is answered in plain text:

| Text | Meaning |
|------|---------|
| `error: unknown command, type help` | the first word is not one of the eight |
| `error: line too long` | more than 199 characters |
| `error: target missing`, `error: target not found` | `doc` or `man` without a target, or with one that is not there |
| `error: use cmd <target> <command> [name=value]` | `cmd` without target or command |
| `error: answer too long` | the answer of a command is longer than 768 bytes: read the document of the target instead |

Two things to know about the answers:

- `doc`, `man`, `report` and `log` leave the port in pieces. A line of the log written at that
  moment by another part of the board can fall in the middle and break the JSON: ask again. The
  answer of `cmd` is built in memory and printed whole.
- A long answer, such as the whole report, can hold the system while it leaves the port. The
  log then shows `service 'serial' tick took N ms`: it is the reading, not a fault.

## Writing a command

```
cmd <target> <command> [name=value ...]
```

| Value | Written as |
|-------|------------|
| Text | as it is: `hostname=kitchen`. With spaces, in double quotes: `deviceName="Kitchen sockets"` |
| Number | `port=1883`, `keepAliveSec=60` |
| Yes or no | `true` or `false`; `on`, `yes`, `1` and `off`, `no`, `0` are taken too |

- Every argument is `name=value`, without spaces around the `=`. A word without `=` makes the
  whole line `invalid`.
- A name the command does not know is ignored. An argument that is required and missing, or a
  value outside its limits, stops the command: nothing is changed.
- The arguments are never written in the log: they can be passwords.
- **The commands "with confirmation" run at once.** The confirmation is asked by the web
  console, not by the board: `cmd sys.system factoryReset` clears the settings without asking.

The answer is the one every interface gives:

```json
{"result":{},"ok":true,"status":"ok"}
{"result":{},"ok":false,"status":"invalid","field":"level"}
{"result":{},"ok":true,"status":"pending","ref":3}
```

| Status | Meaning |
|--------|---------|
| `ok` | done |
| `pending` | accepted, the result arrives later |
| `busy` | another operation is in progress |
| `invalid` | bad argument or malformed line; `field` names the argument |
| `not_found` | no such target, command, source or file |
| `no_mem` | a fixed capacity is exhausted |
| `io` | hardware, storage or network failure |
| `timeout` | no answer in time |
| `unsupported` | not on this board or in this build |
| `denied` | not allowed now |
| `error` | anything else |

A command that answers `pending` ends later. The serial console does not print the event that
closes it: read the outcome in the log, or in the document of the target.

```
cmd sys.net scan
doc sys.net
```

Some lines to start from:

```
info
report services
cmd sys.config set hostname=kitchen deviceName="Kitchen sockets"
cmd sys.net join ssid="My network" psw=the-password
cmd sys.clock sync
cmd sys.system restart
man mod.zappy2
```

## Commands of the services

The exact map of a board is given by the board itself: `targets`, then `man <target>`. The
table below is the whole system; a target is there only when its service is in the build.
`?` marks an optional argument, **(c)** a command the web console confirms first, **(a)** a
command that answers `pending`. Limits, defaults and the meaning of each service are in
[Kernel and services](kernel-and-services.md#the-services).

| Target | Commands |
|--------|----------|
| `sys.system` | `restart` **(c)** · `factoryReset` **(c)** |
| `sys.log` | `clear` · `setLevel level, source?` (levels: `off`, `error`, `warn`, `info`, `debug`, `trace`) |
| `sys.config` | `set lang?, hostname?, deviceName?` · `save` |
| `sys.clock` | `set unix?, tz?, posix?, ntp?` · `sync` **(a)** |
| `sys.net` | `scan` **(a)** · `near` **(a)** · `test ssid, psw?` **(a)** · `join ssid, psw?` **(c)** **(a)** · `setAp ssid?, psw?` · `setMode mode` **(c)**: `AP` or `LAN` |
| `sys.web` | `setPassword old, new` · `logoutAll` · `files` · `useProgramConsole` **(c)** · `removeFile name` **(c)** |
| `sys.bus` | `i2cOpen bus?, sda, scl, hz?` · `i2cClose bus?` · `i2cScan bus?` **(a)** |
| `sys.mqtt` | `set enabled?, host?, port?, tls?, user?, psw?` · `setTopics base?, keepAliveSec?, events?, home?, homePrefix?` · `reconnect` · `publish topic, payload?, retain?` |
| `sys.ota` | `fromUrl url, sha256?, target?, size?` **(c)** **(a)** · `confirm` · `rollback` **(c)** · `abort` |
| `sys.geo` | `set lat?, lon?, city?, region?, country?, lookupUrl?` · `later` · `lookup` **(a)** · `clear` **(c)** |
| `sys.probe` | `ping host, timeoutMs?` **(a)** · `tcp host, port, timeoutMs?` **(a)** · `wake mac, broadcast?` **(a)** |
| `sys.cloud` | `set heartbeatMin?, pushLogLevel?` · `report` · `checkUpdate` · `update` **(c)** |
| `sys.modules` | `restart module` **(c)** |
| `sys.lora` | `set freq?, bw?, sf?, cr?, power?, sync?` · `send text` or `send hex` · `listen on` · `enable on` · `clear` |
| `sys.serial`, `sys.storage`, `sys.gpio`, `sys.reset`, `sys.httpclient` | no commands: only the document |

Not every firmware has every target. `sys.mqtt`, `sys.ota`, `sys.geo`, `sys.cloud` and
`sys.probe` are not in the firmware of the ESP8266 family. In the light edition `sys.probe` and
the command `near` are missing, and `sys.bus` is present only when a module needs it. `sys.lora`
is on the boards with a radio.

**Modules.** Each module has its own commands on `mod.<name>`: `man mod.<name>` lists them, and
the page of the module explains them, for example
[Zappy 2](../src/modules/zappy2/README.md) or [MeteoWifi](../src/modules/meteowifi/README.md).
The modules with settings also have `config.set name=value ...` and `config.reset` **(c)**; in
the builds with MQTT or the cloud service every module has `things`, `things.values` and
`thing.set` too.

For a diagnosis the **documents** matter more than the commands:

| Ask | To know |
|-----|---------|
| `info` | firmware, board, free memory, uptime, why the board started |
| `report services` | state and reason of every service, how many times it started and failed, its slowest tick |
| `doc sys.net` | mode, state, signal, failed attempts, Internet, the networks of the last scan |
| `doc sys.gpio` | who owns which pin, and its level now |
| `doc sys.bus` | where each I2C bus is open and for whom; the chips of the last scan |
| `doc sys.httpclient` | the last server asked and the outcome |
| `doc sys.mqtt` | broker, connected or not, the last error, what was told to Home Assistant |
| `doc sys.ota` | firmware installed, on trial or not, the last image and its error |
| `doc sys.modules`, `doc mod.<name>` | state and reason of each module; its own values |

## Reading the log

Every entry is printed on the port as it is written, one line each:

```
[   12345] I net      Internet reachable
```

| Part | Meaning |
|------|---------|
| `[   12345]` | milliseconds since the start of the board |
| `I` | level: `E` error, `W` warning, `I` info, `D` debug, `T` trace |
| `net` | source: `sys`, a service, a module by its name, or `cmd` for the commands that were run |
| the text | ends with `...` when it was longer than an entry holds |

Every command leaves a line of source `cmd`, whoever sent it, with where it came from
(`serial`, `local` for the web console, `mqtt`, `remote`, `internal`) and its outcome:

```
[   60210] I cmd      sys.net scan from serial: pending
[   75400] W cmd      sys.log setLevel from serial: invalid (level)
```

**The log kept in memory.** The board keeps the last entries in a ring: 100 or 200 on the ESP32
family, 16 in the light edition (the ESP8266 family and `esp32s2_mini`). What was printed before
you opened the port is still there:

```
log 50
```

```json
{"seqLast":412,"bootDoneMs":2310,"dropped":312,"items":[
 {"seq":411,"ms":60210,"lvl":"info","src":"cmd","msg":"sys.net scan from serial: pending"}]}
```

`seq` grows by one for each entry and never repeats; `dropped` counts the entries the ring has
overwritten; `cut` marks a text that was shortened. On the port the answer is one line.

**Levels.** `cmd sys.log setLevel level=warn` changes the level of every source,
`level=warn source=net` of one; `cmd sys.log clear` empties the ring. The change lasts until the
restart. A release firmware is built with the lines up to `info`: `debug` and `trace` lines are
not in it, and raising the level adds nothing. To see them, build with a higher
`LMP_LOG_LEVEL`; the `esp32dev_debug` environment does.

**What the board remembers, and what it does not:**

| | Kept in | Cleared by |
|---|---------|------------|
| The log | RAM | every restart |
| Last words: the reason of a restart and the last 12 lines before it | a memory the restart does not touch, ESP32 family only | a loss of power |
| The count of the starts and the last six, with reason, time and duration | flash | nothing; a factory reset keeps it |
| Settings, data of the modules | flash | a factory reset |

## The start-up lines

A board that starts writes, more or less in this order:

| Line | Source | Says |
|------|--------|------|
| `LoopMaxPro <version> (<build date>) board <board>` | `sys` | which firmware, for which board |
| `chip <model> rev N, N core, N MHz, flash N KB, heap N KB, reset: <reason>` | `sys` | the chip, and [why the board started](#why-the-board-started) |
| `previous start ended after N s: <why>` | `sys` | how the start before ended, ESP32 family |
| `  before: <line>` | `sys` | the last lines of the start before, when it did not end on purpose |
| `filesystem N KB, used N KB` | `storage` | the filesystem is mounted |
| `first start: device key created` | `config` | only at the very first start, and after the flash was erased |
| `host <hostname>, access point <name>, language <code>` | `config` | the settings are loaded |
| `mode LAN` | `sys` | the device is a client of a network; without this line it is in mode `AP` |
| `access point '<name>' on 192.168.4.1, channel N` | `net` | the access point is up |
| `connecting to '<network>'`, then `connected to '<network>', address <ip>, signal N dBm` | `net` | the client of the network |
| `<server> on port 80, console in the program` | `web` | the web server; `console not installed` means the files of the console are missing |
| `N modules`, then `<module> <version>: Ready` for each one | `modules` | the modules of this firmware |
| `boot done in N ms, heap N KB free, lowest N KB` | `sys` | every service has finished starting |
| `time set from ntp: <date and time>` | `clock` | the clock has the time |
| `Internet reachable` | `net` | the first check of Internet passed |

If the system cannot start at all the port says `LoopMaxPro: startup failed` and a status name.

## Why the board started

The `reset:` of the second line, also `resetReason` in `doc sys.system`:

| Reason | Means |
|--------|-------|
| `poweron` | power arrived, or the enable button was pressed. The board knows nothing of before, except the count of the starts |
| `software` | a restart asked by the firmware: a command, an update, a guard. `previous start ended ...` says which |
| `external` | the reset pin |
| `brownout` | the supply voltage fell too low: power supply, cable, contacts, peaks of current |
| `watchdog` | the chip restarted because the firmware did not answer; in brackets `int`, `task` or `other` |
| `panic` | an error of the firmware |
| `deepsleep` | the chip woke up from deep sleep |

`previous start ended after N s: <why>` tells how long the start before lasted and, after the
colon, who asked for the restart:

| Why | Who asked |
|-----|-----------|
| `command` | `sys.system restart` or `factoryReset`, from any interface |
| `reset button` | the reset button held: a factory reset |
| `firmware updated`, `firmware rollback` | the update service |
| `critical service failed` | a service the system cannot work without kept failing |
| `guard: main task stalled` | the main task stood still for 60 seconds |
| `guard: requests to servers stuck` | a request to a server hung far beyond its wait |
| `guard: network lost` | in mode `LAN`, no network or no Internet for 15 minutes |
| nothing after the seconds | nobody asked: a panic, a watchdog, the reset pin |

When the restart was not asked with one of the first four, the `before:` lines follow: they are
the scene of the fault, what the board was doing.

`doc sys.system` also has `starts`, how many times the board started in its life, and
`lastStarts`, the last six with `why`, `at` (Unix time, when the clock had it) and `ran`
(seconds, when the board could tell). Several starts in a day, or reasons other than `poweron`
and `software`, deserve a look.

## When a board misbehaves

First of all: **do not restart the board** and do not touch its power.
[Open the port without a restart](#open-the-port-without-restarting-the-board), then:

```
info
report services
log 200
```

Look for these lines.

| Line, or sign | Means | Where to look |
|---------------|-------|---------------|
| `reset: poweron` and a short uptime | the power went away and came back, or the enable button was pressed: a fault of the firmware does not leave this reason | cable, power supply, contacts |
| `service '<name>' error: <reason>` | a service failed; it is started again with growing delays | the reason in the line; `report services` |
| `critical service '<name>' keeps failing: <reason>` | an essential service does not start: the board restarts by itself | the reason in the line |
| `service '<name>' cannot start: a dependency is not in this build` | the firmware was built without a service this one needs | the build options of the environment |
| `service '<name>' tick took N ms` | that service held everything for N ms (reported above 50) | if it repeats: what the service does then. `modules` means a module; `serial` is your own reading |
| `connection to '<network>' lost`, `'<network>' not reached: <why>, attempt N` | the Wi-Fi fell, or does not come back | signal (`sta.rssi`), router, `sta.failures` in `doc sys.net` |
| `'<network>' not reachable: access point up as a fallback` | five attempts failed: the board turned its access point on and keeps trying | who looks for it on the network does not find it: it is on `192.168.4.1` |
| `mode LAN without a network: back to the access point` | the settings say `LAN` and hold no network | join a network again from the console |
| `Internet not reachable` | Wi-Fi connected, Internet not | router or line: the local console works |
| `network lost for N min` | the guard of the network is about to restart the board | router, signal, `doc sys.net` |
| `the device is not announced on the network: <status>`, `search of the other devices: <status>` | the name `<hostname>.local`, or the search of the other devices, does not work | `mdns` and `near.state` in `doc sys.net`; some guest networks block mDNS |
| `<server>: timeout`, `<server>: io`, source `httpclient` | a request to that server had no answer | `doc sys.httpclient`: `failures`, `last` |
| `<server>: no memory for a secure connection` | an `https` request waited for memory and did not get it; who asked tries again later | `heap` in `doc sys.system` |
| `not connected: <why>`, `connection lost: <why>`, source `mqtt` | the broker is not reached, refuses the board, or the connection fell | `doc sys.mqtt`: host, port, `tls`, user, `error` |
| `home: the description of <module>.<thing> is too long`, `home: <thing> of <module> not done: <status>` | a thing was not told to Home Assistant, or a request of it was not carried out | `home` in `doc sys.mqtt` |
| Home Assistant does not find the board, and the log is silent | most often: a broker is written in the settings and `enabled` is off | `enabled` in `doc sys.mqtt`; `cmd sys.mqtt set enabled=true` |
| `new firmware <version> on trial` | normal after an update; `new firmware confirmed` follows a minute later | a restart in between means the firmware of before came back: `doc sys.ota` |
| `<target> image given up after N bytes: <reason>` | an update was not installed | the reason in the line; `error` in `doc sys.ota` |
| `no answer from <server>`, `answer of <server> not usable`, source `clock` | the NTP server did not give the time | `ntpServer` in `doc sys.clock`; Internet |
| `settings not saved: <status>`, `key/value store not available: <status>` | the settings live in memory only: changes may not survive a restart | `doc sys.storage` |
| `<module>: init failed (<status>)`, `<module>: start failed (<status>)` | a module did not start; the others work | `doc sys.modules`: `state` and `reason`; `doc sys.gpio` for a pin taken by somebody else |
| `<module>: settings not saved (<status>)` | the file of a module was not written | `doc sys.storage`: room in the filesystem |
| `I2C N on pins A and B not opened: <status>` | the driver of the bus did not start on those pins | the pins; `doc sys.bus`, `doc sys.gpio` |
| `ping: <status>`, `tcp: <status>`, source `probe` | a question to the network could not leave. A device that does not answer is not an error: it is `timeout` in the outcome | `doc sys.probe` |
| `wrong password from <address>` | somebody failed the login of the web console; with `: address locked`, that address has to wait | `auth.wrongPasswords` in `doc sys.web` |

Three questions that divide the cases:

1. **Was it stopped, or was it unreachable?** If the log goes on without gaps the board was
   working: it was who looked that did not reach it. Another network, a phone still on the
   access point of another board, a `.local` name that changed.
2. **Did it restart?** `resetReason` and `lastStarts` say so. `poweron` is always the power.
3. **Was it the board, or a part of it?** A display or a strip standing still while the log
   goes on is the peripheral or its wires. `doc sys.gpio` shows the level of every pin now.

**Looking for a chip on I2C.** When a module says its display or sensor does not answer, the bus
can be tried by hand, one pair of pins at a time:

```
cmd sys.bus i2cOpen sda=<pin> scl=<pin>
cmd sys.bus i2cScan
doc sys.bus
cmd sys.bus i2cClose
```

`scan.found` lists the addresses that answered, as `"0x3C"`. `i2cOpen` answers `busy` while the
bus is open on other pins or a pin belongs to somebody else; `doc sys.gpio` says to whom.

**Asking the network.** On the boards with `sys.probe`, with the IPv4 address of the device to
ask:

```
cmd sys.probe ping host=<address>
cmd sys.probe tcp host=<address> port=1883
doc sys.probe
```

The outcome arrives a moment later, in `last`: `ok` with `rttMs`, `timeout`, or `denied` for a
port a device refuses.

**For developers.** Two build options of the ESP32 family write more on the serial port:
`-DLMP_HEAP_MAP=1` prints the largest blocks of memory in use, three minutes after the start
and the first time the free memory gets low; `-DLMP_NET_TRACE=1`, with the linker options listed
in [src/hal/esp32/esp32_net.cpp](../src/hal/esp32/esp32_net.cpp), prints second by second what
the Wi-Fi was given to send and what it refused. Both are described in
[include/lmp_config.h](../include/lmp_config.h).

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="http-api.md">← HTTP API</a> &nbsp;·&nbsp;
<a href="network-and-home-assistant.md">Discovery, MQTT and Home Assistant →</a>
</p>
