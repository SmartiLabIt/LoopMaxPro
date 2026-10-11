<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/documentation.svg" alt="Documentation" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="build.md">Start: Build and flash →</a>
</p>

---

The documents of LoopMaxPro, by subject. Each page has the same bar on top and at the bottom:
home, this index, the page before and the page after.

## Start here

| | Document | What you find |
|---|----------|---------------|
| 1 | [Build and flash](build.md) | Requirements, environments, how a firmware gets its modules, building, flashing, the simulator and the tests on the PC |
| 2 | [Boards](boards.md) | The boards, what each one has, the two editions, how to choose a board for a module |

## Inside the system

| | Document | What you find |
|---|----------|---------------|
| 3 | [Architecture](architecture.md) | The principles, the layers, how the system starts and runs, memory, security |
| 4 | [Hardware abstraction layer](hal.md) | The interfaces to the hardware and how they are implemented on each platform |
| 5 | [Kernel and services](kernel-and-services.md) | The life cycle, commands and events, and the catalogue of the system services |
| 6 | [Data model and report](data-model.md) | Native data and fields, the documents, the system report, what is sent and when |

## Modules

| | Document | What you find |
|---|----------|---------------|
| 7 | [Modules and SDK](modules-sdk.md) | What a module is, its manifest, state, settings and commands, the tune |
| 8 | [Writing a module](writing-a-module.md) | From an empty folder to a working module, step by step |

The free modules have their own pages:
[EspRadio](../src/modules/espradio/README.md),
[ExpNet](../src/modules/expnet/README.md),
[GanjaLeaf](../src/modules/ganjaleaf/README.md),
[MeteoWifi](../src/modules/meteowifi/README.md),
[RGB Controller](../src/modules/rgb/README.md),
[Zappy 2](../src/modules/zappy2/README.md).

## Around the device

| | Document | What you find |
|---|----------|---------------|
| 9 | [Web console](web-console.md) | The console served by the device, the standard page of a module, a page of its own |
| 10 | [HTTP API](http-api.md) | Routes, access, documents, commands, logs, events |
| 11 | [Serial console](serial-console.md) | The commands from the serial port and how to read the log |
| 12 | [Discovery, MQTT and Home Assistant](network-and-home-assistant.md) | How devices are found, the broker, what Home Assistant shows |
| 13 | [LoRa](lora.md) | The point-to-point LoRa service |
| 14 | [Cloud bridge](cloud.md) | What the system offers to a bridge towards a server |
| 15 | [Versions and updates](updates.md) | The versions of the system and of the modules, and how a board is updated |

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="build.md">Start: Build and flash →</a>
</p>
