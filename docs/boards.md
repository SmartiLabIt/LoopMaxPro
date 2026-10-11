<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/boards.svg" alt="Boards" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="build.md">← Build and flash</a> &nbsp;·&nbsp;
<a href="architecture.md">Architecture →</a>
</p>

---

This page is the reference of the boards LoopMaxPro runs on: chip, memory, edition, the name of
the build environment, what is on the board, and the pins to know before you wire anything. It
also gives the hardware each edition needs and a guide to choose a board for a module. The
commands to build and upload are in [Build and flash](build.md).

- [Supported boards](#supported-boards)
- [What each board has](#what-each-board-has)
- [What the HAL gives on each chip](#what-the-hal-gives-on-each-chip)
- [Board by board](#board-by-board)
- [Hardware requirements](#hardware-requirements)
- [Choosing a board for a module](#choosing-a-board-for-a-module)
- [Boards that are not supported](#boards-that-are-not-supported)
- [Adding a board](#adding-a-board)

## Supported boards

Every board has 2.4 GHz Wi-Fi and at least 4 MB of flash. The ESP32 family runs the normal
edition, with one exception in the table; the ESP8266 runs the light one. The tier sizes the
tables of the system. Both are explained in [Build and flash](build.md#tiers-and-editions).

| Board | Chip | Flash | RAM of the chip | PSRAM | Edition | Tier | Environments |
|-------|------|-------|-----------------|-------|---------|------|--------------|
| ESP32 development board with a WROOM module | ESP32, 2 cores, 240 MHz | 4 MB | 520 KB | no | normal | MID | `esp32dev`, `esp32dev_idf` |
| ESP32-S3 DevKitC-1 N8R2 | ESP32-S3, 2 cores, 240 MHz | 8 MB | 512 KB | 2 MB | normal | MAX | `esp32s3_n8`, `esp32s3_n8_idf` |
| Heltec WiFi LoRa 32 V3 | ESP32-S3, 2 cores, 240 MHz | 8 MB | 512 KB | no | normal | MID | `heltec_v3`, `heltec_v3_idf` |
| Lolin S2 mini | ESP32-S2, 1 core, 240 MHz | 4 MB | 320 KB | 2 MB | light | MID | `esp32s2_mini` |
| ESP32-C3 with a USB to UART bridge | ESP32-C3, 1 core, 160 MHz | 4 MB | 400 KB | no | normal | MID | `esp32c3`, `esp32c3_idf` |
| ESP32-C3 without a bridge, such as the SuperMini | ESP32-C3, 1 core, 160 MHz | 4 MB | 400 KB | no | normal | MID | `esp32c3_usb`, `esp32c3_usb_idf` |
| ESP32-C6 DevKitC-1 N16 | ESP32-C6, 1 core, 160 MHz | 16 MB | 512 KB | no | normal | MID | `esp32c6_n16` |
| Heltec WiFi Kit 8 | ESP8266, 1 core, 80 MHz | 4 MB | 80 KB | no | light | MIN | `heltec_wifi_kit8` |
| NodeMCU V3 with an ESP-12F module | ESP8266, 1 core, 80 MHz | 4 MB | 80 KB | no | light | MIN | `esp8266_nodemcu` |
| Mega 2560 + WiFi, the ESP8266 half (experimental) | ESP8266, 1 core, 80 MHz | 4 MB | 80 KB | no | light | MIN | `mega_wifi_esp8266` |

The environments with `_idf` build the same board with ESP-IDF instead of the Arduino core.
`esp32s2_mini` exists with Arduino only and `esp32c6_n16` with ESP-IDF only. `esp32dev` also
has a debug build, `esp32dev_debug`, and `esp32dev_manual` for boards that must be put in
download mode by hand.

## What each board has

| Environment | PlatformIO board | Serial console | On the board | Reset button of LoopMaxPro |
|-------------|------------------|----------------|--------------|----------------------------|
| `esp32dev` | `esp32dev` | UART0, through the USB bridge | nothing more | GPIO4: wire a button |
| `esp32s3_n8` | `esp32-s3-devkitc-1` | USB port of the chip | PSRAM | GPIO4: wire a button |
| `heltec_v3` | `heltec_wifi_lora_32_V3` | UART0, through the USB bridge | SX1262 LoRa radio, 868 MHz; OLED display, 0.96 inch; battery connector with charger | GPIO0: the PRG button |
| `esp32s2_mini` | `lolin_s2_mini` | USB port of the chip | PSRAM | GPIO0: the button 0 |
| `esp32c3` | `esp32-c3-devkitm-1` | UART0, through the USB bridge | nothing more | GPIO4: wire a button |
| `esp32c3_usb` | `esp32-c3-devkitm-1` | USB port of the chip | nothing more | GPIO4: wire a button |
| `esp32c6_n16` | `esp32-c6-devkitc-1` | UART0, through the USB bridge | nothing more | GPIO4: wire a button |
| `heltec_wifi_kit8` | `heltec_wifi_kit_8` | UART0, through the USB bridge | OLED display, 0.91 inch, 128 x 32; battery connector with charger | GPIO0: the PRG button |
| `esp8266_nodemcu` | `nodemcuv2` | UART0, through the USB bridge | OLED display, 0.96 inch, on the version that has one | GPIO0: the FLASH button |
| `mega_wifi_esp8266` | `esp12e` | UART0, through the USB bridge, when the switches say so | an ATmega2560 that LoopMaxPro does not use | none |

Three notes that hold for every board:

- **Reset button.** The pin is `LMP_RESET_PIN`. The button connects it to ground; the pull-up of
  the pin is on. Held for 10 seconds it brings the device back to the factory settings. The pin
  belongs to the reset service: a module cannot take it, unless the environment moves the button
  to another pin.
- **Displays** are not driven by the system. They are left to the modules: MeteoWifi, for one,
  draws on an SSD1306 OLED over I2C.
- **The LoRa radio** belongs to the LoRa service, with its pins. See [LoRa](lora.md).

## What the HAL gives on each chip

A module does not talk to the chip: it asks the system for pins, PWM, buses. This is what the
hardware layer offers on each family, as the sources in [src/hal](../src/hal) say.

| | ESP32 | ESP32-S2 | ESP32-S3 | ESP32-C3 | ESP32-C6 | ESP8266 |
|---|---|---|---|---|---|---|
| Tasks | yes, 2 cores | yes, 1 core | yes, 2 cores | yes, 1 core | yes, 1 core | no: one context |
| PWM outputs at the same time | 8 | 8 | 8 | 6 | 6 | every output pin |
| Different PWM frequencies at the same time | 4 | 4 | 4 | 4 | 4 | 1, for all the pins |
| Analog inputs | GPIO32 to 39 | GPIO1 to 10 | GPIO1 to 10 | GPIO0 to 4 | GPIO0 to 6 | A0 only, 0 to 1 V |
| I2C buses | 2 | 2 | 2 | 1 | 1 | 1, when a module requires it |
| SPI bus | yes | yes | yes | yes | yes | no |
| Lines of addressable LEDs (WS2812) | 2 | 2 | 2 | 2 | 2 | none |
| Settings are kept in | NVS | NVS | NVS | NVS | NVS | files on LittleFS |
| Pins that can only be inputs | GPIO34 to 39 | GPIO46 | none | none | none | none |

Only the first analog converter of the chip is used: the second one, where a chip has it, is
taken by the radio while Wi-Fi is on. The chips have radios and peripherals the HAL has no
interface for: Bluetooth on every ESP32 except the S2, 802.15.4 on the C6. A module that wants
them calls the framework directly, and then it builds for that framework only.

## Board by board

In the pin tables, "read at power-on" marks the strapping pins of the chip: they choose how the
chip starts, so an external circuit must not force them while the board powers up. Do not put a
relay on them, nor on the pins of the console.

### ESP32 with 4 MB, `esp32dev`

The reference board: any development board with an ESP32-WROOM module and 4 MB of flash. The
firmware has 1.5 MB for each copy and 896 KB for files.

| Pin | Use |
|-----|-----|
| GPIO0 | BOOT button. Read at power-on: low starts the download mode |
| GPIO2, 5, 15 | Read at power-on. GPIO2 is the LED of many boards |
| GPIO12 | Read at power-on: when high the chip picks the wrong flash voltage and the board does not start |
| GPIO1, 3 | UART0: the console |
| GPIO6 to 11 | The flash: not usable |
| GPIO16, 17 | Free on WROOM modules; taken by the PSRAM on WROVER modules |
| GPIO34 to 39 | Inputs only, without internal pull-up or pull-down |
| GPIO32 to 39 | Analog inputs |
| GPIO4 | Reset button of LoopMaxPro |

Some boards of this kind restart from their USB adapter but do not enter the download mode by
themselves. For those, use `esp32dev_manual`: hold BOOT, restart the board, then upload. Release
BOOT before the board restarts at the end, or it waits for another upload and looks mute.

### ESP32-S3 N8R2, `esp32s3_n8`

The board with the most room: two cores, 8 MB of flash, 2 MB of PSRAM, many pins. It is the only
one at tier MAX. The large buffers of the system go to the PSRAM, and the internal RAM stays for
the radio and the tasks. The console is on the USB port of the chip: on boards with two USB
connectors, use the one wired to the chip, not the one of the UART bridge. The LED of the board
is an addressable RGB LED, not a plain LED on a pin.

| Pin | Use |
|-----|-----|
| GPIO0 | BOOT button. Read at power-on |
| GPIO3, 45, 46 | Read at power-on |
| GPIO19, 20 | USB of the chip: the console |
| GPIO26 to 32 | Flash and PSRAM: not usable |
| GPIO43, 44 | UART0: free, because the console is on USB |
| GPIO1 to 10 | Analog inputs |
| GPIO4 | Reset button of LoopMaxPro |

### Heltec WiFi LoRa 32 V3, `heltec_v3`

An ESP32-S3 with 8 MB of flash and no PSRAM, an SX1262 LoRa radio, an OLED display and a
connector for a battery. The environment is for the 868 MHz version of the board: 1.8 V
oscillator, antenna switch driven by the radio chip.

| Pin | Use |
|-----|-----|
| GPIO0 | PRG button, and reset button of LoopMaxPro. Read at power-on |
| GPIO8, 9, 10, 11 | Radio: chip select, clock, data out, data in |
| GPIO12, 13, 14 | Radio: reset, busy, interrupt |
| GPIO17, 18, 21 | Display: SDA, SCL, reset |
| GPIO36 | Power of the display and of the Vext pin, on when low |
| GPIO35 | White LED |
| GPIO1, 37 | Battery voltage and the switch of its measure |
| GPIO19, 20 | USB of the chip, on the side connectors |
| GPIO43, 44 | UART0: the console |
| GPIO3, 45, 46 | Read at power-on |

Radio and display take eleven pins: few are left. The PRG button has two jobs: held for 10
seconds while the system runs it is the factory reset, pressed at power-on it starts the
download mode. A module that uses the display first turns its power on with GPIO36.

### Lolin S2 mini, `esp32s2_mini`

An ESP32-S2 with 4 MB of flash and 2 MB of PSRAM inside the chip. One core, no Bluetooth.
Arduino only.

| Pin | Use |
|-----|-----|
| GPIO0 | Button 0, and reset button of LoopMaxPro. Read at power-on |
| GPIO45, 46 | Read at power-on. GPIO46 is an input only |
| GPIO19, 20 | USB of the chip: the console |
| GPIO26 to 32 | Flash and PSRAM: not usable |
| GPIO15 | LED of the board |
| GPIO1 to 10 | Analog inputs |
| GPIO43, 44 | UART0: free, because the console is on USB |

What is particular to this board:

- **Light edition.** The internal RAM of the S2 is small for two encrypted connections at the
  same time. The environment asks for the light edition, and the memory of the encrypted
  connections goes to the PSRAM. The tier stays MID, so HTTPS, MQTT and the updates over the
  network are there.
- **Wi-Fi at 8.5 dBm at most**, `LMP_WIFI_MAX_TX_QDBM=34`. At full power the peaks of the
  radio, with the CPU busy on a secure connection, are more than the regulator of the board
  holds: the chip stops until a watchdog restarts it.
- **The serial port is the chip itself.** During an upload the board leaves and comes back as
  another serial port, and PlatformIO follows it: do not fix the upload port. A program on the
  PC must raise the DTR line to receive anything; terminal programs do.

### ESP32-C3, `esp32c3` and `esp32c3_usb`

Small and cheap, one core, 4 MB of flash. There are two environments because there are two kinds
of board:

- `esp32c3`: the USB connector goes to a USB to UART bridge. Console on UART0.
- `esp32c3_usb`: no bridge, the USB connector goes to the chip. Console on the USB port of the
  chip. The SuperMini boards are of this kind.

A firmware of one kind leaves a board of the other kind without console. Both set
`LMP_MODULE_MEM=1`, small: a C3 has about 100 KB free with the system running, and the modules
size their lists and buffers on that.

| Pin | Use |
|-----|-----|
| GPIO9 | BOOT button. Read at power-on |
| GPIO2, 8 | Read at power-on. GPIO8 is often the LED of the board |
| GPIO18, 19 | USB of the chip: the console of `esp32c3_usb` |
| GPIO20, 21 | UART0: the console of `esp32c3` |
| GPIO11 to 17 | The flash: not usable |
| GPIO0 to 4 | Analog inputs |
| GPIO4 | Reset button of LoopMaxPro |

The mini boards usually bring out 13 pins: GPIO0 to 10, 20 and 21. On those, GPIO9 with its BOOT
button is a handy reset button: the [ready-made products](build.md#ready-made-products) use it.

### ESP32-C6 N16, `esp32c6_n16`

One core, Wi-Fi 6, 16 MB of flash: 5.5 MB for each copy of the firmware and 4.7 MB for files, the
most room of all. ESP-IDF only, because the Arduino core 2.0.x does not know this chip. The
environment sets the flash to 16 MB; a C6 board with less flash needs an environment of its own.

| Pin | Use |
|-----|-----|
| GPIO9 | BOOT button. Read at power-on |
| GPIO8, 15 | Read at power-on |
| GPIO12, 13 | USB of the chip |
| GPIO16, 17 | UART0: the console |
| GPIO24 to 30 | The flash: not usable |
| GPIO0 to 6 | Analog inputs |
| GPIO4 | Reset button of LoopMaxPro |

### Heltec WiFi Kit 8, `heltec_wifi_kit8`

An ESP8266 with 4 MB of flash, an OLED display of 128 x 32 points and a connector for a battery.

| Pin | Use |
|-----|-----|
| GPIO0 | PRG button, and reset button of LoopMaxPro. Read at power-on |
| GPIO1, 3 | UART0: the console |
| GPIO2, 15 | Read at power-on: GPIO2 must be high, GPIO15 low |
| GPIO4, 5 | Display: SDA, SCL |
| GPIO16 | Reset of the display. This pin has no interrupt |
| GPIO6 to 11 | The flash: not usable |
| GPIO12, 13, 14 | Free |
| A0 | The only analog input, 0 to 1 V |

### NodeMCU V3, `esp8266_nodemcu`

An ESP-12F module with 4 MB of flash and a CH340 adapter, all the pins on the connectors. Also
for the version with an OLED display on the board.

| Pin | Label | Use |
|-----|-------|-----|
| GPIO0 | D3 | FLASH button, and reset button of LoopMaxPro. Read at power-on |
| GPIO1, 3 | TX, RX | UART0: the console |
| GPIO2 | D4 | Read at power-on, must be high. LED of the module, lit when low |
| GPIO15 | D8 | Read at power-on, must be low |
| GPIO4, 5 | D2, D1 | Display, on one version of the board: SDA, SCL |
| GPIO14, 12 | D5, D6 | Display, on the other version: SDA, SCL |
| GPIO13 | D7 | Free |
| GPIO16 | D0 | No interrupt. A second LED on many boards |
| A0 | A0 | The only analog input |

- **The boards with a display come in two versions that look the same**, with the display on
  GPIO4 and 5 or on GPIO14 and 12. MeteoWifi looks in both places. On the second version GPIO12
  and GPIO14 are not free: the pins that Zappy 2 and RGB Controller take by default on an
  ESP8266 include GPIO12, so give them other pins there.
- **The analog input has a divider on the board**: A0 accepts 0 to 3.3 V, and the HAL reports
  the voltage at the pin of the chip, 0 to 1 V. A module scales the reading.

### Mega 2560 + WiFi, `mega_wifi_esp8266`

A board in the shape of an Arduino Mega with two processors: an ATmega2560 and an ESP8266 with
4 MB of flash, one CH340 adapter, and switches that choose who talks to whom. LoopMaxPro runs on
the ESP8266; the ATmega2560 is left alone.

> **Experimental.** The environment builds and the firmware starts with its console, but joining
> a Wi-Fi network is not confirmed on this board. Use a NodeMCU or a Heltec WiFi Kit 8 for a
> device you count on.

- **To load the firmware**: switches 5, 6 and 7 on, the others off. **To run it**: switch 7 off,
  then a reset. Switch 7 ties GPIO0 to ground, so with 7 left on the board waits for an upload
  and looks mute.
- **No reset button**: GPIO0 is on a switch. The factory reset is asked with the command
  `sys.system factoryReset`: see [Serial console](serial-console.md).
- The connectors of the board belong to the ATmega2560. The few pins of the ESP8266 are on
  solder pads.

### All the ESP8266 boards

- 80 KB of RAM and no tasks: the system runs in one context. An operation that takes long, such
  as a write to the flash, holds everything else while it lasts.
- One PWM frequency for all the pins, one analog input, no SPI bus, no addressable LEDs.
- At power-on the chip writes a few lines at 74880 baud: on the console they look like garbage
  before the first line of the firmware.
- The web console travels in the filesystem image and the firmware is updated from USB only:
  see [Build and flash](build.md#building-and-uploading).

## Hardware requirements

| | Normal edition | Light edition |
|---|---|---|
| Chips | ESP32, ESP32-S3, ESP32-C3, ESP32-C6 | ESP8266, on ESP-12E, ESP-12F and similar modules |
| Flash | 4 MB at least; 8 MB or more leave room to grow | 4 MB |
| How the flash is used | Two copies of the firmware and a filesystem: 1.5 MB + 1.5 MB + 896 KB on 4 MB | About 1 MB for the firmware, 1 MB for the filesystem |
| RAM | The internal RAM of the chip, 400 to 520 KB | 80 KB |
| PSRAM | Optional. Used when it is there, for the large buffers | none on an ESP8266 |
| Wi-Fi | 2.4 GHz | 2.4 GHz |
| Framework | Arduino or ESP-IDF | Arduino |
| Reset button | Optional, on a pin of your choice | Optional |

The light edition on an ESP8266 is a complete local device: pins, schedules, console, network.
It leaves out what needs an encrypted connection or more memory than the chip has.

| | Normal, ESP32 family | Light, ESP8266 |
|---|:---:|:---:|
| Kernel, services, commands, serial command line | yes | yes |
| Saved settings and factory reset | yes | yes |
| Wi-Fi network, access point with captive portal | yes | yes |
| Web console | yes, inside the firmware | yes, in the filesystem |
| Time from the network | yes | yes |
| Name `<host>.local` | yes | no: use the address |
| Live events in the browser | every open session | one browser |
| Sessions | 4; 8 at tier MAX | 2 |
| Modules in a firmware | 8; 16 at tier MAX | 4 |
| Log kept on the device | 100 entries; 200 at tier MAX | 16 entries |
| Help texts of the commands | yes | no |
| Requests to web services | HTTP and HTTPS | plain HTTP, when a module requires it |
| MQTT and Home Assistant | yes | no |
| Firmware update over the network | yes | no: from USB |
| Position of the device | yes | no |
| [Cloud bridge](cloud.md) | yes | no |
| Network probe | yes | no |
| I2C bus | yes | when a module requires it |
| SPI bus, addressable LEDs | yes | no |
| LoRa | on the boards with the radio | no |

The Lolin S2 mini is between the two: light edition at tier MID. It keeps the limits of tier MID
and its network services: HTTPS, MQTT, updates over the network. It gives up what the light
edition gives up: the `.local` name, the help texts, the network probe, all but 16 log entries
and all but one browser on the live events.

## Choosing a board for a module

| If the module needs | Take |
|---------------------|------|
| A few relays or sensors, nothing more | Any board: the price decides. An ESP8266 is enough |
| HTTPS, MQTT, Home Assistant or updates over the network | The ESP32 family |
| More than 6 PWM outputs | ESP32, ESP32-S2 or ESP32-S3 |
| PWM at more than one frequency | The ESP32 family |
| Addressable LEDs | The ESP32 family |
| More than one analog input | The ESP32 family: 5 on the C3, 7 on the C6, 8 on the ESP32, 10 on the S2 and S3 |
| Two I2C buses | ESP32, ESP32-S2 or ESP32-S3 |
| A device on SPI | The ESP32 family |
| Many free pins | ESP32-S3 or ESP32. Not the Heltec V3, not a C3 mini |
| Much memory: large buffers, many modules together | ESP32-S3 with PSRAM |
| Heavy work beside the network | A chip with two cores: ESP32 or ESP32-S3 |
| A firmware over 1.5 MB, or files over 896 KB | A board with 8 or 16 MB: ESP32-S3, Heltec V3, ESP32-C6 |
| LoRa | Heltec V3, or another board of the ESP32 family with an SX126x module on SPI |
| A display on the board | Heltec V3, Heltec WiFi Kit 8, NodeMCU with display |
| A battery with its charger | Heltec V3 or Heltec WiFi Kit 8 |
| Settings that change often | Not an ESP8266: each save holds the system while the flash is written |
| The smallest size | An ESP32-C3 mini |
| The lowest cost | An ESP8266 board, then an ESP32-C3 mini |

Two rules for whoever writes a module:

- A module written with the SDK and the HAL only runs on every board. One that calls Arduino or
  ESP-IDF directly runs only where that framework is.
- Before you fix the pins of a product, read the pin table of the board. Pins read at power-on,
  pins of the console and input-only pins are not for relays. A module meant for several boards
  uses pins that are free on all of them, or gives each board its own in `boardPins`.

### The free modules and their boards

What each `module.json` in [src/modules](../src/modules) declares. The pins are the ones the
build gives when the environment names none.

| Module | Runs on | Default pins |
|--------|---------|--------------|
| [Zappy 2](../src/modules/zappy2/README.md) | ESP32 family, ESP8266 | ESP32: 22, 23. S2, S3, C3, C6: 5, 6. ESP8266: 12, 13 |
| [RGB Controller](../src/modules/rgb/README.md) | ESP32 family, ESP8266 | ESP32: 25, 26, 27. S2, S3, C3, C6: 5, 6, 7. ESP8266: 12, 13, 14 |
| [GanjaLeaf](../src/modules/ganjaleaf/README.md) | ESP32 family | Leaf as RGB Controller. Ring: 23 on the ESP32, 10 on the others, 4 on the Heltec V3 |
| [MeteoWifi](../src/modules/meteowifi/README.md) | ESP32 family, ESP8266 | Display on an ESP8266: SDA 4, SCL 5; on the NodeMCU also SDA 14, SCL 12. On the ESP32 family the pins of the display are given in the build |
| [ExpNet](../src/modules/expnet/README.md) | ESP32 family, normal edition | no pins |
| [EspRadio](../src/modules/espradio/README.md) | ESP32 family | no pins |

## Boards that are not supported

| Board | Why |
|-------|-----|
| ESP32-C2 | The PlatformIO platform the project uses, `espressif32` 7.0.1, does not support the chip |
| Any ESP board with less than 4 MB of flash | No flash layout for it: the system, its second copy and the filesystem do not fit |
| Heltec CubeCell, ASR650x | 16 KB of RAM and 128 KB of flash, no Wi-Fi |
| ATmega2560: Arduino Mega, and the AVR half of the Mega 2560 + WiFi | 8 KB of RAM, and a compiler without the C++ standard library |
| Arduino MKR WiFi 1010, SAMD21 | 32 KB of RAM and 256 KB of flash |

## Adding a board

A board of a chip that is already there needs configuration only, no code.

1. **ESP32 family.** Write the board once in [boards/esp32.ini](../boards/esp32.ini), in a
   section `[b_<name>]`: the PlatformIO `board`, the partition table for its flash, and in
   `flags` the tier, `-DLMP_BOARD=\"<name>\"` and `-DLMP_RESET_PIN=<pin>`. Then add
   `[env:<name>]` next to the others, and `[env:<name>_idf]` in
   [boards/esp32_idf.ini](../boards/esp32_idf.ini) with a file `boards/sdkconfig.<name>_idf`
   for flash size, partition table and CPU speed. A chip the Arduino core does not know gets
   the ESP-IDF environment only.
2. **Console.** Nothing to do when the USB connector goes to a UART bridge. When it goes to the
   chip: `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` with Arduino,
   `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` with ESP-IDF.
3. **PSRAM.** `-DBOARD_HAS_PSRAM` with Arduino, when the definition of the board does not turn
   it on; `CONFIG_SPIRAM=y` with ESP-IDF.
4. **LoRa radio.** Take library and flags from `[lora_sx126x]` and add band, pins, oscillator
   voltage and antenna switch, as `[b_heltec_v3]` does. See [LoRa](lora.md).
5. **ESP8266.** A new environment in [boards/esp8266.ini](../boards/esp8266.ini) that extends
   `esp8266_common`, with `eagle.flash.4m1m.ld`, the flags of tier MIN, the name of the board
   and its reset pin.
6. **Tier and partitions.** The partition table follows the size of the flash, the tier the
   memory: MAX only with PSRAM.
7. **Modules.** When the pins of the board are not the ones a module takes for its chip, give
   them in the build, or add a `board:<name>` entry to `boardPins` in its `module.json`.

A new chip is another matter: it needs its part of the hardware layer. See
[Hardware abstraction layer](hal.md).

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="build.md">← Build and flash</a> &nbsp;·&nbsp;
<a href="architecture.md">Architecture →</a>
</p>
