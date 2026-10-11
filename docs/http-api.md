<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/http-api.svg" alt="HTTP API" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="web-console.md">← Web console</a> &nbsp;·&nbsp;
<a href="serial-console.md">Serial console →</a>
</p>

---

Every LoopMaxPro device with a network answers a small HTTP API: the same one its
[web console](web-console.md) uses. This page is the reference of that API as the web service
implements it ([src/services/web/](../src/services/web/)): routes, login, documents, commands,
log, real-time events, uploads and errors. Read it to drive a device from a script, from a
program of your own or from another interface.

- [The rules](#the-rules)
- [Routes](#routes)
- [Access](#access)
- [The state before the login](#the-state-before-the-login)
- [Documents](#documents)
- [Commands](#commands)
- [Log](#log)
- [Real-time events](#real-time-events)
- [Files of the console](#files-of-the-console)
- [Uploads: firmware and files](#uploads-firmware-and-files)
- [Paths of a module](#paths-of-a-module)
- [Errors](#errors)
- [Trying it](#trying-it)

## The rules

A device has no route for each function. Everything is a **document** to read or a **command**
to run, on a **target**: `sys.<service>` for a service of the system, `mod.<module>` for a
module. They are the same documents and commands of the [serial console](serial-console.md) and
of [MQTT](network-and-home-assistant.md); what each service offers is in
[Kernel and services](kernel-and-services.md).

| | |
|---|---|
| Base | `/api`, plain HTTP, port 80 (8088 on the PC simulator) |
| Format | JSON, UTF-8, without spaces |
| Version | the answers of the web service have the header `X-LMP-Api: 2`. The same number is `api` in `/api/state` |
| Cache | the answers of the API have `Cache-Control: no-store` |
| Reading | `GET`. It never changes anything |
| Changing | `POST`, and only through the commands |
| Body of a request | 1024 bytes at most (`LMP_HTTP_BODY_MAX`). Longer: `413` |
| Answer | 3072, 8192 or 16384 bytes at most, by the resource tier of the board (`LMP_HTTP_RESP_MAX`). The number of a device is `answerMax` in `doc/sys.web`. Longer: `507` |
| One at a time | the device answers one request at a time; the others wait. When it cannot take a request in time it answers `503` with `Retry-After: 1` |
| Paths | a slash at the end is ignored |

## Routes

| Method | Route | Login | What it gives |
|--------|-------|-------|---------------|
| GET | `/api/state` | no | the least needed to start: name, mode, language, version, whether a login is needed |
| POST | `/api/auth/login` | no | opens a session |
| GET | `/api/auth/me` | no | the state of the session |
| POST | `/api/auth/logout` | no | closes the session of the token it carries |
| GET | `/api/report` | yes | the whole report of the device |
| GET | `/api/report?parts=device,net` | yes | only the parts asked for |
| GET | `/api/doc/<target>` | yes | the state of a service or of a module |
| GET | `/api/cfg/<target>` | yes | its settings, without the secrets |
| GET | `/api/manifest` | yes | the list of the targets |
| GET | `/api/manifest/<target>` | yes | the commands of a target, with arguments and limits |
| POST | `/api/cmd/<target>/<command>` | yes | runs a command |
| GET | `/api/log?since=&max=&level=&src=` | yes | the log, in pages |
| GET | `/api/events` | yes | events in real time |
| POST | `/api/upload/ota?target=&sha256=` | yes | a firmware, or a filesystem image, from a file |
| POST | `/api/upload/file?name=&sha256=` | yes | one file of the console |
| GET | anything else | no | the files of the console |

"Login: yes" means a token, unless the request comes from the access point of the device: see
below. The targets of a device are whatever its firmware holds; the real list is
`GET /api/manifest`:

```json
{"targets":["sys.system","sys.log","sys.serial","sys.storage","sys.config","sys.net","sys.web","mod.zappy2"]}
```

(shortened: a device has more of them.)

## Access

| Where the request comes from | Access |
|------------------------------|--------|
| The access point of the device | **free**: who is connected got in with the password of the access point |
| The network | with a token |

The access point is on in AP mode, as a fallback when the network is lost, and for 30 seconds
after a change of network asked from there. A client of the access point is known by its
address, `192.168.4.x`. When the network the device joined uses the same addresses nobody can
be told apart, and the token is asked of everybody.

`/api/state` says what holds **for who is asking**, in `auth.required`: the same device can
answer `false` to a client of its access point and `true` to one on the network.

**Password.** The user is always `admin`. The password is the one chosen with
`sys.web setPassword`; until one is chosen, it is the password of the access point (`loopmaxap`
from the factory: change it). An empty password is never accepted.

**Login.**

```
POST /api/auth/login
Content-Type: application/json

{"password":"the password"}
```

```
200
{"token":"3f9a0c51d2e84b7a9c6f0e1d2b3a4c5d","ttl":86400,"user":"admin"}
```

The token, 32 hexadecimal digits, goes in every request:

```
Authorization: Bearer 3f9a0c51d2e84b7a9c6f0e1d2b3a4c5d
```

A browser cannot add a header to the request of the events, so the token is accepted in the
address as well: `/api/events?token=3f9a0c51...`.

| | |
|---|---|
| How long | `ttl` seconds from the login: 24 hours unless the setting `sessionTtlMin` says otherwise. It does not get longer with use |
| Sessions together | 2, 4 or 8, by the resource tier of the board (`sessionsMax` in `doc/sys.web`). One more takes the place of the one silent for longest |
| Where they are | in memory: a restart closes them all. So do `sys.web setPassword` and `sys.web logoutAll` |
| Wrong passwords | after 5 from one address, that address waits 30 seconds; then 60, 120, up to 15 minutes. While it waits not even the right password is looked at |

| Answer of the login | When |
|---------------------|------|
| `400` `{"error":"invalid","msg":"password missing","field":"password"}` | no `password` in the body |
| `401` `{"error":"denied","msg":"wrong password","field":"password"}` | wrong password |
| `429` `{"error":"busy",...}` with `Retry-After: <seconds>` | too many wrong passwords from this address |

The body of the login can also be a form (`password=...`), with
`Content-Type: application/x-www-form-urlencoded`.

**The other two routes.**

```
GET /api/auth/me
200 {"required":true,"auth":true,"user":"admin","ttl":86123}
```

It always answers `200`. `required`: whether who asks needs a token. `auth`: whether the token
sent is good. `ttl`: the seconds it has left.

```
POST /api/auth/logout
200 {"ok":true}
```

`ok` is false when the token was not of a session.

Any other route asked without a good token answers
`401 {"error":"auth","msg":"login required"}`.

## The state before the login

```
GET /api/state
```

```json
{"v":2,"api":2,"name":"Kitchen sockets","host":"loopmax-a1b2","board":"esp32dev","edition":"full",
 "code":"","fw":"1.0.12","build":"2026-03-14","lang":"en","mode":"LAN","fallback":false,
 "auth":{"required":true,"user":"admin"},
 "console":{"installed":true,"inProgram":true,"uploaded":false},
 "modules":[{"name":"zappy2","title":"Zappy 2","icon":"🔌","kind":"hardware","version":"1.0.1"}]}
```

| Field | |
|-------|---|
| `v`, `api` | version of the format, version of the API |
| `name`, `host` | the name people gave the device (it can be empty) and its host name |
| `board` | the board the firmware was built for |
| `edition` | `light` on the smallest boards, `full` on the others. The light edition leaves the help texts out of the manifests |
| `code` | the code of the kind of device, when the build gives one. Empty otherwise |
| `fw`, `build` | version of LoopMaxPro and date of the build |
| `lang` | language of the device, two letters |
| `mode` | `AP` or `LAN` |
| `fallback` | true while the access point is on because the network was lost |
| `lanes` | only on a firmware built to say so: how many requests a client should send together. A device short of memory asks for this |
| `auth.required` | whether who asks needs a token |
| `console` | `installed`: the device has the files of its console; `inProgram`: they are part of the firmware; `uploaded`: files uploaded by hand are in use in their place |
| `modules` | the modules of the firmware: `name`, `title`, `icon`, `kind`, `version`, and `ui` when the module has an interface of its own |

Nothing here identifies the device: no key, no hardware address.

## Documents

**Report.** Everything about the device in one answer. Its part `device` holds the key of the
device, which `/api/state` never shows.

```
GET /api/report?parts=device,services
200 {"v":2,"device":{...},"services":[...]}
```

The parts are `device`, `services`, and one for each service that has one: `system`,
`storage`, `clock`, `net`, `geo`, `cloud`, `modules`, as far as the build has those services.
Without `parts` they all come. The content of each part is in
[Data model and report](data-model.md).

**State of a target.**

```
GET /api/doc/sys.web
```

```json
{"server":"esp_http_server","port":80,"running":true,"api":2,
 "auth":{"required":true,"user":"admin","ownPassword":false,"sessions":1,"sessionsMax":4,
         "sessionTtlMin":1440,"wrongPasswords":0},
 "events":{"clients":1,"clientsMax":4},
 "console":{"installed":true,"inProgram":true,"uploaded":false},
 "requests":128,"refused":2,"failed":0,"canUpload":true,"uploads":0,"answerMax":8192,
 "urls":["http://loopmax-a1b2.local/","http://192.168.1.50/"]}
```

This is the document of the web service itself. `ownPassword` is false while the console still
uses the password of the access point; `canUpload` is false on the boards that cannot receive
long files; `urls` are the addresses the console answers at.

**Settings of a target.** A secret never leaves the device: in its place there is
`<name>Set`, true or false.

```
GET /api/cfg/sys.web
200 {"adminPswSet":false,"sessionTtlMin":1440}
```

A target without settings answers `{}`. The settings of a module are at `/api/cfg/mod.<name>`.

**Manifest of a target.** The commands, each with its arguments.

```
GET /api/manifest/sys.web
```

```json
{"target":"sys.web","commands":[
  {"name":"setPassword","help":"Password of the console. Empty: the password of the access point is used",
   "args":[{"name":"old","type":"str","min":0,"max":63,"secret":true,"required":true},
           {"name":"new","type":"str","min":0,"max":63,"secret":true,"required":true}]},
  {"name":"logoutAll","help":"Closes every session of the console","args":[]},
  {"name":"removeFile","help":"Removes a file of the console","confirm":true,
   "args":[{"name":"name","type":"str","min":1,"max":64,"def":"","required":true}]}]}
```

(two commands left out.)

| Member | |
|--------|---|
| `help` | what the command does, in English. Missing in the light edition, and with `?help=0` |
| `confirm` | an interface should ask before sending it. The device runs it all the same |
| `async` | the command answers `pending` and ends later, with an event |
| `anyArgs` | the command takes arguments by any name, not only the ones listed |
| `args[].type` | `bool`, `u8`, `u16`, `u32`, `i8`, `i16`, `i32`, `i64`, `f32`, `f64`, `str` |
| `args[].min`, `max` | limits of a number; shortest and longest length of a text |
| `args[].def`, `secret`, `required` | default value; a text never to be shown or logged; whether it must be there |

The manifest of a module has two members more: `module` (`title`, `icon`, `kind`, `version`,
`code`) and, when the module has settings, `config`, their schema: a list of fields, each with
`key`, `type`, `min`, `max` and `def`, and with `secret`, `readOnly` or `locked` where they
apply.

`GET /api/manifest/<target>?help=0` leaves the help texts out: for an answer that would not fit
in a small board.

An unknown target answers `404 {"error":"not_found","msg":"no such target","field":"target"}`
on all three routes.

## Commands

```
POST /api/cmd/mod.zappy2/set
Content-Type: application/json

{"channel":0,"value":true}
```

```
200
{"result":{},"ok":true,"status":"ok"}
```

The arguments are a JSON object; a command without arguments takes an empty body. The answer
always has this shape, the same on the serial console and on MQTT:

| Field | |
|-------|---|
| `ok` | true for `ok` and `pending` |
| `status` | `ok`, `pending`, `busy`, `invalid`, `not_found`, `no_mem`, `io`, `timeout`, `unsupported`, `denied`, `error` |
| `field` | the argument that was refused, when there is one |
| `result` | what the command gives back |
| `ref` | only with `pending`: the number its closing event will carry |

| HTTP code | `status` |
|-----------|----------|
| 200 | `ok` |
| 202 | `pending`: accepted, the outcome comes with an event |
| 400 | `invalid` |
| 403 | `denied` |
| 404 | `not_found` |
| 409 | `busy` |
| 501 | `unsupported` |
| 504 | `timeout` |
| 507 | `no_mem` |
| 500 | `io`, `error` |

Two refusals, as they arrive:

```
400 {"result":{},"ok":false,"status":"invalid","field":"channel"}     channel out of its limits
403 {"result":{},"ok":false,"status":"denied","field":"channel"}      a schedule holds the channel
```

**Commands that end later.** A command marked `async` in the manifest answers `pending` with a
`ref`. When it ends, the event `cmd` leaves with the same number in `value` and the outcome in
`b`: 0 `ok`, 1 `pending`, 2 `busy`, 3 `invalid`, 4 `not_found`, 5 `no_mem`, 6 `io`, 7 `timeout`,
8 `unsupported`, 9 `denied`, 10 `error`.

```
POST /api/cmd/sys.clock/sync        202  {"result":{},"ok":true,"status":"pending","ref":7}
event                                    {"id":"cmd","a":0,"b":0,"value":7}
```

**Arguments as a form.** With `Content-Type: application/x-www-form-urlencoded` the body is read
as a form: `channel=0&value=true`. A value written that way cannot hold a double quote. Names
that the command does not know are ignored, in a form as in JSON. A body that is neither is
`invalid`.

**Before the command runs.** A path that is not `/api/cmd/<target>/<command>` answers `400`; a
target or a command that does not exist answers
`404 {"error":"not_found","msg":"no such command","field":"command"}` (`field` is `target` when
it is the target that is missing).

**In the log.** Every command leaves one entry with where it came from:
`sys.config set from local: ok`. The arguments are never written.

### The commands of the web service, `sys.web`

| Command | Arguments | |
|---------|-----------|---|
| `setPassword` | `old`, `new` | the password of the console. `new` has 8 characters at least; empty, the password of the access point is used again. A wrong `old` is `denied`. It closes every session |
| `logoutAll` | none | closes every session, and the streams of events |
| `files` | none | the files of the console in the filesystem: `result.files` with `name` and `size`, then `count` and `bytes` |
| `removeFile` | `name` | removes one of them |
| `useProgramConsole` | none | removes the files uploaded by hand: the console of the firmware is used again. `unsupported` on a firmware without a console inside. `result.removed` says how many went |

The commands of the other services are in [Kernel and services](kernel-and-services.md), those
of the modules in [Modules and SDK](modules-sdk.md) and in the page of each module.

## Log

```
GET /api/log?since=412&max=20&level=warn&src=net
```

```json
{"seqLast":431,"bootDoneMs":640,"dropped":0,
 "items":[{"seq":415,"ms":86400123,"lvl":"warn","src":"net","msg":"connection to 'Home' lost"}]}
```

| Parameter | |
|-----------|---|
| `since` | only the entries with a greater number. For the next page: the number of the last one received |
| `max` | how many. The device gives at most as many as fit in one answer |
| `level` | `error`, `warn`, `info`, `debug`, `trace`, or its number from 1 to 5: entries up to that level. Anything else: `400`, field `level` |
| `src` | one source only: `net`, `web`, `cmd`, ... |

`ms` is the time since the start of the device. An entry that was too long has `"cut":true`.
The sources, and the log level of the device, are in `doc/sys.log`.

## Real-time events

```
GET /api/events?token=...
```

A stream of *server-sent events*: in a browser it is read with `EventSource`. Three kinds.

| Kind | Data |
|------|------|
| `event` | an event of the system |
| `log` | a log entry just written. Its `id` is the number of the entry |
| `ping` | `{}`, every 15 seconds: the connection is alive |

```
event: event
data: {"id":"net.sta.up","a":0,"b":0,"value":0}

id: 431
event: log
data: {"seq":431,"ms":5526,"lvl":"info","src":"net","msg":"connected to 'Home', address 192.168.1.50, signal -52 dBm"}
```

An event has always `id`, `a`, `b` and `value`. It says **what changed** and does not carry the
document: who receives it reads again the document it needs. So an event is always small.

| `id` | When | What to read again |
|------|------|--------------------|
| `svc` | a service changes state. It also carries `service` and `state`, as names | `report?parts=services` |
| `mode` | the mode changes: `a` is 1 for AP, 2 for LAN | `state` |
| `config` | settings change. `a` is the section: 0 `sys`, 1 `net`, 2 `web`, 3 `clock`, 4 `cloud`, 5 `mqtt`, 6 `geo` | `cfg/<target>` |
| `net.ap.up`, `net.ap.down` | the access point | `doc/sys.net` |
| `net.sta.up`, `net.sta.lost` | the link to the network | `doc/sys.net` |
| `net.fallback` | the access point comes on as a fallback | `doc/sys.net`, `state` |
| `net.join` | a change of network, or a test, ended: `a` is 1 for a test, `b` is 1 when the network accepted the device | `doc/sys.net` |
| `net.near` | the list of the other LoopMaxPro devices on the network changed: `b` says how many | `doc/sys.net` |
| `internet.up`, `internet.down` | Internet reachable or not | `doc/sys.net` |
| `time` | the time was set. `a` is the source: 1 a command, 2 a time server, 3 the cloud. `value` is the Unix time | `doc/sys.clock` |
| `cmd` | a command that answered `pending` ended: `value` is its `ref`, `b` the outcome | what the command changed |
| `ota.progress` | an image is arriving: `b` is the percentage, `value` the bytes. One event every five points | nothing |
| `ota.done` | the image is installed (`b` is 0) or was given up | `doc/sys.ota` |
| `mqtt.up`, `mqtt.lost` | the link to the broker | `doc/sys.mqtt` |
| `cloud.up`, `cloud.lost` | the link of the cloud bridge | `doc/sys.cloud` |
| `reset` | a restart was asked for; `a` is 1 for the factory reset | nothing: the device is going away |
| `module.state` | a module changes state. `a` is its place in `modules` of `/api/state`; `b` the state: 0 `Stopped`, 1 `Init`, 2 `Ready`, 3 `Degraded`, 4 `Error` | `doc/sys.modules` |
| `module.data` | the data of a module changed; `a` is its place in the list. One every 200 ms at most for each module | `doc/mod.<name>` |
| `lora.rx`, `lora.tx` | a packet received, a transmission ended | `doc/sys.lora` |

| | |
|---|---|
| Who arrives later | does not get the past: it asks for it with `/api/log` |
| Connection dropped | the browser reconnects by itself; the entries lost are asked with `/api/log?since=` |
| Slow client | loses events: the device waits for nobody |
| Clients together | as many as the sessions; one on the light edition (`events.clientsMax` in `doc/sys.web`). One more gets `503 {"error":"busy","msg":"too many clients"}` |
| Without access | `401`, and no stream |

## Files of the console

Whatever does not begin with `/api` is a file of the [web console](web-console.md): from the
firmware when the build put the console there, otherwise from the folder `/web` of the
filesystem.

| Rule | |
|------|---|
| `/` | `index.html` |
| Compressed | the files are stored compressed and sent with `Content-Encoding: gzip`. In the filesystem `<file>.gz` is looked for first |
| Cache | a file with a fingerprint in its name, such as `lmp.13759944.js`, is good for a year: `max-age=31536000, immutable`. The others are `no-cache` |
| Fingerprint | a part of the name between two dots, 8 hexadecimal digits at least |
| Types | `html`, `js`, `css`, `json`, `svg`, `png`, `jpg`, `ico`, `woff2`, `txt`; anything else is `application/octet-stream` |
| Access | free: the login page must be able to load |
| Method | `GET` only: another one answers `405` |
| Out of the folder | never: a path with `..` is refused with `400` |
| Missing | `404`, as text |

Without `index.html` the device answers with a small page that says the console is not
installed. `/api/state` tells it in `console.installed`.

**Captive portal.** While the access point is on with its captive portal, the device answers
every name with its own address, and a request for another site gets a `302` to
`http://192.168.4.1/`: phones and PCs open the console by themselves. The API is never
redirected.

## Uploads: firmware and files

A firmware does not fit in memory: it arrives in pieces and is written as it arrives. The body
of the request is the file as it is, not a form.

```
POST /api/upload/ota?target=app&sha256=<64 hexadecimal digits>
Content-Type: application/octet-stream

<the bytes of the image>
```

```
200
{"result":{"bytes":1227853,"sha256":"9f2c...","restart":true},"ok":true,"status":"ok"}
```

| Parameter | |
|-----------|---|
| `target` | `app`, the firmware, or `fs`, the image of the filesystem. Missing: `app` |
| `sha256` | the fingerprint expected. When it is there and does not match, the image is given up |

After a `200` the device **restarts by itself**, a second and a half later. The progress
arrives with the events `ota.progress`; `sha256` in the answer is the fingerprint the device
computed. What happens to a new firmware at its first start is in
[Versions and updates](updates.md).

| Code | `error` | When |
|------|---------|------|
| 400 | `invalid` | fingerprint not well written, unknown `target`, image empty or of another length than declared, fingerprint that does not match, transfer interrupted |
| 401 | `auth` | no access. Nothing is read |
| 409 | `busy` | another image is arriving, or the device is about to restart |
| 413 | `no_mem` | the image, by the length declared, is larger than its place |
| 500 | `io`, `no_mem`, `error` | the image could not be written, turned out too large, or was not accepted |
| 501 | `unsupported` | no firmware update in this build |
| 503 | `denied` | the update service is not running |

**One file of the console**, which touches nothing else and asks for no restart:

```
POST /api/upload/file?name=lmp.13759944.js.gz&sha256=<64 hexadecimal digits>
```

```
200
{"result":{"bytes":48211,"sha256":"...","path":"/web/lmp.13759944.js.gz"},"ok":true,"status":"ok"}
```

| | |
|---|---|
| Where it goes | always into the folder `/web`. A name with `/` or `\`, one that begins with a dot, or one longer than 64 characters is refused: `400`, field `name` |
| Half way | the file is written under a temporary name and takes its own at the end: a transfer that stops leaves the file of before |
| `sha256` | when given and different from the one of the bytes received: `400`, field `sha256`, and nothing is kept |
| No room | `507` `no_mem` |
| No filesystem | `503` `unsupported` |
| After it | the files uploaded come before those inside the firmware, until `sys.web useProgramConsole` |
| List and removal | `sys.web files`, `sys.web removeFile` |

The two routes exist only where the HTTP server of the board can take a long body: `canUpload`
in `doc/sys.web`. The ESP8266 family cannot.

## Paths of a module

A module whose purpose is to answer other programs can take one word for itself, with
`httpPrefix()` and `onHttp()` of the SDK ([src/sdk/http.h](../src/sdk/http.h)): everything that
arrives at `/<word>/...` is then its own. These paths are outside `/api` and follow other rules.

| | |
|---|---|
| Login | none |
| From where | the local networks only: `10.x`, `172.16`-`172.31`, `192.168.x`, `169.254.x` and the PC itself. From anywhere else: `403 {"error":"forbidden","msg":"only from the local network"}` |
| Methods | any: the module decides |
| Pages of another origin | allowed: the answers carry `Access-Control-Allow-Origin: *`, and the web service answers the `OPTIONS` request of a browser by itself, with `204` |
| Answer | code, type and body are of the module. `Cache-Control: no-store` |

Every other module goes through documents and commands.

## Errors

Outside the commands, an error has this shape:

```json
{"error":"not_found","msg":"no such target","field":"target"}
```

`msg` is a short text in English for who is developing; `field` is there when one argument or
one part of the path is to blame.

| Code | `error` | When |
|------|---------|------|
| 400 | `invalid` | a request not well written |
| 401 | `auth` | no token, or a token that is good no more |
| 401 | `denied` | wrong password at the login |
| 403 | `forbidden` | a path of a module asked from outside the local networks |
| 404 | `not_found` | a route, a target or a command that does not exist |
| 405 | `method` | the wrong method: `msg` says the right one |
| 413 | `too_long` | a body over 1024 bytes. Not for the uploads |
| 429 | `busy` | too many wrong passwords |
| 503 | `busy` | the device is busy: try again after a second |
| 507 | `too_large` | the answer does not fit in the memory of the device. Ask for less: `?parts=`, `?help=0`, a smaller `max` |

A command that the device refuses is not one of these: it answers with the shape of the
[commands](#commands), and its `status`.

## Trying it

With `curl`, from a PC on the same network. `curl -d` sends a form unless told otherwise: say
that the body is JSON.

```
curl http://loopmax-a1b2.local/api/state

curl -X POST http://loopmax-a1b2.local/api/auth/login \
     -H "Content-Type: application/json" -d '{"password":"the password"}'

curl -H "Authorization: Bearer $TOKEN" "http://loopmax-a1b2.local/api/report?parts=device,net"

curl -X POST http://loopmax-a1b2.local/api/cmd/mod.zappy2/set \
     -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
     -d '{"channel":0,"value":true}'

curl -N "http://loopmax-a1b2.local/api/events?token=$TOKEN"
```

Without a board, the PC simulator answers the same API on `http://localhost:8088`: how to start
it is in [Web console](web-console.md#working-with-the-pc-simulator). Two things are of the
simulator only: a request from the PC itself counts as one from the access point, so no token
is needed while the simulated access point is on; and every answer carries
`Access-Control-Allow-Origin: *`, so that a page served by another program of the PC can call
it. A real device sends that header only for the [paths of a module](#paths-of-a-module).

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="web-console.md">← Web console</a> &nbsp;·&nbsp;
<a href="serial-console.md">Serial console →</a>
</p>
