// LoopMaxPro - compile time configuration.
// Every value can be overridden with -DLMP_xxx=value in platformio.ini.
//
// THE TUNE (docs/modules-sdk.md). The values here are the ones of everybody. A module that
// needs another one on a board asks for it in its module.json, "tune", and the build gives it
// only to the firmwares with that module on that board (tools/pio/lmp_tune.py). So:
//   - never change a value here because one module needs it: that changes every other module;
//   - a new knob is born here with #ifndef and the value that was there before; a new mechanism
//     is born off (0): who needs it turns it on from its tune;
//   - LoopMaxPro is touched for implementations and for bugs, nothing else.
#pragma once

// ---- light edition ------------------------------------------------------------------------------
// The ESP8266 family runs the light edition, and the boards that ask for it with -DLMP_LIGHT=1
// (the ESP32-S2 mini): the same system and the same console, with less
// memory spent. Log texts in flash, no help texts for the commands, one browser following
// the events, the I2C bus only when a module asks for it. docs/build.md.
#ifndef LMP_LIGHT
#if defined(LMP_MCU_ESP8266)
#define LMP_LIGHT 1
#else
#define LMP_LIGHT 0
#endif
#endif
// 1: the commands carry their help text in the manifest. The light edition leaves it out: the
// console shows the name and the arguments.
#ifndef LMP_CMD_HELP
#define LMP_CMD_HELP (!LMP_LIGHT)
#endif

// ---- resource tier: 1 = MIN, 2 = MID, 3 = MAX ---------------------------------------------
#ifndef LMP_TIER
#define LMP_TIER 2
#endif

// ---- memory for the modules: 1 small, 2 medium, 3 large ---------------------------------------
// How much a module can keep in its lists and buffers, set by the board:
//   1 small:  ESP32-C3, about 100 KB free with the system running (boards/*.ini say it)
//   2 medium: ESP32, ESP32-C6, ESP32-S3 without PSRAM
//   3 large:  the boards with PSRAM, where the large buffers go, and the PC
// A module sizes what it keeps on it: as ExpNet does.
#ifndef LMP_MODULE_MEM
#if LMP_TIER >= 3
#define LMP_MODULE_MEM 3
#elif LMP_TIER == 2
#define LMP_MODULE_MEM 2
#else
#define LMP_MODULE_MEM 1
#endif
#endif

#if LMP_TIER <= 1
#define LMP_TIER_LOG_RING 32
#define LMP_TIER_LOG_MSG_LEN 96
#define LMP_TIER_HTTP_RESP_MAX 3072
#define LMP_TIER_MAX_SESSIONS 2
#define LMP_TIER_MAX_MODULES 4
#define LMP_TIER_MAX_COMMANDS 48
#elif LMP_TIER == 2
#define LMP_TIER_LOG_RING 100
#define LMP_TIER_LOG_MSG_LEN 128
#define LMP_TIER_HTTP_RESP_MAX 8192
#define LMP_TIER_MAX_SESSIONS 4
#define LMP_TIER_MAX_MODULES 8
#define LMP_TIER_MAX_COMMANDS 96
#else
#define LMP_TIER_LOG_RING 200
#define LMP_TIER_LOG_MSG_LEN 160
#define LMP_TIER_HTTP_RESP_MAX 16384
#define LMP_TIER_MAX_SESSIONS 8
#define LMP_TIER_MAX_MODULES 16
#define LMP_TIER_MAX_COMMANDS 160
#endif

#ifndef LMP_LOG_RING
#if LMP_LIGHT
#define LMP_LOG_RING 16   // light edition: the last entries; the serial port has them all
#else
#define LMP_LOG_RING LMP_TIER_LOG_RING
#endif
#endif
#ifndef LMP_LOG_MSG_LEN
#define LMP_LOG_MSG_LEN LMP_TIER_LOG_MSG_LEN
#endif
#ifndef LMP_HTTP_RESP_MAX
#define LMP_HTTP_RESP_MAX LMP_TIER_HTTP_RESP_MAX
#endif
#ifndef LMP_MAX_SESSIONS
#define LMP_MAX_SESSIONS LMP_TIER_MAX_SESSIONS
#endif
#ifndef LMP_MAX_MODULES
#define LMP_MAX_MODULES LMP_TIER_MAX_MODULES
#endif
#ifndef LMP_MAX_COMMANDS
#define LMP_MAX_COMMANDS LMP_TIER_MAX_COMMANDS
#endif

// ---- logging --------------------------------------------------------------------------------
// Levels: 1 error, 2 warn, 3 info, 4 debug, 5 trace. Calls above LMP_LOG_LEVEL are compiled out.
#ifndef LMP_LOG_LEVEL
#ifdef LMP_DEBUG
#define LMP_LOG_LEVEL 4
#else
#define LMP_LOG_LEVEL 3
#endif
#endif
#ifndef LMP_LOG_MAX_SOURCES
#define LMP_LOG_MAX_SOURCES 32
#endif
#ifndef LMP_LOG_MAX_SINKS
#define LMP_LOG_MAX_SINKS 4
#endif

// ---- kernel ---------------------------------------------------------------------------------
#ifndef LMP_MAX_SUBSCRIBERS
#define LMP_MAX_SUBSCRIBERS 16
#endif
#ifndef LMP_EVENT_QUEUE
#define LMP_EVENT_QUEUE 24
#endif
// Targets of documents and commands: services plus modules.
#ifndef LMP_MAX_TARGETS
#define LMP_MAX_TARGETS (20 + LMP_MAX_MODULES)
#endif
#ifndef LMP_CMD_MAX_ARGS
#define LMP_CMD_MAX_ARGS 8
#endif
// Room for the decoded text arguments of one command.
#ifndef LMP_CMD_TEXT
#define LMP_CMD_TEXT 256
#endif
// A tick longer than this is reported in the log, in milliseconds.
#ifndef LMP_TICK_WARN_MS
#define LMP_TICK_WARN_MS 50
#endif
#ifndef LMP_APP_STACK
#define LMP_APP_STACK 8192
#endif
// The guard of the application task: when it does not come round for these seconds the board
// restarts (boards with the guard, the ESP32 family). 0: off.
#ifndef LMP_STALL_GUARD_S
#define LMP_STALL_GUARD_S 60
#endif
// A request to a server that gives no sign of life for its wait and these seconds more has
// stuck the worker: the board restarts.
#ifndef LMP_HTTPC_STUCK_S
#define LMP_HTTPC_STUCK_S 60
#endif
// A guard on the memory for the secure connections, on the boards without external RAM: a new
// one is set up only with this many bytes free. Setting it up takes about 30 KB, and a first
// ESP32 whose memory touches zero, even for a moment, loses the Wi-Fi until the restart
// (seen on the first ESP32). A request to an https address waits for it in its queue,
// at most LMP_SECURE_WAIT_S seconds, then it fails as no_mem; a broker reached with TLS waits
// before each attempt. 0: no guard, as every module has it; a module short of memory on a
// board asks for it in its module.json (tune).
#ifndef LMP_SECURE_MIN_FREE
#define LMP_SECURE_MIN_FREE 0
#endif
#ifndef LMP_SECURE_WAIT_S
#define LMP_SECURE_WAIT_S 20
#endif
// For who develops: 1 writes on the serial port the largest blocks of memory in use, three
// minutes after the start and the first time the free memory gets low (ESP32 family). What
// holds the memory of a small board is read there, by its sizes.
#ifndef LMP_HEAP_MAP
#define LMP_HEAP_MAP 0
#endif
// For who develops: 1 writes on the serial port, second by second, what the Wi-Fi was given to
// send and what it refused, the data TCP sent a second time, how long each frame stayed in the
// radio and how many the router did not confirm, how long the board stood still and what was
// written on the flash (ESP32 family). Where a slow board loses its time is read there. The
// build needs five options for the linker too: they are in esp32_net.cpp.
#ifndef LMP_NET_TRACE
#define LMP_NET_TRACE 0
#endif
// Worker: the task for what takes long, such as a request to a server. Its stack holds a
// secure connection while it is set up. Jobs waiting for their turn.
#ifndef LMP_WORKER_STACK
#define LMP_WORKER_STACK 10240
#endif
#ifndef LMP_WORKER_QUEUE
#define LMP_WORKER_QUEUE 4
#endif
// Time between a change of the settings and the write to storage, in milliseconds.
#ifndef LMP_CONFIG_SAVE_DELAY_MS
#define LMP_CONFIG_SAVE_DELAY_MS 2000
#endif
// Command line on the serial port: 1 enabled, 0 disabled.
#ifndef LMP_FEATURE_CLI
#define LMP_FEATURE_CLI 1
#endif
#ifndef LMP_CLI_LINE
#define LMP_CLI_LINE 200
#endif

// ---- reset button ---------------------------------------------------------------------------
// Pin of the reset button, active low. -1 when the board has none.
#ifndef LMP_RESET_PIN
#define LMP_RESET_PIN -1
#endif
// Hold time for a factory reset, in milliseconds.
#ifndef LMP_RESET_HOLD_MS
#define LMP_RESET_HOLD_MS 10000
#endif

// ---- board ----------------------------------------------------------------------------------
#ifndef LMP_BOARD
#define LMP_BOARD "unknown"
#endif
// Code of the kind of device, given by the module that makes the product. Empty without one.
#ifndef LMP_DEVICE_CODE
#define LMP_DEVICE_CODE ""
#endif
#ifndef LMP_SERIAL_BAUD
#define LMP_SERIAL_BAUD 115200
#endif
// Transmit buffer of the console, in bytes. An answer that fits leaves without holding the
// system; a longer one holds it while the rest goes out.
#ifndef LMP_SERIAL_TX_BUFFER
#define LMP_SERIAL_TX_BUFFER 4096
#endif
// Console on the USB port of the chip: longest wait for a host that does not read, in
// milliseconds. After it the output is kept in the buffer, oldest bytes dropped first.
#ifndef LMP_SERIAL_TX_TIMEOUT_MS
#define LMP_SERIAL_TX_TIMEOUT_MS 10
#endif
// ---- network and web ------------------------------------------------------------------------
// 1 when the build has the services sys.net and sys.web. 0 leaves the device with the serial
// port only.
#ifndef LMP_FEATURE_NET
#define LMP_FEATURE_NET 1
#endif
// The name <host>.local on the network (mDNS). The light edition leaves it out: it costs RAM
// the ESP8266 needs, and the address and the captive portal of the access point stay.
#ifndef LMP_FEATURE_MDNS
#define LMP_FEATURE_MDNS (!LMP_LIGHT)
#endif
#ifndef LMP_HTTP_PORT
#define LMP_HTTP_PORT 80
#endif
// Longest body of a request, in bytes: the arguments of a command.
#ifndef LMP_HTTP_BODY_MAX
#define LMP_HTTP_BODY_MAX 1024
#endif
// Networks kept after a scan, strongest first. They are part of the state of the service, so
// of every report: fewer where the answers are short.
#ifndef LMP_NET_SCAN_MAX
#if LMP_TIER <= 1
#define LMP_NET_SCAN_MAX 8
#else
#define LMP_NET_SCAN_MAX 16
#endif
#endif
// The other devices of the system kept after a search of the network. They are part
// of the state of the net service, so of every report.
#ifndef LMP_NET_NEAR_MAX
#define LMP_NET_NEAR_MAX 12
#endif
// How often the device looks for them by itself, in minutes. The first time is a little after
// it joined the network.
#ifndef LMP_NET_NEAR_EVERY_MIN
#define LMP_NET_NEAR_EVERY_MIN 360
#endif
// The version of the API of the device: in /api/state, in X-LMP-Api, in what the device
// announces on the network.
#define LMP_API_VERSION 2
// Highest transmit power of the Wi-Fi, in quarters of dBm (84 = 21 dBm). 0 leaves the one of
// the chip. Lower on the boards whose regulator does not hold the peaks of the radio.
#ifndef LMP_WIFI_MAX_TX_QDBM
#define LMP_WIFI_MAX_TX_QDBM 0
#endif
// The buffers of the Wi-Fi taken when needed, to send and to receive (ESP32 family). Fewer keep a
// burst of answers of a busy network in less memory. The same for every module; a module that
// needs fewer asks for them in its module.json, "tune", on the boards where it needs them:
// ExpNet on the C3.
#ifndef LMP_WIFI_DYNAMIC_TX_BUFFERS
#define LMP_WIFI_DYNAMIC_TX_BUFFERS 32
#endif
#ifndef LMP_WIFI_DYNAMIC_RX_BUFFERS
#define LMP_WIFI_DYNAMIC_RX_BUFFERS 32
#endif
// Channel of the access point. 0: one of 1, 6 and 11, chosen from the address of the device.
// With a client of a network connected the access point follows the channel of that network.
#ifndef LMP_NET_AP_CHANNEL
#define LMP_NET_AP_CHANNEL 0
#endif
// Attempts to reach the configured network before the access point comes up as a fallback.
#ifndef LMP_NET_FALLBACK_AFTER
#define LMP_NET_FALLBACK_AFTER 5
#endif
// In fallback the device tries the configured network again at this interval, in seconds.
// Each attempt disturbs who is connected to the access point: not too often.
#ifndef LMP_NET_FALLBACK_RETRY_SEC
#define LMP_NET_FALLBACK_RETRY_SEC 120
#endif
// A device on the network that has lost it for these minutes, with nobody on its access point,
// restarts: a radio that stopped answering comes back only so. After a restart for the same
// reason it waits LMP_NET_GUARD_AGAIN_MIN: every restart drops the relays for a moment. 0: off.
#ifndef LMP_NET_GUARD_MIN
#define LMP_NET_GUARD_MIN 15
#endif
#ifndef LMP_NET_GUARD_AGAIN_MIN
#define LMP_NET_GUARD_AGAIN_MIN 60
#endif
// The ways to the network the device looks for at start, besides its Wi-Fi: a sum of the values
// below. The same firmware runs with and without them: what answers is used, in this order: the
// cable when it has an address, the Wi-Fi when a network is configured, the modem last. The
// modem has its place kept and is not written yet: asking for it stops the build.
#define LMP_NET_LINK_ETH 1
#define LMP_NET_LINK_CELL 2
#ifndef LMP_NET_LINKS
#define LMP_NET_LINKS 0
#endif
#if (LMP_NET_LINKS) & LMP_NET_LINK_CELL
#error "LMP_NET_LINK_CELL: the link through a modem is not written yet"
#endif
// The cable: a W5500 on SPI, on a bus of its own. -1: not wired; SCK, MOSI, MISO and CS are
// required. Without the interrupt the driver asks the chip every LMP_ETH_POLL_MS.
#ifndef LMP_ETH_PIN_SCK
#define LMP_ETH_PIN_SCK -1
#endif
#ifndef LMP_ETH_PIN_MOSI
#define LMP_ETH_PIN_MOSI -1
#endif
#ifndef LMP_ETH_PIN_MISO
#define LMP_ETH_PIN_MISO -1
#endif
#ifndef LMP_ETH_PIN_CS
#define LMP_ETH_PIN_CS -1
#endif
#ifndef LMP_ETH_SPI_MHZ
#define LMP_ETH_SPI_MHZ 20
#endif
#ifndef LMP_ETH_PIN_INT
#define LMP_ETH_PIN_INT -1
#endif
#ifndef LMP_ETH_PIN_RST
#define LMP_ETH_PIN_RST -1
#endif
#ifndef LMP_ETH_POLL_MS
#define LMP_ETH_POLL_MS 10
#endif
// Seconds a cable may stay without an address before the device turns to its Wi-Fi, or to the
// access point as a fallback when no network is configured. The cable is still waited for, and
// it is taken back as soon as it has an address.
#ifndef LMP_NET_LINK_WAIT_SEC
#define LMP_NET_LINK_WAIT_SEC 30
#endif
// Wrong passwords accepted from one address before it has to wait.
#ifndef LMP_WEB_LOGIN_ATTEMPTS
#define LMP_WEB_LOGIN_ATTEMPTS 5
#endif
// First wait after too many wrong passwords, in seconds. It doubles at every new failure.
#ifndef LMP_WEB_LOGIN_LOCK_SEC
#define LMP_WEB_LOGIN_LOCK_SEC 30
#endif
// Connections the server of the ESP32 family keeps open at the same time, events included.
// When they are all taken, a new one closes the one silent for longest.
#ifndef LMP_WEB_MAX_SOCKETS
#define LMP_WEB_MAX_SOCKETS 7
#endif
// How many requests the console of the device sends together: said in /api/state, and the page
// keeps the others waiting. 0: all of them, as a browser does (the dozen of a page that opens).
// For a module short of memory on a board (tune): each request travelling is memory.
#ifndef LMP_WEB_LANES
#define LMP_WEB_LANES 0
#endif
// Clients that can follow the events at the same time.
#ifndef LMP_WEB_EVENT_CLIENTS
#if LMP_LIGHT
#define LMP_WEB_EVENT_CLIENTS 1
#else
#define LMP_WEB_EVENT_CLIENTS LMP_MAX_SESSIONS
#endif
#endif
// Events waiting to leave, and longest event in bytes.
#ifndef LMP_WEB_EVENT_QUEUE
#define LMP_WEB_EVENT_QUEUE 8
#endif
#ifndef LMP_WEB_EVENT_LEN
#define LMP_WEB_EVENT_LEN (LMP_LOG_MSG_LEN + 96)
#endif

// ---- services of the network ------------------------------------------------------------------
// Each one can be left out of a build: 1 in, 0 out. What needs a secure connection is out
// of the smallest boards, which have no memory for it. There the client of web services comes
// in when a module asks for it, for plain HTTP only.
#ifndef LMP_FEATURE_HTTPCLIENT
#if defined(LMP_NEEDS_HTTPCLIENT)
#define LMP_FEATURE_HTTPCLIENT LMP_FEATURE_NET
#else
#define LMP_FEATURE_HTTPCLIENT (LMP_FEATURE_NET && LMP_TIER >= 2)
#endif
#endif
#ifndef LMP_FEATURE_MQTT
#define LMP_FEATURE_MQTT (LMP_FEATURE_NET && LMP_TIER >= 2)
#endif
// Firmware update: from a file sent by the console, and from an address when the build has
// the client for the requests. The smallest boards have neither way in.
#ifndef LMP_FEATURE_OTA
#define LMP_FEATURE_OTA (LMP_FEATURE_NET && LMP_TIER >= 2)
#endif
// Where the device is. Out of the smallest boards: on those every text of the firmware
// takes memory, and this service costs a kilobyte and a half of it.
#ifndef LMP_FEATURE_GEO
#define LMP_FEATURE_GEO (LMP_FEATURE_NET && LMP_TIER >= 2)
#endif
// The server asked for the position when the device reaches Internet: a public service,
// free, without a key, commercial use allowed, 1000 requests a day for each address. An
// empty lookupUrl in the settings asks nobody.
#ifndef LMP_GEO_LOOKUP_URL
#define LMP_GEO_LOOKUP_URL "https://ipwho.is/"
#endif
// 1: ipwho.is asked without the secure connection, the same answer in http. The secure one takes
// about 50 KB for a second at every start: a module that has not that memory on a board asks
// for it in its module.json, "tune" (ExpNet on the C3).
#ifndef LMP_GEO_HTTP
#define LMP_GEO_HTTP 0
#endif
// A GPS moves the saved position only past this distance in degrees, about 50 m, and saves
// it at most once in this time: a fix every second must not wear the flash.
#ifndef LMP_GEO_GPS_STEP
#define LMP_GEO_GPS_STEP 0.0005
#endif
#ifndef LMP_GEO_GPS_SAVE_MS
#define LMP_GEO_GPS_SAVE_MS 600000u
#endif
// Buses for the hardware outside the board.
// In the light edition only when a module of the firmware needs it: modules.py defines
// LMP_NEEDS_BUS for a module with "bus" in the "requires" of its module.json.
// The cloud, public part: waits for a bridge and does nothing without one. docs/cloud.md.
#ifndef LMP_FEATURE_CLOUD
#define LMP_FEATURE_CLOUD (LMP_FEATURE_NET && LMP_TIER >= 2)
#endif
// Requests of the server waiting for the application task, and the longest one in bytes.
#ifndef LMP_CLOUD_INBOX
#define LMP_CLOUD_INBOX 2
#endif
#ifndef LMP_CLOUD_IN_MAX
#define LMP_CLOUD_IN_MAX 1024
#endif
// Log entries pushed to the server between two rounds; commands followed until they end.
// Room for one envelope to the server: an answer of the console and the frame around it.
#ifndef LMP_CLOUD_OUT_MAX
#define LMP_CLOUD_OUT_MAX (LMP_HTTP_RESP_MAX + 256)
#endif
#ifndef LMP_CLOUD_LOG_QUEUE
#define LMP_CLOUD_LOG_QUEUE 4
#endif
// Events of the device waiting for the next envelope to the server: the ones the console
// follows, one envelope for each round.
#ifndef LMP_CLOUD_EVENT_QUEUE
#define LMP_CLOUD_EVENT_QUEUE 8
#endif
#ifndef LMP_CLOUD_WAITING
#define LMP_CLOUD_WAITING 4
#endif
// Changes close together become one envelope: the partial report after 2 s, the data of a
// module after half a second.
#ifndef LMP_CLOUD_PARTIAL_MS
#define LMP_CLOUD_PARTIAL_MS 2000
#endif
#ifndef LMP_CLOUD_DATA_MS
#define LMP_CLOUD_DATA_MS 500
#endif
// Questions to the network: pings, ports, ARP, mDNS, SSDP, the packet that wakes a PC. For the
// tools of a network (ExpNet) and for the diagnosis of any board. Not in the light
// edition: those boards have no memory for it.
#ifndef LMP_FEATURE_PROBE
#define LMP_FEATURE_PROBE (LMP_FEATURE_NET && LMP_TIER >= 2 && !LMP_LIGHT)
#endif
// Questions waiting for their turn: one travels at a time, on the task of the service.
#ifndef LMP_PROBE_QUEUE
#define LMP_PROBE_QUEUE 8
#endif
// The stack of the task that asks: the ping has a task of its own, the rest is small.
#ifndef LMP_PROBE_STACK
#define LMP_PROBE_STACK 4608
#endif
// Most services or devices kept for one search with mDNS or SSDP.
#ifndef LMP_PROBE_HITS
#define LMP_PROBE_HITS (LMP_MODULE_MEM >= 3 ? 96 : LMP_MODULE_MEM == 2 ? 48 : 24)
#endif
// TCP connections opened together to try many ports of a device: the sockets of lwIP are few,
// and the web server and the other services keep theirs.
#ifndef LMP_PROBE_TCP_TOGETHER
#define LMP_PROBE_TCP_TOGETHER (LMP_MODULE_MEM >= 3 ? 8 : LMP_MODULE_MEM == 2 ? 6 : 4)
#endif

#ifndef LMP_FEATURE_BUS
#if LMP_LIGHT && !defined(LMP_NEEDS_BUS)
#define LMP_FEATURE_BUS 0
#else
#define LMP_FEATURE_BUS 1
#endif
#endif

// Requests to servers waiting for their turn: one travels at a time.
#ifndef LMP_HTTPC_QUEUE
#define LMP_HTTPC_QUEUE 4
#endif
// Longest wait for an answer, in milliseconds, when who asks does not say.
#ifndef LMP_HTTPC_TIMEOUT_MS
#define LMP_HTTPC_TIMEOUT_MS 10000
#endif
// The check that Internet is reachable: seconds between two of them while it is, and while
// it is not.
#ifndef LMP_NET_PROBE_SEC
#define LMP_NET_PROBE_SEC 300
#endif
#ifndef LMP_NET_PROBE_RETRY_SEC
#define LMP_NET_PROBE_RETRY_SEC 60
#endif
// A new firmware is confirmed by itself after it ran this long, in seconds. A firmware that
// restarts before is given up at the next start, where the board can go back. 0: only the
// command confirms.
#ifndef LMP_OTA_CONFIRM_SEC
#define LMP_OTA_CONFIRM_SEC 60
#endif
// Longest message of the broker the system reads, topic and content, in bytes.
#ifndef LMP_MQTT_TOPIC_MAX
#define LMP_MQTT_TOPIC_MAX 96
#endif
// The largest message the MQTT service writes: an answer to a command, the state of a module.
// As large as an answer of the web for everybody; a module short of memory on a board asks for
// less (tune), and a longer answer is then not sent.
#ifndef LMP_MQTT_OUT_MAX
#define LMP_MQTT_OUT_MAX LMP_HTTP_RESP_MAX
#endif
// The stack of the task of an MQTT client that is not secure, in bytes: the broker of the
// house, of Home Assistant. 0: the one of the driver, which is sized for a secure connection.
// For a module short of memory on a board (tune).
#ifndef LMP_MQTT_PLAIN_STACK
#define LMP_MQTT_PLAIN_STACK 0
#endif
#ifndef LMP_MQTT_PAYLOAD_MAX
#define LMP_MQTT_PAYLOAD_MAX 512
#endif
// Messages received and waiting for the round of the system.
#ifndef LMP_MQTT_INBOX
#define LMP_MQTT_INBOX 4
#endif
// States the bridge towards Home Assistant remembers, to tell only what changed: one for
// each part of a module with things, and one for each light. A hub with 64 nodes has 65.
#ifndef LMP_HOME_STATES
#define LMP_HOME_STATES 96
#endif
// Parts of the system that can ask for the messages of a topic.
// One is of the bridge towards Home Assistant, the others are for the modules.
#ifndef LMP_MQTT_LISTENERS
#define LMP_MQTT_LISTENERS 5
#endif

// ---- modules ----------------------------------------------------------------------------------
// 1 when the build has the service that hosts the modules.
#ifndef LMP_FEATURE_MODULES
#define LMP_FEATURE_MODULES 1
#endif
// Longest file of settings of a module, in bytes: names, levels, schedules.
#ifndef LMP_MODULE_CFG_MAX
#if LMP_TIER <= 1
#define LMP_MODULE_CFG_MAX 2048
#else
#define LMP_MODULE_CFG_MAX 4096
#endif
#endif
// Changes of the state of a module closer than this, in milliseconds, are told once.
#ifndef LMP_MODULE_DATA_DELAY_MS
#define LMP_MODULE_DATA_DELAY_MS 200
#endif
// The things of the modules as data, for the server through the bridge and for who asks
// (sdk/things.h): in the builds that have somebody to tell them to.
#ifndef LMP_FEATURE_THINGS
#define LMP_FEATURE_THINGS (LMP_FEATURE_MODULES && (LMP_FEATURE_CLOUD || LMP_FEATURE_MQTT))
#endif
// Parts of a module whose changes are followed one by one: the nodes of a hub.
#ifndef LMP_THINGS_PARTS
#define LMP_THINGS_PARTS 64
#endif
// Measures kept while the server of the device is away, to give them when it is back: 20 bytes
// each, taken at the first one and given back when the server has them all. With a server and
// nothing else: a firmware without one keeps none. 0: none is kept.
#ifndef LMP_THINGS_KEPT
#define LMP_THINGS_KEPT 64
#endif

// ---- Service of the product -----------------------------------------------------------------
// A firmware can bring one more service, written outside the system, that can stop the
// modules of the device (kernel/service/product.h). Its environment names the function that
// gives it: -DLMP_PRODUCT_SERVICE=<function>. Not defined: no such service, as in every
// firmware of this repository.

// ---- LoRa radio -----------------------------------------------------------------------------
// 1 when the board has a LoRa radio: the service sys.lora is part of the build.
// Band, chip and pins come from the environment of the board: LMP_LORA_BAND, LMP_LORA_CHIP,
// LMP_LORA_PIN_CS, _SCK, _MOSI, _MISO, _RST, _BUSY, _DIO1, LMP_LORA_TCXO_MV,
// LMP_LORA_DIO2_RF_SWITCH.
#ifndef LMP_FEATURE_LORA
#define LMP_FEATURE_LORA 0
#endif
// Longest packet, in bytes. The radio chips stop at 255.
#ifndef LMP_LORA_MAX_PAYLOAD
#define LMP_LORA_MAX_PAYLOAD 255
#endif
// Received packets kept for who reads the state later.
#ifndef LMP_LORA_INBOX
#define LMP_LORA_INBOX 8
#endif
// Parts of the system that can ask to be called for every packet.
#ifndef LMP_LORA_LISTENERS
#define LMP_LORA_LISTENERS 4
#endif

#ifndef LMP_HAL_I2C_MAX
#define LMP_HAL_I2C_MAX 2
#endif
#ifndef LMP_HAL_UART_MAX
#define LMP_HAL_UART_MAX 2
#endif
