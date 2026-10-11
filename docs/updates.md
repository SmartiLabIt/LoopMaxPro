<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/updates.svg" alt="Versions and updates" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="cloud.md">← Cloud bridge</a>
</p>

---

A firmware holds LoopMaxPro and the modules built into it, and each of them has its own version.
This page says where the versions are written and shown, how a new firmware reaches a board
from USB and over the network, what is kept across an update and how to go back. It is for who
builds a firmware and for who keeps devices up to date.

- [The versions](#the-versions)
- [Where the version shows](#where-the-version-shows)
- [Updating from USB](#updating-from-usb)
- [Updating over the network](#updating-over-the-network)
- [What is kept](#what-is-kept)
- [Going back](#going-back)

## The versions

| | Where it is written | Its notes |
|---|---------------------|-----------|
| LoopMaxPro | The file [VERSION](../VERSION) in the root, one line | [CHANGES.md](../CHANGES.md) in the root |
| A module | `"version"` in its `module.json`, and the fourth argument of `LMP_MODULE(...)` in its source. The two must say the same; the firmware reports the one in the source | `CHANGES.md` in the folder of the module, for example [Zappy 2](../src/modules/zappy2/CHANGES.md) |

The two are counted apart. A new version of a module does not change the version of LoopMaxPro,
and a new LoopMaxPro leaves the versions of the modules as they are.

Before every build [tools/pio/version.py](../tools/pio/version.py) reads `VERSION` and writes
`include/lmp_version.h`, a generated file that is not in the repository:

| Define | Value |
|--------|-------|
| `LMP_VERSION` | The text of `VERSION`, as `1.0.12` |
| `LMP_VERSION_FULL` | The same, with `+` and the short hash of the commit when the build is made from a git checkout |
| `LMP_BUILD_DATE` | The day of the build, UTC, as `2026-10-09` |

[src/base/version.h](../src/base/version.h) compares two versions: up to four numbers separated
by dots, and a text after them makes a version older than the same numbers alone. So `1.0.1`
comes after `1.0.0`, and `1.0.0` after `1.0.0-dev`.

### The notes

Each `CHANGES.md` has the newest version on top:

```markdown
## 1.0.1 (2026-10-01)

A console that is safer with slow boards.

- One command at a time: repeated taps no longer send duplicate commands
- The overview lists the versions of LoopMaxPro, each with its notes
```

The title is the version, with its date in brackets. The lines that begin with `-` are the
notes; the other lines say what the version is about. The build of the console
([tools/web/build.py](../tools/web/build.py)) takes the newest version of the `CHANGES.md` in the
root and puts it in the console, as it is.

## Where the version shows

| Where | What |
|-------|------|
| Console, foot of every page | `LoopMaxPro <version>`, the board and the chip |
| Console, **Overview** | The version next to the name of the device; **Firmware build**, the day of the build; the card **LoopMaxPro versions**, with date and notes of the newest version the console knows, marked `installed` when it is the one of the device |
| Console, **Update** | **Installed** and **Built** |
| Console, **Modules** | Name and version of every module of the firmware |
| Console, page of a module, tab **Info** | **Version** of the module |
| `GET /api/state` | `fw` and `build`, and `version` for each entry of `modules`. No login needed |
| Document `sys.ota` | `installed` and `build` |
| Device report, part `device` | `fw` and `build` |
| Log, at every start | `LoopMaxPro <full version> (<build date>) board <board>` |

The pages of the console are in [Web console](web-console.md), the routes in
[HTTP API](http-api.md).

## Updating from USB

It works on every board, and it is the only way on the ESP8266 boards. The commands are the
standard ones of PlatformIO; environments and modules are in [Build and flash](build.md).

```
pio run -e esp32dev -t upload
```

| Family | `-t upload` writes | `-t uploadfs` writes |
|--------|--------------------|----------------------|
| ESP32 | Bootloader, partition table, boot data and the program. The console is inside the program | An empty filesystem, which takes the place of the files of the modules. Never needed for an update |
| ESP8266 | The program | The filesystem with the console in it, built from `web/` |

On the ESP8266 the console is a set of files, so a new LoopMaxPro wants both commands:

```
pio run -e esp8266_nodemcu -t upload
pio run -e esp8266_nodemcu -t uploadfs
```

There `uploadfs` replaces every file of the board, the settings of the system too: the device
starts as new and is set up again.

## Updating over the network

The update service, `sys.ota`, is in the build with `LMP_FEATURE_OTA`: on by default with the
network at tier 2 and above, so on every ESP32 board. The ESP8266 boards do not have it. The
flash of an ESP32 has two spaces for the program (see [partitions/](../partitions/)): a new one
is written in the space that is not running, and used from the next start.

In the console it is the tab **Update** of the LoopMaxPro space.

### From a file

The card **Firmware from a file** takes the `firmware.bin` of a build, found in
`.pio/build/<environment>/`. The browser works out the SHA-256 fingerprint of the file and sends
it with the file; the device checks the image, installs it and restarts by itself.

The console uses one request, which any program can send. The file is the body of the request as
it is, not a form:

```
POST /api/upload/ota?target=app&sha256=<64 hexadecimal digits>
Content-Type: application/octet-stream
Authorization: Bearer <token of the login>
```

```
curl -X POST --data-binary @firmware.bin \
  -H "Content-Type: application/octet-stream" -H "Authorization: Bearer <token>" \
  "http://<device>/api/upload/ota?target=app&sha256=<fingerprint>"
```

| Parameter | Meaning |
|-----------|---------|
| `target` | `app`, the program, or `fs`, an image of the filesystem. Default `app` |
| `sha256` | Optional. With it the image is installed only when its fingerprint is that one |

The token is the one of the login, see [HTTP API](http-api.md); it is not needed where the
console asks for no login. The answer is
`{"result":{"bytes":…,"sha256":"…","restart":true},"ok":true,"status":"ok"}`, or an error with
`error` and `msg`.

### From an address

The card **Firmware from an address** is there when the build has the client for web requests.
The device downloads the file, installs it and restarts. It is the command `fromUrl` of
`sys.ota`:

| Argument | Meaning |
|----------|---------|
| `url` | Required. It begins with `http://` or `https://`, at most 160 characters |
| `sha256` | Optional, 64 hexadecimal digits: the file is refused when it differs |
| `target` | Optional: `app` (default) or `fs` |
| `size` | Optional, in bytes: the file is refused when it is longer or shorter |

The command answers `pending` and the download goes on by itself, for five minutes at most. An
error page of the server is never taken for a firmware.

### The checks

| Check | When it fails |
|-------|---------------|
| One image at a time | `busy` |
| The image fits in its space | `no_mem`: "image too large" |
| The size is the one declared, and not zero | The image is given up: "longer than declared", "shorter than declared", "empty" |
| The fingerprint, when one was given | The image is given up: "fingerprint does not match" |
| A program is a whole image for this chip | The image is not accepted: "image not valid" |
| Login, where the console asks for one | HTTP 401 |

A program given up changes nothing: the one that is running stays. The reason is in the member
`error` of the document and in the log.

An image of the filesystem (`fs`) is different. It is written over the files as it arrives, so
the files of before are gone from its first byte, and it is checked only for size and
fingerprint. One given up half way leaves a filesystem that is made new at the next start.

### On trial

A program installed over the network starts **on trial**. It is confirmed by itself after it has
run for `LMP_OTA_CONFIRM_SEC` seconds (60) without restarting, or at once with the button
**Keep it** of the console, the command `confirm`. If the device restarts before that, it goes
back to the program of before. With `LMP_OTA_CONFIRM_SEC` at 0 only the command confirms.

### Document, commands and events

The document `sys.ota`:

| Member | Meaning |
|--------|---------|
| `available` | The board can update itself |
| `installed`, `build` | Version and day of the build of the program that is running |
| `state` | `idle`, `receiving`, `installed` (written and checked, about to restart) or `failed` |
| `target`, `from`, `received`, `size`, `percent`, `error`, `sha256` | The last image: `app` or `fs`, `file` or `url`, bytes, the reason it was given up, its fingerprint. Not there while `state` is `idle` |
| `trial` | The program that is running waits to be confirmed |
| `canRollback` | There is a program of before to go back to |
| `fromUrl` | The build can download an image from an address |
| `installs`, `failures` | Counters since the start |

| Command | What it does |
|---------|--------------|
| `sys.ota fromUrl` | Downloads an image and installs it, see above |
| `sys.ota confirm` | Confirms the program that is running |
| `sys.ota rollback` | Goes back to the program of before and restarts. `denied` when there is none |
| `sys.ota abort` | Gives up the image that is arriving |

While an image arrives the device publishes the event `ota.progress`, with the percent in `b`
and the bytes in `value`, every five parts in a hundred, or every 64 KB when the size is not
known. At the end `ota.done`, with 0 in `b` when the image was installed.

## What is kept

On an ESP32 the settings of the system and the device key are in the NVS, the files of the
modules (their settings and their data) in the filesystem, the console inside the program. On an
ESP8266 settings, key, files of the modules and console are all files of the filesystem.

| Way | Program and console | Settings of the system, device key | Files of the modules |
|-----|---------------------|-------------------------------------|----------------------|
| Network, `app` (ESP32) | New | Kept | Kept |
| Network, `fs` (ESP32) | The same | Kept | Replaced by the image |
| USB `-t upload` (ESP32) | New | Kept | Kept |
| USB `-t uploadfs` (ESP32) | The same | Kept | Erased: the image is empty |
| USB `-t upload` (ESP8266) | New program, console of before | Kept | Kept |
| USB `-t uploadfs` (ESP8266) | Program of before, new console | Lost | Lost |
| USB `-t erase` | Erased | Lost: the next firmware makes a new key at its first start | Lost |

Files of the console uploaded by hand, from the card **Console files** of the tab **Update**,
stay across an update and keep taking the place of the console of the program. The command
`sys.web useProgramConsole` removes them.

## Going back

| Way | How |
|-----|-----|
| By itself | A new program that restarts before it is confirmed is left for the one of before, see [On trial](#on-trial) |
| From the console | The button **Go back** of the tab **Update**, there while `canRollback` is true: the command `sys.ota rollback`. The device restarts with the program of before, and with its console |
| Installing the version of before | From a file or from USB, like an update. The update service does not compare versions: an older `firmware.bin` is installed like a newer one |

Going back changes the program only. The settings and the files of the modules stay as they are
now.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="cloud.md">← Cloud bridge</a>
</p>
