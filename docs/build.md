<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/build.svg" alt="Build and flash" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="boards.md">Boards →</a>
</p>

---

This page explains how a LoopMaxPro firmware is built and written to a board: what you need on
your computer, how `platformio.ini` puts a firmware together, which environments exist, how the
modules of a firmware are chosen, and how to run the whole system on a PC without any board. Start
here if you want to try LoopMaxPro on a board you own. What each board has, and its pins, is in
[Boards](boards.md).

To put a ready firmware on a board without building anything, use the
[web flasher](https://smartilab.it/firmware/) of SmartiLab: it runs in the browser, and you
choose the firmware and a compatible board.

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [How the configuration is composed](#how-the-configuration-is-composed)
- [Environments](#environments)
- [Tiers and editions](#tiers-and-editions)
- [Choosing the modules](#choosing-the-modules)
- [Building and uploading](#building-and-uploading)
- [Partitions](#partitions)
- [Running on a PC](#running-on-a-pc)
- [Ready-made products](#ready-made-products)
- [Build options](#build-options)

## Requirements

PlatformIO is the only build system. Everything else is downloaded by it at the first build.

| What | Notes |
|------|-------|
| PlatformIO Core, the `pio` command | Alone, or inside the PlatformIO IDE extension of VS Code, which the project recommends in `.vscode/extensions.json` |
| Python 3 | The one PlatformIO brings is enough. The build scripts use the standard library only |
| Platforms | `espressif32@7.0.1` and `espressif8266@4.2.1`, pinned in [platformio.ini](../platformio.ini) |
| Libraries | RadioLib 7.7.1, only for the boards with a LoRa radio. The ESP-IDF builds also download the components listed in [src/idf_component.yml](../src/idf_component.yml): LittleFS, mDNS and MQTT |
| A C++ compiler for the PC | Only for the `native` environment (simulator and tests): `g++` on the host. On Windows: `winget install BrechtSanders.WinLibs.POSIX.UCRT`; the build finds it even when it is not in `PATH` |
| Node.js | Optional. When it is installed, the build of the web console also checks the syntax of its script. It is never needed to build |
| Git | Optional. In a Git checkout the short hash of the commit is added to the version of the firmware |

The web console is built by a Python script, [tools/web/build.py](../tools/web/build.py): there
is no JavaScript toolchain to install.

On Windows keep the project in a short path, such as `C:\LoopMaxPro`. The ESP-IDF
builds make long paths inside `.pio`, and past the limit of Windows the link of the bootloader
stops with `cannot open linker script file bootloader.ld`.

## Quick start

```
pio run -e esp32dev                build the firmware
pio run -e esp32dev -t upload      build it and write it to the board
pio device monitor -e esp32dev     open the serial console, 115200 baud
```

`esp32dev` is the default environment, so `pio run` alone builds it. Put the environment of your
board in its place: the list is in [Environments](#environments). A board environment builds the
system without modules; [Choosing the modules](#choosing-the-modules) tells how to add them.

A new device starts as a Wi-Fi access point named `LoopMax-XXXX`, where `XXXX` are the last four
hex digits of its MAC address. The default password is `loopmaxap` and the device answers at
`http://192.168.4.1`. From there it is joined to your network: see
[Web console](web-console.md). The commands of the serial port are in
[Serial console](serial-console.md).

## How the configuration is composed

A firmware is the sum of five choices. Each one is a section of the configuration, and an
environment puts them together:

```
env:<board> = [lmp] + [fw_<framework>] + [mcu_<family>] + [tier_<n>] + board options
```

| Section | File | What it sets |
|---------|------|--------------|
| `[lmp]` | [platformio.ini](../platformio.ini) | What every firmware shares: C++17, the warnings, the folders of `src/` that are compiled, the build scripts |
| `[fw_arduino]`, `[fw_idf]` | platformio.ini | The framework, and `LMP_FW_ARDUINO` or `LMP_FW_IDF` |
| `[mcu_esp32]`, `[mcu_esp32_idf]`, `[mcu_esp8266]` | platformio.ini | The platform and its version, LittleFS as filesystem, the folder of the HAL, `LMP_MCU_ESP32` or `LMP_MCU_ESP8266` |
| `[tier_min]`, `[tier_mid]`, `[tier_max]` | platformio.ini | `LMP_TIER` 1, 2 or 3: how large the tables of the system are |
| `[lora_sx126x]` | platformio.ini | Optional hardware: the library and the flags of an SX126x LoRa radio. The board adds band and pins |
| `[b_<name>]` | [boards/esp32.ini](../boards/esp32.ini) | One board of the ESP32 family, written once: PlatformIO board, partition table, tier, name, pins |
| `[env:<name>]` | `boards/*.ini` | An environment: the sections above, joined |

Three things to know before you add an environment of your own:

- With `extends = a, b` PlatformIO replaces a key that both sections have, it does not join the
  two values. So `build_flags` and `build_src_filter` are always composed by hand, with
  `${section.key}`. Every environment in `boards/` shows how.
- ESP-IDF does not read `build_src_filter`: for those builds the folders are listed in
  [src/CMakeLists.txt](../src/CMakeLists.txt). A new folder in `src/` goes in both places, and a
  new driver of the framework goes in `PRIV_REQUIRES` of that file.
- The sources of every module in `src/modules/` are always compiled. The linker keeps only the
  modules named for that firmware.

### Build scripts

PlatformIO runs the scripts of [tools/pio/](../tools/pio) at every build.

| Script | What it does |
|--------|--------------|
| `version.py` | Writes `include/lmp_version.h` from the [VERSION](../VERSION) file: version, version with the commit hash, build date |
| `cxxflags.py` | Adds `-fno-exceptions -fno-rtti` to the C++ compiler only |
| `modules.py` | Reads the modules of the firmware, checks them against their `module.json` and turns names, parameters and pins into build flags. It uses `lmp_pins.py` and `lmp_tune.py` |
| `web.py` | Builds the web console with `tools/web/build.py` and puts it where that family of boards wants it |
| `flashinfo.py` | Adds a target: `pio run -e <env> -t flashinfo` writes `lmp_flash.json` in the build folder, the list of the images an upload writes and their addresses |
| `native_env.py` | `native` only: finds the compiler of the PC on Windows |

## Environments

| Environment | Board | Framework |
|-------------|-------|-----------|
| `esp32dev` | ESP32 with 4 MB of flash, the reference board | Arduino |
| `esp32dev_debug` | The same, as a debug build: `LMP_DEBUG` and the log at level 5, trace | Arduino |
| `esp32dev_manual` | The same firmware as `esp32dev`, for boards whose serial adapter cannot start the download mode: 115200 baud, no automatic reset | Arduino |
| `esp32s3_n8` | ESP32-S3 N8R2: 8 MB of flash, 2 MB of PSRAM | Arduino |
| `heltec_v3` | Heltec WiFi LoRa 32 V3 | Arduino |
| `esp32s2_mini` | Lolin S2 mini, ESP32-S2 | Arduino |
| `esp32c3` | ESP32-C3 with a USB to UART bridge | Arduino |
| `esp32c3_usb` | ESP32-C3 boards without a bridge: console on the USB port of the chip | Arduino |
| `esp32dev_idf`, `esp32s3_n8_idf`, `heltec_v3_idf`, `esp32c3_idf`, `esp32c3_usb_idf` | The boards above | ESP-IDF |
| `esp32c6_n16` | ESP32-C6 with 16 MB of flash | ESP-IDF |
| `heltec_wifi_kit8` | Heltec WiFi Kit 8, ESP8266 | Arduino |
| `esp8266_nodemcu` | NodeMCU V3 with an ESP-12F module, ESP8266 | Arduino |
| `mega_wifi_esp8266` | Mega 2560 + WiFi, the ESP8266 half (experimental) | Arduino |
| `rgb_c3`, `ganjaleaf_c3` | [Ready-made products](#ready-made-products) on an ESP32-C3 | Arduino |
| `native` | Your PC: [simulator and tests](#running-on-a-pc) | none |

### The `_idf` variants

Every board of the ESP32 family is described once, in its `[b_<name>]` section. Two environments
build it:

- `<name>` with the Arduino core 2.0.x, which carries ESP-IDF 4.4 inside;
- `<name>_idf` with ESP-IDF 6.0.

Both compile the same sources and the same HAL, [src/hal/esp32](../src/hal/esp32), which is
written on the ESP-IDF drivers that both frameworks carry. Partition table, filesystem format and
settings storage are the same too, so a board moves from one framework to the other with an
upload of the firmware alone: its files and its settings stay.

Two boards have one framework only. `esp32s2_mini` is Arduino only: its console is the USB device
of the chip, driven through the USB stack of the Arduino core. `esp32c6_n16` is ESP-IDF only,
because the Arduino core 2.0.x does not know the C6; its name has no `_idf` suffix.

The settings of ESP-IDF come from two files: [sdkconfig.defaults](../sdkconfig.defaults), common
to every board, and `boards/sdkconfig.<environment>`, with flash size, partition table, CPU
speed, console and PSRAM of that board. The complete `sdkconfig` is generated in the build
folder. After a change to one of the two source files, build from clean:

```
pio run -e esp32dev_idf -t fullclean
pio run -e esp32dev_idf
```

## Tiers and editions

Two independent choices adapt the same system to the memory of a board.

**The tier** sizes the tables and decides which network services are part of the build. The
values are in [include/lmp_config.h](../include/lmp_config.h).

| | MIN, `LMP_TIER=1` | MID, `LMP_TIER=2` | MAX, `LMP_TIER=3` |
|---|---|---|---|
| Used by | ESP8266 boards | ESP32, S2, C3, C6, Heltec V3 | ESP32-S3 N8R2, PC |
| Modules in one firmware | 4 | 8 | 16 |
| Commands | 48 | 96 | 160 |
| Sessions of the console | 2 | 4 | 8 |
| Longest answer of the API | 3 KB | 8 KB | 16 KB |
| Log entries kept on the device | 32 | 100 | 200 |
| Longest log message, in characters | 96 | 128 | 160 |
| Wi-Fi networks kept after a scan | 8 | 16 | 16 |
| Settings file of a module | 2 KB | 4 KB | 4 KB |
| HTTPS client, MQTT, firmware update over the network, position, cloud bridge | out | in | in |

At tier MIN a client for plain HTTP comes in only when a module of the firmware asks for it.

A second value, `LMP_MODULE_MEM`, tells the modules how much they can keep in their lists and
buffers: 1 small, 2 medium, 3 large. It follows the tier unless the board says otherwise; the
ESP32-C3 boards set it to 1.

**The edition** is normal or light. The light edition is the same system and the same console
with less memory spent. `LMP_LIGHT` is 1 by itself on the ESP8266 family and 0 elsewhere; a
board asks for it with `-DLMP_LIGHT=1`, as the Lolin S2 mini does.

| | Normal | Light |
|---|---|---|
| Log entries kept on the device | by tier | 16; the serial port has them all |
| Help texts of the commands | in the firmware | left out: the console shows name and arguments |
| Browsers following the live events | one for each session | one |
| I2C bus | always | only when a module of the firmware requires it |
| Name `<host>.local` on the network (mDNS) | yes | no: the device is reached by its address |
| Network probe: pings, ports, mDNS and SSDP searches | yes, from tier MID | no |

The device says which edition it runs: `/api/state` reports `"edition": "full"` for the normal
one and `"edition": "light"`. The hardware each edition needs, and what it means for a module, is
in [Boards](boards.md#hardware-requirements).

## Choosing the modules

The environments of the boards name no module: they build the system with its console and nothing
else. The modules of a firmware are named in the option `custom_lmp_modules` of an environment.

```ini
[env:my_relays]
extends = env:esp32dev
custom_lmp_modules = zappy2
```

Any `.ini` file in `boards/` is read, so an environment of your own can live in a file of your
own. The syntax of the option:

```
custom_lmp_modules = <module>:<parameter>=<value>;<parameter>=<value>  <module>  ...
```

- Modules are separated by spaces, commas or new lines.
- The parameters of one module follow a colon and are separated by semicolons.
- The values of a list are separated by a vertical bar: `pins=2|3|4`.
- At most 16 modules, and no more than the tier of the board hosts.

[tools/pio/modules.py](../tools/pio/modules.py) turns the line into build flags. For example

```ini
custom_lmp_modules = ganjaleaf:leaf=2|3|4;ring=10;leds=12
```

becomes

```
-DLMP_MODULE_1=ganjaleaf
-DLMP_MOD_GANJALEAF=1
-DLMP_GANJALEAF_LEAF={2,3,4}
-DLMP_GANJALEAF_LEDS=12
-DLMP_GANJALEAF_RING=10
```

The build stops with a clear message for a module that does not exist, a module named twice, a
parameter the module does not have, a value of the wrong type (the default in `params` of
`module.json` gives the type), and two modules that cannot be on the same device (`notWith` in
`module.json`: `espradio` and `expnet`, for one).

**Pins.** A pin parameter left at its default, -1, takes the pin the module knows for that board:
`boardPins` in its `module.json`, first by board name (`board:heltec_v3`), then by chip
(`esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c6`, `esp8266`). So `zappy2` alone, on an ESP32,
means pins 22 and 23. When a pin the module needs has no value for the board, the build says
which one and how to give it.

**What a module turns on.** The `requires` list of `module.json` brings into the build what the
small boards leave out: `bus` the I2C bus of the light edition, `httpclient` the plain HTTP
client of tier MIN. With `tune`, a module can also ask for a different value of a setting of
`lmp_config.h` on one chip or one board, only in the firmwares that carry it. The manifest is
described in [Modules and SDK](modules-sdk.md).

### Without touching the files

The same text in the environment variable `LMP_MODULES` takes the place of the option. It is the
quick way to try a module on a board:

```
# Linux, macOS
LMP_MODULES="zappy2:pins=18|19" pio run -e esp32dev -t upload

# Windows PowerShell
$env:LMP_MODULES = "zappy2:pins=18|19"
pio run -e esp32dev -t upload
Remove-Item Env:LMP_MODULES
```

### The free modules

| Module | Name in the build | Parameters |
|--------|-------------------|------------|
| [Zappy 2](../src/modules/zappy2/README.md) | `zappy2` | `pins`: the two lines |
| [RGB Controller](../src/modules/rgb/README.md) | `rgb` | `pins`: red, green, blue |
| [GanjaLeaf](../src/modules/ganjaleaf/README.md) | `ganjaleaf` | `leaf`: red, green, blue; `ring`: the pin of the LED ring; `leds`: how many LEDs, 12 |
| [MeteoWifi](../src/modules/meteowifi/README.md) | `meteowifi` | `sda`, `scl`: the I2C pins of the display; `sda2`, `scl2`: a second place to look for it; `address`: 60, which is 0x3C |
| [ExpNet](../src/modules/expnet/README.md) | `expnet` | none |
| [EspRadio](../src/modules/espradio/README.md) | `espradio` | none |

Which chips each module runs on is in [Boards](boards.md#the-free-modules-and-their-boards).

## Building and uploading

The standard PlatformIO commands do everything.

```
pio run -e <env>                   build
pio run -e <env> -t upload         build and write the firmware
pio run -e <env> -t uploadfs       build and write the filesystem image
pio device monitor -e <env>        serial console; crashes are decoded
pio run -e <env> -t clean          remove the build of the environment
```

Add `--upload-port <port>` when more than one board is connected. Where the web console travels
depends on the family:

| Family | Where the console is | What to upload |
|--------|----------------------|----------------|
| ESP32 | Inside the firmware. It is built from [web/](../web) at every build, with the pages of the modules of that firmware only | `-t upload`, nothing else. The filesystem image is empty: `uploadfs` is not needed, and it would erase the files the board keeps there |
| ESP8266 | In the filesystem image, built into `data/web/` | `-t upload`, then `-t uploadfs`. The image replaces every file of the board, the settings of the system too: after it the device starts as new |

On the ESP32 family the first upload is the only one that needs the cable: later firmwares can be
sent from the console, see [Versions and updates](updates.md). An ESP8266 is always updated
from USB.

The console can also be built by hand, for example to see its size:

```
python tools/web/build.py                        writes data/web/
python tools/web/build.py --modules meteowifi    only the pages of these modules
```

The script prints the size of every file. It stops when the console, with the pages of its two
largest modules, grows over 150 KB compressed.

A few boards need attention at upload: a button to hold, switches to set, a serial port that
changes. Each case is in [Boards](boards.md).

## Partitions

**ESP32 family.** The tables are in [partitions/](../partitions). All of them have two
application slots: an update is written to the slot that is not running, and the board goes back
to the firmware of before when the new one does not hold.

| Table | Flash | Each firmware slot | Filesystem | Core dump | Used by |
|-------|-------|--------------------|------------|-----------|---------|
| `esp32_4mb.csv` | 4 MB | 1.5 MB | 896 KB | 64 KB | `esp32dev`, `esp32c3`, `esp32c3_usb`, `esp32s2_mini` |
| `esp32_8mb.csv` | 8 MB | 3 MB | 1.9 MB | 64 KB | `esp32s3_n8`, `heltec_v3` |
| `esp32_16mb.csv` | 16 MB | 5.5 MB | 4.7 MB | 256 KB | `esp32c6_n16` |

The first part is the same in the three tables: `nvs`, 20 KB at 0x9000, holds the settings;
`otadata`, 8 KB at 0xE000, remembers which slot runs; `app0` starts at 0x10000. In the 4 MB
table `app1` is at 0x190000, the filesystem at 0x310000 and the core dump area at 0x3F0000. The
filesystem partition is named `spiffs`, the name the tools expect, and holds LittleFS.

**ESP8266.** There is no partition table: the linker script decides. The three environments use
`eagle.flash.4m1m.ld`, which gives about 1 MB to the firmware and 1 MB to a LittleFS filesystem
where the console, the settings and the files of the modules live.

## Running on a PC

The `native` environment compiles the whole system for your computer, on a HAL that simulates the
board ([src/hal/native](../src/hal/native)): Wi-Fi networks around, an I2C device, a LoRa radio,
the time server. It runs at tier MAX with `demo`, a module that exists for the simulator and the
tests ([test/modules/demo](../test/modules/demo)) and is not part of the product.

### The simulator

```
pio run -e native
python tools/web/build.py --sim
.pio/build/native/program
```

On Windows the last line is `.pio\build\native\program.exe`. Then open `http://localhost:8088`:
it is the same console a board serves. Things to know:

- Start it from the root of the project. Files and settings of the simulator are kept in
  `.lmp_native/`, and `build.py --sim` puts the console there.
- It listens on this computer only, never on the network.
- Another port: set the environment variable `LMP_SIM_PORT` before starting it.
- What you type in the terminal reaches the command line of the system, as on the serial port
  of a board.
- A number as argument is a time in milliseconds: the program stops by itself after it.

### The tests

```
pio test -e native                    all of them
pio test -e native -f test_kernel     one folder of test/
```

The tests are written with Unity and live in [test/](../test), one folder for each part of the
system. They keep their files in `.lmp_native/test`, wiped at every start, so they never touch
the settings of the simulator.

## Ready-made products

[boards/products.ini](../boards/products.ini) holds environments that are a board and its
modules together: one command gives a finished device.

| Environment | What it is | Board |
|-------------|------------|-------|
| `rgb_c3` | [RGB Controller](../src/modules/rgb/README.md): an analog RGB LED strip with colors, effects and schedules | ESP32-C3 SuperMini |
| `ganjaleaf_c3` | [GanjaLeaf](../src/modules/ganjaleaf/README.md): a lamp in the shape of a leaf, an RGB strip in the leaf and a ring of twelve WS2812 in the base | ESP32-C3 SuperMini |

```
pio run -e rgb_c3 -t upload
```

Both extend `esp32c3_usb` and give the modules the pins of their circuit:

```ini
custom_lmp_modules = rgb:pins=2|3|4
custom_lmp_modules = ganjaleaf:leaf=2|3|4;ring=10;leds=12
```

GPIO4, the reset button of LoopMaxPro on the C3 environments, is the blue channel here. So the
two products move the reset to GPIO9, the BOOT button every SuperMini has. They are also the
model for a product of your own: extend the environment of the board, write its flags again when
a pin of the board changes, name the modules.

## Build options

Every value of [include/lmp_config.h](../include/lmp_config.h) can be set from an environment
with `-DLMP_...=value`. The ones used most:

| Option | Default | Meaning |
|--------|---------|---------|
| `LMP_BOARD` | `"unknown"` | Name of the board, as the firmware reports it. The build uses it to find the pins of the modules |
| `LMP_RESET_PIN` | -1 | Pin of the reset button, active low. -1: the board has none |
| `LMP_RESET_HOLD_MS` | 10000 | How long the button is held for a factory reset |
| `LMP_TIER`, `LMP_LIGHT`, `LMP_MODULE_MEM` | by board | See [Tiers and editions](#tiers-and-editions) |
| `LMP_LOG_LEVEL` | 3; 4 with `LMP_DEBUG` | 1 error, 2 warn, 3 info, 4 debug, 5 trace. The calls above the level are compiled out |
| `LMP_SERIAL_BAUD` | 115200 | Speed of the serial console |
| `LMP_HTTP_PORT` | 80; 8088 on the PC | Port of the web server |
| `LMP_WIFI_MAX_TX_QDBM` | 0 | Highest transmit power of the Wi-Fi, in quarters of dBm. 0 leaves the one of the chip |
| `LMP_I2C0_SDA`, `LMP_I2C0_SCL`, `LMP_I2C0_HZ` | -1, -1, 100000 | An I2C bus of the board, opened at start |
| `LMP_FEATURE_NET`, `_MDNS`, `_HTTPCLIENT`, `_MQTT`, `_OTA`, `_GEO`, `_CLOUD`, `_PROBE`, `_BUS`, `_CLI`, `_LORA` | by tier and edition | 1 puts a service in the build, 0 leaves it out |

To add a flag, extend an environment and keep its flags:

```ini
[env:esp32dev_quiet]
extends = env:esp32dev
build_flags =
  ${env:esp32dev.build_flags}
  -DLMP_LOG_LEVEL=2
```

To change a value the board already sets, such as `LMP_RESET_PIN`, do not add a second `-D`:
compose the flags again from the common sections, as `boards/products.ini` does.

### Generated folders

None of these is versioned.

| Path | Content |
|------|---------|
| `.pio/` | The builds of PlatformIO and the libraries it downloaded |
| `.pio/build/<env>/lmp_web/`, `lmp_gen/` | ESP32 family: the console of that firmware and its table in C |
| `data/web/` | The compressed console, for the filesystem image of the ESP8266 |
| `include/lmp_version.h` | Written by `version.py` |
| `managed_components/` | The components ESP-IDF downloaded |
| `.lmp_native/` | Files and settings of the simulator; `.lmp_native/test` those of the tests |

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="boards.md">Boards →</a>
</p>
