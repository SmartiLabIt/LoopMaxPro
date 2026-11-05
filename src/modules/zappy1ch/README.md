<p align="center"><a href="../../../README.md"><img src="../../../docs/assets/img/logo.webp" alt="LoopMaxPro" height="44"></a>&nbsp;&nbsp;&nbsp;<img src="../../../docs/assets/t/zappy1ch.svg" alt="Zappy 1" height="50"></p>

<p align="center"><em>One relay, with its own name and schedules.</em><br>
A free module for <a href="../../../README.md">LoopMaxPro</a></p>

<p align="center">
<a href="../../../README.md">Home</a> &nbsp;·&nbsp;
<a href="../../../docs/README.md">Documentation</a> &nbsp;·&nbsp;
<a href="../../../README.md#modules">All modules</a> &nbsp;·&nbsp;
<a href="CHANGES.md">Versions</a> &nbsp;·&nbsp;
<a href="https://smartilab.it/firmware/">Web flasher</a>
</p>

---

<h2 id="overview"><img src="../../../docs/assets/h/overview.svg" alt="Overview" height="24"></h2>

<p align="center">
<img src="docs/how-it-works.svg?v=018dc4f" alt="How Zappy 1 works: one relay input is driven by one board pin; Zappy 1 adds the relay channel, while LoopMaxPro provides its name, schedules, local console and system services" width="900">
</p>

Zappy 1 controls one relay: for example, a lamp, socket, pump or valve. Give the channel a name,
switch it from the web console or use schedules. The same channel is available through the serial
console, HTTP API and MQTT. It runs locally from the board's access point or your home network;
no cloud account is required.

The web console draws the page from the module's channel and schedule data. Because there is just
one channel, it does not show **All on**, **All off** or **Invert all** controls. Zappy 1 uses the
same `PinBank` and `PinSchedule` SDK components as [Zappy 2](../zappy2/README.md).

<h2 id="features"><img src="../../../docs/assets/h/features.svg" alt="Features" height="24"></h2>

- One named relay channel.
- Up to four schedules: weekly on selected days or one-time.
- A schedule holds the channel on for its active span; a manual command cannot turn it off then.
- Optional restore of the last channel state after a power cut (on by default).
- Relay polarity setting for boards that switch on a low input.
- Home Assistant discovery as one outlet on the ESP32 family.
- No cloud service or account required for local use.

<h2 id="hardware"><img src="../../../docs/assets/h/hardware.svg" alt="Hardware" height="24"></h2>

| | |
|---|---|
| Boards | ESP32 family and ESP8266; suggested board: ESP32 WROOM-32, environment `esp32dev` |
| Parts | A one-channel relay board and its appropriate power supply |

### Wiring

| Relay board | Board |
|-------------|-------|
| `IN1` | GPIO22 on the suggested ESP32 board |
| `GND` | GND |
| `VCC` | The relay board's rated supply |

The default pin depends on the board; see `boardPins` in [module.json](module.json). A pin set
in the build takes precedence. Check that the selected GPIO is available on your board. For a
relay board that switches when its input is low, enable **Inverted** in the console.

Mains voltage is dangerous. Have relay wiring and mains connections installed and checked by
someone qualified. Never connect mains voltage to a GPIO.

| Board | Default pin |
|-------|------------:|
| ESP32 | 22 |
| ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6 | 5 |
| ESP8266 | 12 (D6) |

<h2 id="build-and-flash"><img src="../../../docs/assets/h/build-and-flash.svg" alt="Build and flash" height="24"></h2>

**Without building:** select Zappy 1 and a compatible board in the
[web flasher](https://smartilab.it/firmware/). The rest of this section is for PlatformIO.

Add an environment for your board in `platformio.ini`:

```ini
[env:zappy1_esp32]
extends = env:esp32dev
custom_lmp_modules = zappy1ch
```

Build and upload:

```text
pio run -e zappy1_esp32 -t upload
```

To use a different relay pin:

```ini
custom_lmp_modules = zappy1ch:pins=16
```

Or, without changing project files:

```text
LMP_MODULES="zappy1ch:pins=16" pio run -e esp32dev
```

On Windows PowerShell:

```powershell
$env:LMP_MODULES = "zappy1ch:pins=16"
pio run -e esp32dev -t upload
Remove-Item Env:LMP_MODULES
```

For board setup and first connection, see [Build and flash](../../../docs/build.md) and
[Boards](../../../docs/boards.md).

On ESP8266 the console is stored in the filesystem. For a first installation, also run
`pio run -e <environment> -t uploadfs`; this replaces the board's filesystem, including its
settings.

<h2 id="using-it"><img src="../../../docs/assets/h/using-it.svg" alt="Using it" height="24"></h2>

The console has three tabs:

| Tab | What it contains |
|-----|------------------|
| Device | The relay switch, its editable name and polarity, and the restore setting |
| Schedules | Add, edit, enable and remove schedules for the channel |
| Info | Module version, commands and documentation |

### How it behaves

- The channel starts as `Line 1`; commands address it as channel `0`.
- With restore enabled, the channel returns to its last state after restart. With restore off,
  it starts off.
- A schedule turns the channel on at its start and off at its end. During that span the schedule
  holds the channel; `set` and `toggle` are refused, and the console switch is locked.
- Schedules use the device clock, not the Internet. They do nothing until the device knows the
  time. Overlapping enabled schedules are rejected; `force=true` on `sched.update` disables the
  other conflicting schedule. A one-time schedule removes itself after it ends.
- Commands such as `all` and `flip` are available through the module command interface, but the
  single-channel console does not display collective-action buttons.

### Commands

The target is `mod.zappy1ch`.

| Command | Arguments | What it does |
|---------|-----------|--------------|
| `set` | `channel=0`, `value=true` or `false` | Turns the relay on or off |
| `toggle` | `channel=0` | Changes the relay to its opposite state |
| `name` | `channel=0`, `name` (1 to 24 characters) | Names the channel |
| `invert` | `channel=0`, `value=true` or `false` | Sets whether an on channel keeps its GPIO low |
| `restore` | `value=true` or `false` | Restores the last state on startup, or starts off |
| `sched.add` | `channel=0`, `on`, `off`, optional `type`, `days`, `enabled` | Adds a schedule |
| `sched.update` | `id`, optional `days`, `on`, `off`, `enabled`, `force` | Changes a schedule |
| `sched.remove` | `id` | Removes one schedule |
| `sched.clear` | `channel=0` | Removes all schedules for the channel |
| `all` | `value=true` or `false` | Turns the only channel on or off |
| `flip` | none | Toggles the only channel |

For example:

```text
cmd mod.zappy1ch set channel=0 value=true
cmd mod.zappy1ch name channel=0 name="Desk lamp"
cmd mod.zappy1ch sched.add channel=0 days=62 on=480 off=1080
```

Commands are also available through the HTTP API and serial console, and through MQTT when
enabled. See [Serial console](../../../docs/serial-console.md) and
[HTTP API](../../../docs/http-api.md).

### Schedules

| Argument | Weekly (`type=weekly`, default) | Once (`type=once`) |
|----------|---------------------------------|--------------------|
| `days` | Sum of 1 for Sunday, 2 for Monday, 4 for Tuesday and so on through 64 for Saturday; 127 means every day | Not used |
| `on` | Minute of the day to turn on, from 0 to 1439 | Unix time in seconds |
| `off` | Minute of the day to turn off; if earlier than `on`, it is the next day | Unix time in seconds |

This schedule turns the relay on from 08:00 to 18:00, Monday through Friday:

```text
cmd mod.zappy1ch sched.add channel=0 days=62 on=480 off=1080
```

A one-time schedule removes itself when it is over.

<h2 id="home-assistant"><img src="../../../docs/assets/h/home-assistant.svg" alt="Home Assistant" height="24"></h2>

Enable MQTT and configure a broker, then turn on **Presents itself to Home Assistant** in the
MQTT settings. On the ESP32 family, the channel is discovered as `ch0`, an outlet named after
the channel. A schedule that holds the relay also prevents Home Assistant from changing it.
MQTT is not included in the ESP8266 firmware.

More in [Discovery, MQTT and Home Assistant](../../../docs/network-and-home-assistant.md).

<h2 id="license"><img src="../../../docs/assets/h/license.svg" alt="License" height="24"></h2>

Zappy 1 is part of LoopMaxPro and is available under the project's
[license](../../../LICENSE).
