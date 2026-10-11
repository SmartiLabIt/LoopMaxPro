<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/hal.svg" alt="Hardware abstraction layer" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="architecture.md">← Architecture</a> &nbsp;·&nbsp;
<a href="kernel-and-services.md">Kernel and services →</a>
</p>

---

The hardware abstraction layer (HAL) is the only part of LoopMaxPro that knows the chip and the
framework. This page describes its interfaces, how the implementations are organised for the
ESP32 family, the ESP8266 and the PC, how the buses work, and what it takes to bring the system
to a new board or to a new family of chips. It is for who works on the system itself; a module
normally never touches the HAL.

- [What the HAL is for](#what-the-hal-is-for)
- [The table of the board](#the-table-of-the-board)
- [The interfaces](#the-interfaces)
- [How the implementations are organised](#how-the-implementations-are-organised)
- [ESP32 family: one HAL for Arduino and ESP-IDF](#esp32-family-one-hal-for-arduino-and-esp-idf)
- [ESP8266](#esp8266)
- [PC](#pc)
- [What each platform has](#what-each-platform-has)
- [Buses and external chips](#buses-and-external-chips)
- [Porting](#porting)

## What the HAL is for

`src/hal/api` defines the **capabilities** of a board as pure C++ interfaces with simple types:
a clock, a console, pins, a filesystem, a Wi-Fi link, an HTTP server. Kernel, drivers and
services use only those interfaces. The implementations live in separate folders, and each build
compiles exactly one of them. So the same kernel, the same services and the same modules run on
an ESP32 with the Arduino core, on an ESP32 with ESP-IDF, on an ESP8266 and on a PC.

The interfaces follow a few rules:

1. The headers of `src/hal/api` include only standard C headers, `src/base` and
   `lmp_config.h`: no Arduino, no ESP-IDF.
2. Text goes in as `const char*` or `StringView` and comes out in a buffer of the caller.
   Callbacks are `Delegate` objects, callables with fixed storage and no heap.
3. No exceptions and no RTTI. A call that can fail returns a `Status`
   ([src/base/status.h](../src/base/status.h)): `Ok`, `Pending`, `Busy`, `Invalid`, `NotFound`,
   `NoMem`, `Io`, `Timeout`, `Unsupported`, `Denied`, `Error`.
4. A capability that a board does not have is a null pointer in the table of the board. The
   interfaces do not change shape from one board to another.
5. The network does not make the system wait. A scan, a connection, a time request return at
   once and the outcome arrives later. The few calls that must wait for an answer say so in
   their header, and are called from a worker task: `IHttpClient::request()`,
   `INet::peersFind()`, the calls of `INetProbe`.
6. The HAL has no policy. When to connect again, when to bring up the access point, what to do
   with an HTTP request are decisions of the services above.

## The table of the board

A HAL implementation fills one table and defines two functions. They are declared in
[src/hal/api/board.h](../src/hal/api/board.h); here the table is shortened, every pointer starts
as null:

```cpp
namespace lmp::hal {

struct Board {
  const char* name;       // LMP_BOARD
  HalCaps caps;

  // Always present.
  ISystem* sys;   IClock* clock;   ISerial* serial;
  IRtos* rtos;    IRandom* rng;    IGpio* gpio;

  // Required by the OS: storage and network.
  IFs* fs;   IKv* kv;   INet* net;   IHttpServer* http;

  // Optional: null when the board or the build does not have them.
  IHash* hash;   IPwm* pwm;   IPixels* pixels;   IAdc* adc;
  II2c* i2c[LMP_HAL_I2C_MAX];   ISpi* spi;
  IUart* uart[LMP_HAL_UART_MAX];   IOneWire* oneWire;
  IOta* ota;   INtp* ntp;   IHttpClient* httpClient;
  IMqtt* mqtt;   IMqtt* cloudMqtt;   INetProbe* probe;

  // Chips on the board. Null when the board does not have them.
  const LoraWiring* lora;
};

void boot();              // early hardware setup: called once, first thing at startup
const Board& board();     // the board this firmware runs on

}
```

The kernel receives this table when it is created and hands it to the services with
`Kernel::board()`. A service that cannot work without a capability the board does not have says
so in its state; the rest of the system goes on.

`HalCaps` repeats the same information as bits, for who only needs to ask: `Rtos`, `Wifi`,
`Ethernet`, `Fs`, `Kv`, `Tls`, `Mqtt`, `Ota`, `Pwm`, `Rng`, `Sha256`, `CoreDump`, `Adc`, `I2c`,
`Spi`, `Uart`, `OneWire`, `HttpServer`, `HttpClient`, `Lora`
([src/hal/api/types.h](../src/hal/api/types.h)).

## The interfaces

One header for each group. Every method is documented in the header itself.

### System and time

| Interface | Header | What it gives |
|-----------|--------|---------------|
| `ISystem` | [isystem.h](../src/hal/api/isystem.h) | chip model, cores, revision, CPU and flash figures; free, lowest, total heap and largest free block; lowest free stack; reason of the last restart; factory MAC address; watchdog; restart; the "last words" a board keeps across a restart; the guard of the main task |
| `IClock` | [iclock.h](../src/hal/api/iclock.h) | `millis`, `uptimeMs`, `micros`; Unix time, read and set; time zone as a POSIX string; local time |
| `INtp` | [iclock.h](../src/hal/api/iclock.h) | time from a server: start with the name of the server, stop, number of answers. It never waits: each answer sets the clock |
| `IRtos` | [irtos.h](../src/hal/api/irtos.h) | tasks, mutexes, semaphores, queues, delay, yield; `preemptive()` tells whether tasks really exist |
| `IRandom`, `IHash` | [icrypto.h](../src/hal/api/icrypto.h) | random bytes from the best source of the board; incremental SHA-256 |
| `ISerial` | [iserial.h](../src/hal/api/iserial.h) | the console: `begin`, `write`, `read`, `available`, `flush`. Safe across tasks |

`ISystem::watchdogFeed()` is called by the kernel at every round of its loop. An implementation
that has something to do often does it there: emptying the buffer of a console, sampling the
lowest heap.

### Pins and buses

| Interface | Header | What it gives |
|-----------|--------|---------------|
| `IGpio` | [igpio.h](../src/hal/api/igpio.h) | valid pins, pins that can drive an output, mode, write, read, interrupts |
| `IPwm` | [igpio.h](../src/hal/api/igpio.h) | per pin: setup with frequency and resolution, duty, release |
| `IPixels` | [igpio.h](../src/hal/api/igpio.h) | lines of addressable LEDs (WS2812 and alike) on one pin: `begin`, `show`, `end` |
| `IAdc` | [igpio.h](../src/hal/api/igpio.h) | raw value and millivolts of an analog pin, resolution |
| `II2c` | [ibus.h](../src/hal/api/ibus.h) | `begin(sda, scl, hz)`, `probe`, `write`, `read`, `writeRead` |
| `ISpi` | [ibus.h](../src/hal/api/ibus.h) | `begin(sck, miso, mosi)`, `transfer` with the chip select pin, speed and mode of that transfer |
| `IUart` | [ibus.h](../src/hal/api/ibus.h) | extra serial ports, with a direction pin for RS485 |
| `IOneWire` | [ibus.h](../src/hal/api/ibus.h) | reset, read and write of a byte, search of the devices |

### Storage and update

| Interface | Header | What it gives |
|-----------|--------|---------------|
| `IFs`, `IFile` | [istorage.h](../src/hal/api/istorage.h) | mount, space, open, exists, remove, rename, mkdir, list. File handles come from a small fixed pool |
| `IKv` | [istorage.h](../src/hal/api/istorage.h) | small values by namespace and key: `get`, `set`, `remove`, `eraseNamespace`, `commit` |
| `IOta` | [istorage.h](../src/hal/api/istorage.h) | an image written in pieces to the application or to the filesystem: `begin`, `write`, `end`, `abort`; then confirm the new image or go back to the previous one |

### Network

| Interface | Header | What it gives |
|-----------|--------|---------------|
| `INet` | [inet.h](../src/hal/api/inet.h) | Wi-Fi client and access point, tracked separately and able to run together; asynchronous scan; mDNS name and announcement of the device; search of the other LoopMaxPro devices; DNS of the captive portal; events |
| `IHttpServer` | [ihttp.h](../src/hal/api/ihttp.h) | routes, requests that match no route, long bodies handed over in pieces (`onUpload`), a server-sent events channel |
| `IHttpRequest`, `IHttpResponse` | [ihttp.h](../src/hal/api/ihttp.h) | method, decoded path, query, headers, body up to `LMP_HTTP_BODY_MAX`, address of the client; status, headers, body, a file, a redirect |
| `ISseChannel` | [ihttp.h](../src/hal/api/ihttp.h) | one event to every client, without ever waiting for a slow one; a gate that decides who may follow |
| `IHttpClient` | [ihttp.h](../src/hal/api/ihttp.h) | one request with URL, method, headers, body, timeout and TLS options; the answer arrives in chunks. `secure()` tells whether `https` addresses can be reached |
| `IMqtt` | [ihttp.h](../src/hal/api/ihttp.h) | configure, connect, publish, subscribe, incoming messages, reason of the last failure. The driver connects again by itself |
| `INetProbe` | [iprobe.h](../src/hal/api/iprobe.h) | questions to the network: ping, TCP ports, UDP datagrams, ARP sweep, mDNS browse, SSDP search |

Three details that the services rely on:

- `INet::staConnect()` makes **one attempt** and the outcome arrives as an event. The driver
  never tries again by itself: the `net` service decides.
- The credentials of a network are never given to the chip to keep. They belong to the settings
  of the system.
- A long body, such as a firmware image, is never held in memory: `onUpload` hands it over as
  `Begin`, `Data` pieces, then `End` or `Aborted`.

### Memory

[memory.h](../src/hal/api/memory.h) is a small set of functions, not an interface of the table:
`allocLarge()` and `freeLarge()` for the large buffers taken once at start, which go to the
external RAM when the board has it, and `externalRam()` to know how much of it there is.

## How the implementations are organised

```
src/hal/
  api/         the interfaces, types.h, board.h
  common/      code for more than one implementation: FileKv (key/value store on files),
               the wiring of the LoRa radio from the build flags, the meaning of the
               Wi-Fi disconnection codes
  esp32/       the HAL of the whole ESP32 family, for both frameworks
  arduino/
    common/    console and pins written with the Arduino core: used by the ESP8266
    esp32/     the console on the USB port of the chip through the Arduino core
    esp8266/   the HAL of the ESP8266
  native/      the HAL of the PC
```

[platformio.ini](../platformio.ini) always compiles `hal/api` and `hal/common`, then adds the
folders of the family:

| Section | Adds |
|---------|------|
| `[mcu_esp32]` | `hal/esp32/` and `hal/arduino/esp32/` |
| `[mcu_esp8266]` | `hal/arduino/common/` and `hal/arduino/esp8266/` |
| `[env:native]` | `hal/native/` |

The ESP-IDF builds do not use that filter: their sources are listed in
[src/CMakeLists.txt](../src/CMakeLists.txt), which takes `hal/api`, `hal/common` and `hal/esp32`.

## ESP32 family: one HAL for Arduino and ESP-IDF

`src/hal/esp32` is written with the ESP-IDF drivers. The Arduino core carries the same drivers
inside (ESP-IDF 4.4 in the core 2.0.x), and the ESP-IDF environments build with ESP-IDF 6.0, so
the same files serve both frameworks. Where the drivers changed between versions, the code
chooses with `ESP_IDF_VERSION`. Kernel, services and modules do not know which framework is
compiled.

| File | Content |
|------|---------|
| `esp32_board.cpp` | system, clock, tasks (FreeRTOS), random generator, the table of the board |
| `esp32_io.cpp` | console, pins, PWM on the LEDC unit, ADC |
| `esp32_storage.cpp` | LittleFS through the VFS, and NVS as key/value store |
| `esp32_net.cpp` | Wi-Fi, mDNS, the DNS of the captive portal |
| `esp32_http.cpp` | HTTP server with a task of its own, events, long bodies in pieces |
| `esp32_online.cpp` | time from the network, HTTP client, MQTT client |
| `esp32_update.cpp` | firmware and filesystem update, I2C |
| `esp32_spi.cpp` | SPI bus |
| `esp32_pixels.cpp` | addressable LEDs on the RMT unit |
| `esp32_probe.cpp` | the questions of `INetProbe` |
| `esp32_memory.cpp` | large buffers in PSRAM where the board has it |
| `esp32_guard.cpp` | the last words and the guard of the main task |

The same on both frameworks, because it is the same code:

- LittleFS on the partition named `spiffs`: the files of a board stay when it moves from a
  framework to the other.
- The name of the chip, its revision and the factory MAC address, so the host name does not
  change.
- PWM: a timer gives frequency and resolution, and pins that ask for the same values share it.
- ADC: the first converter of the chip, with the factory calibration.
- HTTP client and MQTT verify the server against the certificates of the public authorities
  that the framework carries.

Differences between the chips of the family (ESP32, S2, S3, C3, C6) are not written board by
board: valid pins, PWM channels, I2C ports and the USB unit come from the description of the
chip that ESP-IDF provides.

**The console** is chosen by the board, with the USB flags of the Arduino core or with the
console option of the ESP-IDF configuration:

| Console | Typical boards | Behaviour |
|---------|----------------|-----------|
| First UART, wired to the USB bridge of the board | ESP32, boards with a serial adapter | transmit buffer of `LMP_SERIAL_TX_BUFFER` bytes |
| Serial and debug unit of the chip (USB), with ESP-IDF | C3, S3, C6 | with nobody reading, a write waits at most `LMP_SERIAL_TX_TIMEOUT_MS` |
| USB port of the chip through the Arduino core | S2, and S3 or C3 without a bridge | a write never waits: the text goes to a buffer that is emptied a little at every round of the system |

The third one, in `hal/arduino/esp32`, is the only piece of the ESP32 HAL tied to the Arduino
core.

**After a restart.** The last lines of the log and the reason of a restart asked on purpose are
kept in the RTC memory, which a restart does not clear. The next start writes them in the log,
so a board that stopped says why. The guard of the main task restarts a board whose application
task stays blocked.

## ESP8266

One core, no tasks, about 80 KB of memory. The system runs in one context, inside `loop()`.

| Capability | How |
|------------|-----|
| Tasks | none. `IRtos` exists and is cooperative: `preemptive()` is false, `createTask()` answers `Unsupported`, the mutex does nothing |
| Console | the UART of the chip, behind a buffer of `LMP_SERIAL_TX_BUFFER` bytes that feeds it a little at every round |
| Key/value store | `FileKv`, on files |
| Filesystem | LittleFS of the core |
| PWM | one frequency and one resolution for all the pins: a pin that asks for different values gets `Busy`. From 100 Hz to 40 kHz, from 4 to 16 bits |
| Analog input | one, `A0`, 10 bits, from 0 to 1 V at the pin of the chip |
| HTTP server | the server of the core, served inside `poll()`. No long bodies: `onUpload` answers `Unsupported`, so no firmware can be uploaded |
| HTTP client | plain HTTP only, built when a module asks for it. With no worker task, a request holds the system while it lasts |
| MQTT, update, SPI | not available |
| I2C | one bus on any two pins, 128 bytes for each transfer |
| Heap | the chip does not say how large it is: the total is what was free at the start |
| Large buffers | a second heap in the instruction RAM that the code leaves free, so the main heap stays for the network |

The valid pins are 0 to 5 and 12 to 16; 6 to 11 belong to the flash.

## PC

`src/hal/native` is a board that does not exist, but behaves like one: a real clock, files in a
folder, a console on standard output, and a network, a broker and servers that live in memory.
It may use the C++ standard library freely, because it never runs on a microcontroller.

It serves three purposes:

1. **Unit tests.** The tests of kernel, services, SDK and modules run on the PC against this
   HAL. Each fake part has hooks: a test decides which Wi-Fi networks are around, what a server
   answers, which chips sit on the I2C bus, and reads back what the system sent.
2. **Simulator.** The `native` environment also builds a program that runs the whole system on
   the PC. Its HTTP server opens a real port, `8088` unless the `LMP_SIM_PORT` environment
   variable names another one, so a browser can open the console without a board. Lines typed
   on the keyboard reach the serial command line.
3. **Module development.** The logic of a module can be written and tested with fake pins and
   fake devices before any hardware exists.

## What each platform has

| Capability | ESP32 family | ESP8266 | PC |
|------------|--------------|---------|----|
| Tasks | FreeRTOS | cooperative | threads |
| Filesystem | LittleFS | LittleFS | a folder |
| Key/value store | NVS | files on LittleFS | files |
| Wi-Fi, HTTP server, time from the network | yes | yes | simulated; the HTTP server can open a real port |
| HTTP client | `http` and `https` | `http`, when a module asks | simulated |
| MQTT | yes | no | simulated |
| Firmware and filesystem update | yes | no | keeps the image in memory |
| PWM | timers shared by pins with the same values | one frequency for all the pins | recorded |
| Addressable LEDs | two lines at most | no | recorded |
| ADC | first converter, calibrated | `A0` | no |
| I2C | two buses where the chip has them | one bus | fake devices |
| SPI | one bus | no | recorded |
| Network probe | yes | no | simulated |
| LoRa radio | wiring from the build flags | no | simulated radio |
| `IUart`, `IOneWire`, `IHash` | interface defined, no implementation yet | | |

## Buses and external chips

**I2C.** `II2c` is one bus. The table of the board holds up to `LMP_HAL_I2C_MAX` of them (2).
`writeRead()` is the usual register access, a write followed by a read with a repeated start.

**SPI.** `ISpi` is implemented for the ESP32 family. Speed and mode belong to each transfer, so
different chips can share the bus. `transfer()` drives the chip select pin for the duration of
the transfer; with `kSpiNoCs` it touches no pin, for the drivers that handle the select line
themselves. `kSpiNoPin` as `miso` opens a bus on which nobody answers, such as a display.

**Sharing.** A bus is shared by several chips and by several parts of the system. A module does
not take a bus from the table of the board: it opens it through the `bus` service, which
reserves the pins, lets a second user join a bus that is already open on the same pins, refuses
different ones, and gives back the `II2c` or `ISpi` to talk through. See
[Kernel and services](kernel-and-services.md).

**Chips on the board.** The table of the board also says which external chips are mounted and
how they are wired. Today there is one: the LoRa radio, `Board::lora`, with chip, band and pins
taken from the build flags of the environment (`LMP_LORA_CHIP`, `LMP_LORA_BAND`,
`LMP_LORA_PIN_*`). The HAL does not drive the chip. The driver in `src/drivers/lora` does, as
portable code, through `ISpi` and `IGpio`. See [LoRa](lora.md).

**Key/value store on files.** `FileKv` ([src/hal/common/file_kv.h](../src/hal/common/file_kv.h))
gives an `IKv` to the boards that have no store of their own. It sits on top of `IFs`, so it is
the same on every board and is tested on the PC. One file for each namespace; the namespace in
use stays in memory, `set()` changes the memory and `commit()` writes the file; a file that
cannot be read counts as empty, and the device starts with its defaults. Names are limited to
15 characters, as in NVS.

## Porting

### A new board of a supported family

No C++ is needed. A board is an environment in [boards/](../boards):

1. Add a section for the board with its PlatformIO `board`, its partition table and its flags:
   the tier (`${tier_min.build_flags}`, `${tier_mid.build_flags}` or `${tier_max.build_flags}`),
   the name (`-DLMP_BOARD=\"myboard\"`) and the pin of the reset button (`-DLMP_RESET_PIN=`, or
   leave it out when there is none).
2. Add the environment that joins it to a framework. For ESP-IDF, add also the file
   `boards/sdkconfig.<environment>` with the options of that board: flash size, console, PSRAM.
3. Say where the console is: for a board without a USB bridge, the USB flags of the Arduino
   core (`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`) or the USB console in the ESP-IDF
   configuration.
4. For a board with a LoRa radio, add `${lora_sx126x.build_flags}`, the band and the pins.
5. Build it and check the first lines of the log: chip, flash size, free heap.

The existing sections of [boards/esp32.ini](../boards/esp32.ini) and
[boards/esp8266.ini](../boards/esp8266.ini) are the model to copy. See [Boards](boards.md) for
the boards already supported and [Build and flash](build.md) for the commands.

### A new family of chips

A new family is a new folder under `src/hal`. It has to:

1. Define `lmp::hal::boot()` and `lmp::hal::board()`.
2. Implement what is always present (`ISystem`, `IClock`, `ISerial`, `IRtos`, `IRandom`,
   `IGpio`) and what the OS requires (`IFs`, `IKv`, `INet`, `IHttpServer`). Everything else is
   optional: leave the pointer null, and leave out of the build the services that need it, with
   the `LMP_FEATURE_*` options of [include/lmp_config.h](../include/lmp_config.h).
3. Define `allocLarge()`, `freeLarge()`, `externalRam()` and `roomForSecure()` of
   [memory.h](../src/hal/api/memory.h).
4. Add a `[mcu_<family>]` section to `platformio.ini` with the platform, the `-DLMP_MCU_...`
   flag and the folders to compile, and the board environments that use it.
5. If the framework is not Arduino or ESP-IDF, add its entry point to
   [src/main.cpp](../src/main.cpp).

Some parts are ready to reuse: `FileKv` when the chip has no key/value store; a cooperative
`IRtos`, as the ESP8266 has, when there are no tasks; `hal/arduino/common` for console and pins
on any Arduino core. Nothing above the HAL has to change: if a service needs a change to run on
the new chip, the interface is missing something, and that is where the change belongs.

The PC implementation is a good one to read first: it is plain C++, with no vendor driver in
the way, and it shows what the system expects from each call.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="architecture.md">← Architecture</a> &nbsp;·&nbsp;
<a href="kernel-and-services.md">Kernel and services →</a>
</p>
