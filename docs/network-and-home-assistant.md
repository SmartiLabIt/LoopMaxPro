<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="30"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/network-and-home-assistant.svg" alt="Discovery, MQTT and Home Assistant" height="36"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="serial-console.md">← Serial console</a> &nbsp;·&nbsp;
<a href="lora.md">LoRa →</a>
</p>

---

How a LoopMaxPro device gets on a network, how devices announce themselves and find each other,
and how a device talks to an MQTT broker and shows up in Home Assistant. For who installs a
device at home, and for who writes a module that should appear in Home Assistant.

- [On the network](#on-the-network)
- [How a device announces itself](#how-a-device-announces-itself)
- [Finding the other devices](#finding-the-other-devices)
- [Questions to the network](#questions-to-the-network)
- [MQTT](#mqtt)
- [Home Assistant](#home-assistant)
- [Declaring things in a module](#declaring-things-in-a-module)
- [Light edition and small boards](#light-edition-and-small-boards)

The commands below are written as on the [serial console](serial-console.md):
`cmd <target> <command> name=value`. The same commands run from the web console, from the
[HTTP API](http-api.md) and from MQTT.

## On the network

The net service (`sys.net`, [src/services/net/](../src/services/net/)) keeps the access point and
the client of a network as two separate things. A device is in one of two modes.

| Mode | What the device does |
|------|----------------------|
| `AP` | Turns on its own Wi-Fi network and waits there. Every new device starts so. |
| `LAN` | Is a client of the network written in its settings. |

### Access point

| | |
|---|---|
| Name | `LoopMax-` and the last four hex digits of the MAC address: `LoopMax-9F3A` |
| Password | `loopmaxap` until it is changed. The console warns while it is still this one |
| Address | `192.168.4.1` |
| Captive portal | On (`captive` in the settings): phones and PCs open the console by themselves |
| Channel | 1, 6 or 11, chosen from the MAC address, so that devices close to each other spread. `LMP_NET_AP_CHANNEL` fixes one |

Who is connected to the access point got in with its password and opens the console without a
login. Who comes from the home network logs in: see [Web console](web-console.md).

### Home network

`join` moves the device to a network. It tries the new credentials, and when it gets an address
it saves them and switches to mode `LAN`. When it does not get one within `rollbackSec` (180
seconds by default) it goes back by itself to where it was: the network of before, or the access
point. `test` tries the credentials and keeps nothing.

Once on a network:

- The access point stays up for 30 seconds more, so that who asked for the change can read the
  new address, then it goes down.
- A lost connection is tried again with growing waits, from 2 to 30 seconds.
- After 5 failed attempts in a row (`LMP_NET_FALLBACK_AFTER`) the access point comes back as a
  fallback, and the device keeps trying its network every 120 seconds
  (`LMP_NET_FALLBACK_RETRY_SEC`).
- A device that has lost its network, or Internet, for 15 minutes with nobody on its access point
  restarts (`LMP_NET_GUARD_MIN`; 60 minutes after a restart for this same reason; 0 turns the
  guard off).
- Internet is checked by asking the address in `probeUrl`, in the builds with the HTTP client.
  The outcome is in the document (`internet`) and in the events `internet.up` and
  `internet.down`.

`state` in the document of `sys.net` is one of `off`, `ap`, `connecting`, `connected`,
`retrying`, `fallback`.

### Commands of `sys.net`

| Command | Arguments | What it does |
|---------|-----------|--------------|
| `scan` | – | Looks for the Wi-Fi networks around. Answers `pending`; the list is in `scan.networks` |
| `near` | – | Looks for the other LoopMaxPro devices on this network: see [below](#finding-the-other-devices) |
| `test` | `ssid`, `psw` | Tries the credentials of a network and keeps nothing. Only while the device is reached through its access point |
| `join` | `ssid`, `psw` | Moves the device to a network; goes back by itself when it does not work |
| `setAp` | `ssid`, `psw` | Name and password of the access point, from the next restart. The password has 8 to 63 characters |
| `setMode` | `mode` | `AP` or `LAN`. `LAN` is `denied` until a network was joined |

```
cmd sys.net scan
cmd sys.net join ssid=MyNetwork psw=my-password
doc sys.net
```

## How a device announces itself

In the builds with mDNS, a device with its access point up or connected to a network answers to
the name `<host>.local`. The host name is `loopmax-` and the last four hex digits of the MAC
address (`loopmax-9f3a`) until it is changed in the settings.

It announces two services, both on the port of the console (80):

- `_http._tcp`, the web one;
- **`_loopmaxpro._tcp`**, the service of the system. Who looks for it finds the LoopMaxPro
  devices, and only those. The instance name is the host name.

The TXT records of `_loopmaxpro._tcp`:

| Key | Example | Meaning |
|-----|---------|---------|
| `api` | `2` | Version of the API of the device (`LMP_API_VERSION`) |
| `id` | `AA:BB:CC:DD:9F:3A` | Public id of the device: its MAC address |
| `name` | `Garden sockets` | The name the owner gave. Left out when there is none |
| `code` | | Device code of the build (`LMP_DEVICE_CODE`). Left out when empty, as with the free modules |
| `mod` | `zappy2` | The modules, separated by commas |
| `title` | `Zappy 2` | What the device is: the title of its first module, the one its console opens on |
| `icon` | `🔌` | The icon of that module |
| `fw` | `1.0.12` | Version of LoopMaxPro |
| `board` | `esp32dev` | Board |
| `ed` | `full` | Edition: `full` or `light` |

Nothing empty is announced. The announcement is renewed when the host name, the name of the
device or its modules change.

From a PC the standard tools show it:

```
dns-sd -B _loopmaxpro._tcp          # macOS, Windows with Bonjour
avahi-browse -rt _loopmaxpro._tcp   # Linux
```

Finding is not commanding. Anybody on the network can announce anything: `GET /api/state` on the
address found confirms that a LoopMaxPro answers there, and its commands still ask for its
password.

## Finding the other devices

The device searches, not the browser: a web page cannot search a network.

| | |
|---|---|
| Command | `cmd sys.net near` asks the network who announces `_loopmaxpro._tcp`. It answers `pending` and ends in about 3 seconds, with the event `cmd`. `denied` when the device is not connected to a network: there is no search from the access point |
| By itself | 45 seconds after the device joined the network, then every 6 hours (`LMP_NET_NEAR_EVERY_MIN`) |
| Result | The object `near` in the document of `sys.net` |
| Event | `net.near` when the list changes; `b` is how many devices |
| Limits | At most 12 devices (`LMP_NET_NEAR_MAX`), sorted by `id`. The device itself is not in the list |

```json
"near": { "state": "done", "ageSec": 12, "devices": [
  { "id": "AA:BB:CC:DD:9F:3A", "name": "Garden sockets", "host": "loopmax-9f3a",
    "ip": "192.168.1.50", "api": 2, "code": "", "mod": "zappy2", "title": "Zappy 2",
    "icon": "🔌", "fw": "1.0.12", "board": "esp32dev", "ed": "full" } ] }
```

`state` is `idle` (never searched), `running`, `done` or `failed`. `title` and `icon` are there
only when the device announced them, `port` only when it is not the usual one. The console of a
device found opens at `http://<ip>/`.

What a device says of itself comes from the network. Every text is cut at a whole character
when it is too long, and what is not UTF-8 becomes `?`: the document stays valid whoever
announced.

### Devices that are not on a network yet

A device that is not on a network has its access point up, and the others know it by that name:
the prefix `LoopMax-`.

- In `scan.networks` it carries `"device": true`.
- It stays in the list also with a weak signal: when the networks found are more than the list
  holds (16; 8 on the smallest boards), the networks of anybody else leave first.

A device whose access point was renamed with `setAp` is not recognized any more. And finding it
is not setting it up: for that, connect to its Wi-Fi network.

### The "Find devices" page

In the LoopMaxPro space of the console, after Network: `#/sys/devices`
([web/console/devices.js](../web/console/devices.js)).

| | |
|---|---|
| **Search** | One button, two searches one after the other: `near`, then `scan`. Not together: while the radio looks for the networks around it does not hear the answers of the other devices |
| **On this network** | The devices of `near.devices`: name, what it is, `<host>.local`, address, version, board. The whole row opens the console of that device in a new tab |
| **To set up** | The access points of the devices that are not on a network, with their signal, and the three steps to set one up: connect to its Wi-Fi network, the console opens by itself or at `http://192.168.4.1`, choose the network in the Network tab |
| On opening | The list of the network is asked again when it is older than a minute; the networks around only when they were never looked for |
| From the access point | The page says that the device cannot look for the others from there, and still lists the devices to set up |
| Builds without mDNS | The page says so and shows only the devices to set up |

## Questions to the network

The probe service (`sys.probe`, [src/services/probe/](../src/services/probe/)) asks questions to
the network the device is on. It serves the diagnosis of a network, and it is what
[ExpNet](../src/modules/expnet/README.md) is built on.

| Command | Arguments | What it does |
|---------|-----------|--------------|
| `ping` | `host`, `timeoutMs` | Asks an address for an answer, and how long it takes |
| `tcp` | `host`, `port`, `timeoutMs` | Whether a port of a device takes connections |
| `wake` | `mac`, `broadcast` | Wakes a PC with its MAC address (Wake-on-LAN) |

`host` is an IPv4 address. The commands answer `pending`; the outcome arrives with the event
`cmd` and is in `last` of the document. A module asks through `IProbeService` (`ctx().probe`),
which also sweeps a network with ARP and searches it with mDNS and SSDP:
[src/services/probe/probe_service.h](../src/services/probe/probe_service.h).

## MQTT

The MQTT service (`sys.mqtt`, [src/services/mqtt/](../src/services/mqtt/)) is one more way in
for the commands and one more way out for documents and events: the same ones of the console.
It is off by default: it connects when `enabled` is true and a broker is written in the
settings. It works in mode `LAN`, while the device is connected to its network.

### Settings

| Setting | Default | Command | Meaning |
|---------|---------|---------|---------|
| `enabled` | `false` | `set` | MQTT on or off |
| `host` | empty | `set` | Address of the broker |
| `port` | `8883` | `set` | Its port |
| `tls` | `true` | `set` | Encrypted connection. The certificate of the broker is checked against the certificates in the firmware |
| `user`, `psw` | empty | `set` | Name and password, when the broker asks for them |
| `base` | empty | `setTopics` | First part of every topic. Empty: `lmp/<host name>` |
| `keepAliveSec` | `60` | `setTopics` | Keep alive, 10 to 3600 seconds |
| `events` | `true` | `setTopics` | The events of the system are published too |
| `home` | `true` | `setTopics` | The modules are told to Home Assistant |
| `homePrefix` | `homeassistant` | `setTopics` | Where Home Assistant looks for them |

A broker at home usually listens on port 1883 without encryption:

```
cmd sys.mqtt set host=broker.local port=1883 tls=false user=name psw=secret enabled=true
```

The device connects with the client id `lmp-<host name>`. The other commands are `reconnect`,
which leaves the broker and connects again, and `publish` (`topic`, `payload`, `retain`), which
sends a message under the base of the device.

### Topics

`<base>` is `lmp/<host name>` unless the settings say otherwise.

| Topic | Direction | What it carries |
|-------|-----------|-----------------|
| `<base>/status` | out, retained | `online`, `offline`. `offline` is also the last will: the broker says it when the device disappears |
| `<base>/cmd/<target>/<command>` | in | Runs a command. The content is its arguments, as a JSON object |
| `<base>/ack/<target>/<command>` | out | The answer of the command, the same JSON the HTTP API gives |
| `<base>/get/<target>` | in | Asks a document. The target `report` asks the report of the device; the content names the parts wanted, separated by commas |
| `<base>/doc/<target>` | out | The document asked |
| `<base>/evt` | out | The events of the system: `{"id":"net.near","a":0,"b":2,"value":0}` |
| `<base>/home/...` | both | What the modules show to Home Assistant: see [below](#home-assistant) |

With the usual tools of a broker:

```
mosquitto_sub -h broker.local -t 'lmp/loopmax-9f3a/#' -v
mosquitto_pub -h broker.local -t 'lmp/loopmax-9f3a/cmd/mod.zappy2/set' -m '{"channel":0,"value":true}'
mosquitto_pub -h broker.local -t 'lmp/loopmax-9f3a/get/sys.net' -m ''
```

The second line is answered on `lmp/loopmax-9f3a/ack/mod.zappy2/set`:

```json
{"result":{},"ok":true,"status":"ok"}
```

Targets, commands and documents are the ones of the [HTTP API](http-api.md); documents and
report are described in [Data model and report](data-model.md).

### Limits

- A message received has a topic shorter than 96 bytes and a content shorter than 512
  (`LMP_MQTT_TOPIC_MAX`, `LMP_MQTT_PAYLOAD_MAX`). A command with a longer content is not run.
- Four messages wait for the round of the system (`LMP_MQTT_INBOX`), and one is handled for each
  round. The ones that find the queue full are counted in `dropped`.
- An answer is as large as an answer of the HTTP API (`LMP_MQTT_OUT_MAX`). One that does not fit
  is answered with `{"ok":false,"status":"no_mem"}` for a command, `{"error":"too_large"}` for a
  document.

### From a module

A module finds the broker in its context: `ctx().mqtt`, an `IMqttService`
([src/services/mqtt/mqtt_service.h](../src/services/mqtt/mqtt_service.h)). The pointer is null
in the builds without MQTT.

| Call | What it does |
|------|--------------|
| `connected()` | True while the broker is there |
| `publish(topic, payload, retain)` | Sends under the base: `x/meteo` leaves as `<base>/x/meteo` |
| `publishAt(topic, payload, retain)` | Sends at a whole topic, outside the base |
| `listen(prefix, listener)` | The messages of the topics under `<base>/<prefix>`. The listener runs on the application task |

`cmd`, `ack`, `get`, `doc`, `evt` and `status` belong to the system, `home` to the bridge
towards Home Assistant. Five parts of the system can listen (`LMP_MQTT_LISTENERS`), and the
bridge is one of them. A module that only wants to appear in Home Assistant needs none of this:
it declares its things, as described below.

## Home Assistant

A device presents itself to Home Assistant through its broker, with MQTT discovery. Home
Assistant needs its MQTT integration and nothing else: no add-on for LoopMaxPro, no line in its
configuration.

### Setting it up

1. In Home Assistant, add the MQTT integration with its broker (Mosquitto).
2. On the device, console → MQTT: turn on "MQTT enabled", write the address of the broker, its
   port, name and password, and save. Or the `set` command shown above.

The device then appears in Home Assistant under Settings → Devices & services → MQTT, with its
name, what it shows and what can be commanded. From there it reaches the voice assistants Home
Assistant works with.

The MQTT tab of the console has a box, "Integration with Home Assistant", with these steps and
the state: connected and how many things were presented, not presenting, or off. It warns when
a broker is written but MQTT was left off.

### What appears

One device for each LoopMaxPro device, and one entity for each thing its modules declare.

| The device | Comes from |
|------------|------------|
| Name | The name of the device, or its host name when it has none |
| Manufacturer | `SmartiLab` |
| Model | The title of the module |
| Software version | The version of LoopMaxPro |
| Link to the device | Its console, `http://<ip>/` |

| A thing of a module | Entity | Notes |
|---------------------|--------|-------|
| `toggle` | `switch` | With its device class: `outlet` for a socket |
| `light` | `light` | On and off, brightness, colour when the light has colours |
| `number` | `sensor` | Unit and device class; with a unit, a measurement with its decimals |
| `flag` | `binary_sensor` | With its device class: `problem`, `motion`, `connectivity`... |
| `text` | `sensor` | A word |
| `button` | `button` | Does one thing, has no state |

The names are in the language of the device. Every entity follows `<base>/status`: when the
device leaves the broker, Home Assistant shows it as unavailable.

What the free modules present:

| Module | In Home Assistant |
|--------|-------------------|
| [Zappy 2](../src/modules/zappy2/README.md) | Two sockets, `ch0` and `ch1`: switches of class `outlet`, with the names given to the channels |
| [RGB Controller](../src/modules/rgb/README.md) | One light, `light0`: on and off, brightness, colour |
| [GanjaLeaf](../src/modules/ganjaleaf/README.md) | Two lights, `light0` and `light1`: the leaf and the ring |
| [MeteoWifi](../src/modules/meteowifi/README.md) | Sensors `temperature`, `feels`, `humidity`, `wind`; the button `refresh` |
| [ExpNet](../src/modules/expnet/README.md) | For each service watched, whether it answers and how long it takes; for each PC, whether it is on and a button that wakes it; how many devices are in the network |

### Topics of the bridge

`<prefix>` is `homeassistant`. `<node>` is `lmp_` and the MAC address in lower case, without
separators: `lmp_aabbccdd9f3a`.

| Topic | Direction | What it carries |
|-------|-----------|-----------------|
| `<prefix>/<entity>/<node>/<module>_<thing>/config` | out, retained | What a thing is: the discovery message |
| `<base>/home/<module>/state` | out, retained | The values of the things of the module, in one JSON: `{"ch0":"ON","ch1":"OFF"}` |
| `<base>/home/<module>/<part>/state` | out, retained | The same, for a part of the module that is a device of its own |
| `<base>/home/<module>/light/<thing>` | out, retained | A light, as Home Assistant reads it: state, brightness, colour |
| `<base>/home/<module>/set/<thing>` | in | `ON`, `OFF`, `PRESS`, or the JSON of a light. For a part: `set/<part>.<thing>` |

The discovery message of the first socket of a Zappy 2, on
`homeassistant/switch/lmp_aabbccdd9f3a/zappy2_ch0/config`:

```json
{
  "name": "Line 1",
  "uniq_id": "lmp_aabbccdd9f3a_zappy2_ch0",
  "avty_t": "lmp/loopmax-9f3a/status",
  "stat_t": "lmp/loopmax-9f3a/home/zappy2/state",
  "val_tpl": "{{ value_json.ch0 }}",
  "cmd_t": "lmp/loopmax-9f3a/home/zappy2/set/ch0",
  "dev_cla": "outlet",
  "dev": { "ids": ["lmp_aabbccdd9f3a"], "name": "Garden sockets", "sw": "1.0.12",
           "cu": "http://192.168.1.50/", "mf": "SmartiLab", "mdl": "Zappy 2" }
}
```

Everything is retained by the broker: Home Assistant finds every thing again after its own
restarts. The device tells everything again at each connection to the broker, then only what
changes, three messages for each round of the system.

What Home Assistant asks is answered with the state of the thing as it is now, also when the
device did not do it. A channel held by a schedule is not turned from Home Assistant either:
the schedule wins, as in the console.

### State and settings

`home` and `homePrefix` are in the [settings](#settings) above. The console has the switch
"Presents itself to Home Assistant"; `homePrefix` is changed with the command:

```
cmd sys.mqtt setTopics home=false
cmd sys.mqtt setTopics homePrefix=homeassistant
```

The document of `sys.mqtt` has the object `home`: `enabled`, `prefix`, `node`, `things` (the
things presented), `sent` (messages sent) and `asked` (requests of Home Assistant received).

### Good to know

- A thing that disappears stays in Home Assistant, not updated any more, until it is deleted
  there.
- Home Assistant does not see schedules, settings or the log: those stay in the console.
- The values of a module travel in one JSON message of at most 512 bytes.

## Declaring things in a module

A module knows nothing of Home Assistant. It tells its **things**, one by one, with what they
are now, and takes what it is asked: [src/sdk/home.h](../src/sdk/home.h).

```cpp
void things(lmp::sdk::ThingSink& out) const override {
  out.number("temperature", "Temperature", temp_, "°C", "temperature", 1, known_);
  out.number("humidity", "Humidity", humidity_, "%", "humidity", 0, known_);
  out.technical();
  out.flag("sensor_fault", "Sensor fault", !sensorOk_, "problem");
  out.button("refresh", "Refresh");
}

lmp::Status thingSet(const char* id, const lmp::sdk::ThingAsk& ask) override {
  if (strcmp(id, "refresh") == 0 && ask.press) return refreshNow();
  return lmp::Status::NotFound;
}
```

| Call | What it becomes |
|------|-----------------|
| `toggle(id, name, on, kind)` | Something on or off that can be turned. `kind`: `outlet` for a socket |
| `light(id, name, on, brightness, r, g, b, colours)` | A light. Brightness from 1 to 100 |
| `number(id, name, value, unit, kind, decimals, known)` | A number to read. `kind`: `temperature`, `humidity`, `battery`, `distance`... `known` false: no value now |
| `flag(id, name, on, kind)` | Yes or no, to read. `kind`: `problem`, `motion`, `occupancy`, `connectivity`... |
| `text(id, name, value)` | A word to read |
| `button(id, name)` | Something to press |
| `technical()` | The next thing, and only that one, is for who looks after the device: a broken sensor, a battery. Home Assistant keeps it among the diagnostic entities |
| `part(id, name)` | The things that follow belong to a part of the module that is a device of its own, reached through this one. A null id goes back to the device itself |

- A name is one text, or two: English first, then Italian. Home Assistant gets the one of the
  language of the device.
- An id never changes: it is how Home Assistant remembers a thing. Use lower case letters,
  digits and `_`: it becomes part of the topics and of the template that reads the value.
- `ThingAsk` carries only what was asked: `on`, `brightness` (1 to 100), a colour, `press`.
- When a value changes, `markChanged()` is enough, as for the console.
- The parts of the SDK tell theirs by themselves. A `PinBank` tells its channels (`ch0`,
  `ch1`), as switches, or as contacts to read for a bank of inputs; `kind("outlet")` makes them
  sockets. `Lights` tells its lights (`light0`). A module made only of those writes nothing:
  see [Zappy 2](../src/modules/zappy2/src/zappy2.cpp).

The same things are also data of the module, in the builds with MQTT
([src/sdk/things.h](../src/sdk/things.h)): its document has the object `things` with their
values, and its target has the commands `things`, which describes them, and `thing.set`:

```
cmd mod.zappy2 things
cmd mod.zappy2 thing.set thing=ch0 value=ON
```

More on modules in [Modules and SDK](modules-sdk.md) and [Writing a module](writing-a-module.md).

## Light edition and small boards

What a build has depends on its edition and on its tier: see [Build and flash](build.md) and
[Boards](boards.md). With the environments of `boards/*.ini`:

| | ESP32 family, full edition | `esp32s2_mini`, light edition | ESP8266, light edition |
|---|---|---|---|
| Access point, home network, `scan` | yes | yes | yes |
| `<host>.local`, announcement, `near` | yes | no | no |
| `sys.probe` | yes | no | no |
| MQTT and Home Assistant | yes | yes | no |

- The light edition leaves mDNS out: it costs memory those boards need. A light device is
  reached at its address, or through its access point and the captive portal. The others do not
  see it among the devices on the network, but they still list its access point among the
  devices to set up.
- MQTT, and Home Assistant with it, is in the builds of tier 2 and above. The ESP8266 boards
  are tier 1: a free module that should appear in Home Assistant needs a board of the ESP32
  family.
- The rules are in [include/lmp_config.h](../include/lmp_config.h): `LMP_FEATURE_MDNS`,
  `LMP_FEATURE_MQTT`, `LMP_FEATURE_PROBE`.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="serial-console.md">← Serial console</a> &nbsp;·&nbsp;
<a href="lora.md">LoRa →</a>
</p>
