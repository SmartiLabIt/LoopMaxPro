<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/lora.svg" alt="LoRa" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="network-and-home-assistant.md">← Discovery, MQTT and Home Assistant</a> &nbsp;·&nbsp;
<a href="cloud.md">Cloud bridge →</a>
</p>

---

The LoRa service sends and receives packets from radio to radio, on the boards that have a LoRa
chip. This page says what it does, which radios and boards it supports, the rules of the band it
applies, its settings and commands, and how a module uses it.

## What the service does

LoRa is a service of the system, `sys.lora`, part of the firmware of the boards with a radio.
It is the only owner of the radio: modules and other services send and receive through it and
never touch the chip.

| | |
|---|---|
| Link | **Point to point**: packets from radio to radio, without a network and without a gateway |
| Chips | SX1261, SX1262, SX1268 |
| Bands | 868 MHz and 433 MHz, with the European limits |
| Packet | 1 to 255 bytes, text or any bytes |
| Listening | Continuous: the radio listens whenever it is not sending |
| Rules of the band | Applied by the service: frequency, power, time on air |

The service carries bytes and nothing else. Addresses, acknowledgements and encryption belong
to the module that uses it: whoever has the same radio parameters hears the packets. LoRaWAN,
the link to a network with gateways, is not part of the service.

```
 modules, other services ──▶ ILora ──────────────┐
 console, MQTT, serial ──▶ commands of sys.lora ─┤
                                                 ▼
                              services/lora   LoraService
                              rules of the band, time on air, settings,
                              packets received, events
                                                 │  ILoraRadio
                                                 ▼
                              drivers/lora    SX126x driver, on RadioLib
                                                 │  ISpi, IGpio, IClock
                                                 ▼
                              HAL             SPI bus and pins of the board
```

| Part | Files | Job |
|------|-------|-----|
| Service | [src/services/lora/lora_service.h](../src/services/lora/lora_service.h) | State, commands, settings, limits |
| Rules of the band | [src/services/lora/lora_band.cpp](../src/services/lora/lora_band.cpp) | Parts of the band, power, share of time |
| Time on air | [src/drivers/lora/lora_airtime.cpp](../src/drivers/lora/lora_airtime.cpp) | How long a packet lasts |
| Radio | [src/drivers/lora/lora_radio.h](../src/drivers/lora/lora_radio.h) | `ILoraRadio`, the same for every chip |
| Driver | [src/drivers/lora/sx126x_radio.cpp](../src/drivers/lora/sx126x_radio.cpp) | SX126x, with RadioLib 7.7.1 |
| Simulated radio | [src/drivers/lora/fake_radio.h](../src/drivers/lora/fake_radio.h) | For the tests and the simulator on the PC |

RadioLib does not talk to a framework: it talks to a class of LoopMaxPro that passes every
request to the SPI bus, the pins and the clock of the [HAL](hal.md). Only the SX126x part of the
library is compiled.

## Boards and radios

| Chip | Power, in dBm |
|------|---------------|
| SX1261 | −17 to 14 |
| SX1262 | −9 to 22 |
| SX1268 | −9 to 22 |

The board of `boards/*.ini` with a radio is the **Heltec WiFi LoRa 32 V3**: an ESP32-S3 with an
SX1262 for 868 MHz. Its environment is `heltec_v3`:

```
pio run -e heltec_v3 -t upload
```

The `native` environment, the simulator on the PC, has a simulated radio: the service and its
commands can be tried without a board.

### Adding a radio to a board

The section `[lora_sx126x]` of [platformio.ini](../platformio.ini) brings the library and turns
the service on. The board adds the band and the pins, as
[boards/esp32.ini](../boards/esp32.ini) does for the Heltec:

```ini
[b_heltec_v3]
board = heltec_wifi_lora_32_V3
board_build.partitions = partitions/esp32_8mb.csv
lib_deps = ${lora_sx126x.lib_deps}
flags =
  ${tier_mid.build_flags}
  ${lora_sx126x.build_flags}
  -DLMP_BOARD=\"heltec_v3\"
  -DLMP_RESET_PIN=0
  -DLMP_LORA_BAND=868
  -DLMP_LORA_PIN_CS=8
  -DLMP_LORA_PIN_SCK=9
  -DLMP_LORA_PIN_MOSI=10
  -DLMP_LORA_PIN_MISO=11
  -DLMP_LORA_PIN_RST=12
  -DLMP_LORA_PIN_BUSY=13
  -DLMP_LORA_PIN_DIO1=14
  -DLMP_LORA_TCXO_MV=1800
  -DLMP_LORA_DIO2_RF_SWITCH=1
```

| Flag | Meaning |
|------|---------|
| `LMP_FEATURE_LORA` | 1: the service is part of the build. Set by `[lora_sx126x]` |
| `LMP_LORA_CHIP` | 1261, 1262 or 1268. `[lora_sx126x]` sets 1262 |
| `LMP_LORA_BAND` | 868 or 433: **what the antenna circuit of the board is made for** |
| `LMP_LORA_PIN_CS`, `_SCK`, `_MOSI`, `_MISO`, `_RST`, `_BUSY`, `_DIO1` | The pins of the chip. All are needed: the build stops without them |
| `LMP_LORA_TCXO_MV` | Supply of the oscillator driven by the chip, in millivolts. 0, the default, when the board has a crystal |
| `LMP_LORA_DIO2_RF_SWITCH` | 1 when the chip drives the antenna switch with its DIO2 pin. Default 0 |

The service takes the seven pins for itself: they are listed in the Hardware tab of the console
with the owner `lora`, and a module cannot take them. The driver needs the SPI bus of the HAL,
which the ESP32 family has.

## The band and its rules

The bands LoRa uses are open to everyone, with limits on the frequency, on the power and on the
share of time a device may spend transmitting: the duty cycle. The service refuses what the
rules do not allow. Its table follows the European recommendation ERC 70-03 for short range
devices.

> The rules change and depend on the country. Check the ones in force where the device works:
> who operates the radio answers for it. The table is in
> [src/services/lora/lora_band.cpp](../src/services/lora/lora_band.cpp).

### 868 MHz

| From | To | Time on air in one hour | Highest power |
|------|----|-------------------------|---------------|
| 863.0 | 865.0 MHz | 0.1 %: 3.6 s | 14 dBm |
| 865.0 | 868.0 MHz | 1 %: 36 s | 14 dBm |
| 868.0 | 868.6 MHz | 1 %: 36 s | 14 dBm |
| 868.7 | 869.2 MHz | 0.1 %: 3.6 s | 14 dBm |
| 869.4 | 869.65 MHz | 10 %: 360 s | 22 dBm, the limit of the chips |
| 869.7 | 870.0 MHz | 1 %: 36 s | 14 dBm |

### 433 MHz

| From | To | Time on air in one hour | Highest power |
|------|----|-------------------------|---------------|
| 433.05 | 434.79 MHz | 10 %: 360 s | 10 dBm |

### How they are applied

- **Frequency**: the whole signal must stay inside one part of the band. With a bandwidth of
  125 kHz, 868.05 MHz is refused: the signal would go below 868.0.
- **Power**: not above the highest of the part of the band chosen, and not outside what the
  chip can do.
- **Time on air**: each part of the band has a share that is spent by transmitting and fills up
  again with time, up to what the rules allow in one hour. A packet that does not fit is
  refused with the status `denied` and the field `duty`.
- A board made for another band, 915 MHz for example, is not supported: the service does not
  start.

At the start the share is full: the device does not remember what it sent before a restart.

### How long a packet lasts

With a bandwidth of 125 kHz, coding rate 4/5, preamble of 8 symbols and CRC on:

| Spreading factor | 10 bytes | 51 bytes | Packets of 51 bytes in one hour, at 1 % |
|------------------|----------|----------|------------------------------------------|
| 7 | 41 ms | 103 ms | 350 |
| 9 | 144 ms | 329 ms | 109 |
| 12 | 991 ms | 2466 ms | 14 |

A higher spreading factor reaches farther with packets that are much slower.

## Settings

Kept in the storage of the device, namespace `lora`. A factory reset clears them.

| Field | Values | Default | Notes |
|-------|--------|---------|-------|
| `enabled` | true, false | true | Radio on |
| `listen` | true, false | true | Listens when it is not sending |
| `freq` | In hertz | 868100000 or 433175000 | By the band of the board |
| `bw` | 62500, 125000, 250000, 500000 | 125000 | Bandwidth, in hertz |
| `sf` | 7 to 12 | 7 | Spreading factor |
| `cr` | 5 to 8 | 5 | Coding rate: 5 means 4/5 |
| `sync` | 0 to 255 | 18, that is 0x12 | Sync word: 0x12 for private networks, 0x34 is the one of LoRaWAN |
| `power` | From the lowest of the chip to the highest of the band | 10 | In dBm |
| `preamble` | 6 to 65535 | 8 | In symbols. It has no command |
| `crc` | true, false | true | It has no command |

Two devices hear each other only with the same `freq`, `bw`, `sf`, `cr`, `sync`, `preamble` and
`crc`.

## Commands

The target is `sys.lora`.

| Command | Arguments | Notes |
|---------|-----------|-------|
| `set` | `freq`, `bw`, `sf`, `cr`, `power`, `sync` | Also one alone. Either every value is valid or nothing changes: the answer names the field that is not. `busy` while a packet is on air |
| `send` | `text` or `hex` | Up to 120 bytes; `hex` has two digits for each byte. It answers at once: the end arrives with the event `lora.tx` |
| `listen` | `on` | Listening on or off |
| `enable` | `on` | Turns the radio on or off |
| `clear` | – | Empties the packets received and the counters |

From the [serial console](serial-console.md):

```
cmd sys.lora send text=hello
cmd sys.lora send hex=0102ff
cmd sys.lora set freq=869525000 sf=9 power=14
cmd sys.lora listen on=false
doc sys.lora
```

The answer of `send`:

```json
{"result":{"seq":1,"bytes":4,"airtimeMs":31,"airtimeLeftMs":35969},"ok":true,"status":"ok"}
```

When `send` does not start:

| Status | Field | Meaning |
|--------|-------|---------|
| `busy` | – | Another packet is on air |
| `denied` | `duty` | The share of time of the band is not enough |
| `denied` | `enabled` | The radio is off |
| `invalid` | `text`, `hex` | No content, or both, or a `hex` that is not valid |

The same commands run from the [HTTP API](http-api.md) and from
[MQTT](network-and-home-assistant.md#mqtt). In the console, the Hardware tab has the LoRa card:
chip and band, state, frequency, power, signal on the channel, counters, the time on air used
this hour and the switch of the radio.

## State document

`doc sys.lora`, or `GET /api/doc/sys.lora`:

```json
{
  "chip": "SX1262", "band": 868, "state": "listening",
  "enabled": true, "listen": true,
  "freq": 868100000, "bw": 125000, "sf": 7, "cr": 5, "sync": 18, "power": 10,
  "preamble": 8, "crc": true,
  "limits": { "fromHz": 868000000, "toHz": 868600000, "dutyPercent": 1, "maxPower": 14,
              "airtimeHourMs": 36000, "airtimeLeftMs": 35969 },
  "stats": { "sent": 1, "received": 1, "damaged": 0, "failed": 0, "refused": 0, "airtimeMs": 31 },
  "channelRssi": -94,
  "last": { "rssi": -92.5, "snr": 7.2 },
  "inbox": [ { "seq": 1, "ms": 12345, "bytes": 9, "rssi": -92.5, "snr": 7.2,
               "hex": "74656d703d32312e35", "text": "temp=21.5" } ]
}
```

| Field | Meaning |
|-------|---------|
| `state` | `off`, `idle`, `listening`, `sending` |
| `limits` | The part of the band in use: its edges, its duty cycle, the highest power, the time on air of one hour and what is left of it |
| `syncNow` | The sync word a module asked for now, when it is not the one of the settings |
| `channelRssi` | Signal on the channel right now, in dBm: the noise, or somebody sending. Only while listening |
| `last` | Signal and signal to noise ratio of the last packet received |
| `stats.damaged` | Packets heard with errors and dropped |
| `stats.failed` | Transmissions that did not end |
| `stats.refused` | Transmissions the rules of the band did not allow |
| `stats.airtimeMs` | Time spent transmitting since the start |
| `inbox` | The last 8 packets, from the oldest (`LMP_LORA_INBOX`). `ms` is the uptime at the arrival; `text` is there only when the content is printable text |

## Events

| Event | In the code | Data |
|-------|-------------|------|
| `lora.rx` | `EventId::LoraReceived` | `a`: length in bytes; `value`: number of the packet |
| `lora.tx` | `EventId::LoraSent` | `b`: status of the transmission; `value`: number of the packet |

## From a module

A module asks the kernel for `ILora`
([src/services/lora/lora_service.h](../src/services/lora/lora_service.h)). The interface exists
only in the builds with the radio, so the code that uses it is kept inside `LMP_FEATURE_LORA`:

```cpp
#include "services/lora/lora_service.h"

lmp::Status onStart() override {
#if LMP_FEATURE_LORA
  lmp::ILora* lora = ctx().kernel->api<lmp::ILora>();
  if (lora && lora->available()) {
    lora->subscribe([this](const lmp::LoraPacket& p) { onPacket(p.data, p.len, p.rssiDbm); });
    const uint8_t hello[] = {'h', 'i'};
    lora->send(hello, sizeof(hello));          // Ok, Busy, Denied or Invalid
  }
#endif
  return lmp::Status::Ok;
}
```

| Call | What it does |
|------|--------------|
| `available()` | True when the radio is on and answers |
| `send(data, len)` | Starts a transmission and returns at once. `Busy` while another packet is on air, `Denied` when the radio is off or the band allows no more time on air |
| `send(data, len, preambleSymbols)` | The same with a longer preamble for this packet: for a receiver that sleeps and samples the channel at intervals. The preamble of the settings comes back after it |
| `useSyncWord(sync)` | A sync word for now, not saved: for a module that talks to its own devices. 0 goes back to the one of the settings |
| `subscribe(handler)` | The handler is called on the application task for every packet received. Four parts of the system can subscribe (`LMP_LORA_LISTENERS`) |
| `airtimeMs(len)` | What a packet of that length costs, in milliseconds |
| `airtimeLeftMs()` | What is left of the share of time |
| `settings()`, `stats()` | The settings and the counters |

- `send` only starts the transmission. A module that must know how it ended listens to the
  event `LoraSent` in its `onEvent`: one packet is on air at a time, so the next one is its own.
- A `LoraPacket` carries `seq`, `ms`, `rssiDbm`, `snrDb`, `len` and `data`.
- A longer preamble costs time on air: the share is charged for the packet as it leaves.

More on modules in [Modules and SDK](modules-sdk.md) and [Writing a module](writing-a-module.md).

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="network-and-home-assistant.md">← Discovery, MQTT and Home Assistant</a> &nbsp;·&nbsp;
<a href="cloud.md">Cloud bridge →</a>
</p>
