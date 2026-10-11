// LoopMaxPro - ExpNet, the free edition of Networks Explorer: the bench of a network.
//   Network:  who is in the network, kept on file and searched again by itself every quarter of
//             an hour: address, MAC, name, services, the LoopMaxPro devices; a device never seen
//             before is told
//   Live:     the time of the answers of the router and of Internet, the lost ones, the signal,
//             the traffic of the router
//   Monitor:  the services of the network, checked every minute: up or down, time, 7 days of
//             history, events
//   Wake:     the PC to switch on with Wake-on-LAN, whether they are on, a webhook to turn off
//   Telegram: a message when something happens, a summary in the morning
// The tools (ping, trace, ports, name servers, HTTP), the commands of the chat, the speed, MQTT
// and the test endpoints are of the full version: the page shows them, and says where
// they are.
// The questions to the network are of sys.probe; the module decides what to ask and when, and
// keeps what the answers say. Its interface is ui/expnet.js.
#include "lmp_config.h"

// The sources of every module are compiled in every build: this one needs the questions to the
// network and the client of web services, which the light edition has not.
#if LMP_FEATURE_PROBE && LMP_FEATURE_HTTPCLIENT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "hal/api/memory.h"
#include "history.h"
#include "kernel/kernel.h"
#include "sdk/home.h"
#include "sdk/module.h"
#include "sdk/state.h"
#include "services/clock/clock_service.h"
#include "services/cloud/cloud_service.h"
#include "services/httpclient/httpclient_service.h"
#include "services/mqtt/mqtt_service.h"
#include "services/net/net_service.h"
#include "services/probe/probe_service.h"
#include "services/storage/storage_service.h"
#include "wan.h"

// The extended table of the makers, on the boards with room for it: not on the C3.
//
#if LMP_MODULE_MEM >= 2
#include "makers.h"
#endif

namespace expnet {

struct Cfg {
  lmp::FixedString<64> telegramToken;   // the token of the bot: a secret
  lmp::FixedString<24> telegramChat;    // where the messages go, and the only chat obeyed
  bool botCommands;                     // the commands of the chat: of the full version, locked off
  bool notify;                          // a message when a service falls and when it is back
  bool notifyNew;                       // a message for a device never seen before
  bool notifyPc;                        // a message when a PC is switched on or off
  bool notifyNet;                       // a message when Internet is back, and how long it was gone
  uint8_t summaryHour;                  // a summary every morning at this hour; 24: none
  uint8_t quietFrom;                    // from this hour messages come without a sound; 24: never
  uint8_t quietTo;                      // to this hour
  uint16_t scanEveryMin;                // the search of the network; 0: only by hand
  uint16_t everySec;                    // how often a new service is checked
  uint8_t trafficFrom;                  // who tells the traffic: 0 found, 1 UPnP, 2 Starlink, 3 Huawei, 4 SNMP
  lmp::FixedString<24> upnpHost;        // its address, with ":port" when it is not the usual one; empty: found
  lmp::FixedString<24> snmpCommunity;   // the word a router with SNMP wants; empty: SNMP is never asked
  uint32_t snmpIf;                      // the port of that router towards Internet; 0: found
};

}  // namespace expnet

LMP_FIELDS(expnet::Cfg,
  LMP_F(telegramToken, "telegramToken", Str(0, 64, "").secret()),
  LMP_F(telegramChat,  "telegramChat",  Str(0, 24, "")),
  LMP_F(botCommands,   "botCommands",   Bool(false).locked()),
  LMP_F(notify,        "notify",        Bool(true)),
  LMP_F(notifyNew,     "notifyNew",     Bool(true)),
  LMP_F(notifyPc,      "notifyPc",      Bool(false)),
  LMP_F(notifyNet,     "notifyNet",     Bool(true)),
  LMP_F(summaryHour,   "summaryHour",   U8(0, 24, 24)),
  LMP_F(quietFrom,     "quietFrom",     U8(0, 24, 24)),
  LMP_F(quietTo,       "quietTo",       U8(0, 23, 7)),
  LMP_F(scanEveryMin,  "scanEveryMin",  U16(0, 1440, 15)),
  LMP_F(everySec,      "everySec",      U16(10, 3600, 60)),
  LMP_F(trafficFrom,   "trafficFrom",   U8(0, 4, 0)),
  LMP_F(upnpHost,      "upnpHost",      Str(0, 21, "")),
  // Not a secret: it must be seen, and emptied again, which the form does not do with one.
  LMP_F(snmpCommunity, "snmpCommunity", Str(0, 24, "")),
  LMP_F(snmpIf,        "snmpIf",        U32(0, 4294967295.0, 0)))

namespace expnet {
namespace {

using lmp::Status;
using lmp::hal::Ip4;

// What ExpNet keeps and how much it asks, by the memory of the board (LMP_MODULE_MEM):
// the C3 as it was measured on the bench, the boards with PSRAM much higher, the
// medium ones in between. The large buffers go to the PSRAM where there is one.
#if LMP_MODULE_MEM >= 3
constexpr uint8_t kServicesMax = 32;
constexpr uint8_t kPcsMax = 32;
constexpr uint16_t kEventsMax = 1000;
constexpr uint16_t kHostsMax = 512;
constexpr uint8_t kLiveSlots = 240;
constexpr uint32_t kLiveEveryMs = 250;
constexpr uint8_t kRouterSlots = 60;
constexpr uint32_t kRouterEveryMs = 1000;
constexpr uint8_t kOutbox = 8;
constexpr size_t kMessageCap = 3500;
#elif LMP_MODULE_MEM == 2
constexpr uint8_t kServicesMax = 24;
constexpr uint8_t kPcsMax = 24;
constexpr uint16_t kEventsMax = 200;
constexpr uint16_t kHostsMax = 254;
constexpr uint8_t kLiveSlots = 120;
constexpr uint32_t kLiveEveryMs = 500;
constexpr uint8_t kRouterSlots = 40;
constexpr uint32_t kRouterEveryMs = 1500;
constexpr uint8_t kOutbox = 5;
constexpr size_t kMessageCap = 1500;
#else
constexpr uint8_t kServicesMax = 16;
constexpr uint8_t kPcsMax = 16;
constexpr uint16_t kEventsMax = 64;
constexpr uint16_t kHostsMax = 128;
constexpr uint8_t kLiveSlots = 60;
constexpr uint32_t kLiveEveryMs = 1000;
constexpr uint8_t kRouterSlots = 30;
constexpr uint32_t kRouterEveryMs = 2000;
constexpr uint8_t kOutbox = 3;
constexpr size_t kMessageCap = 640;
#endif
constexpr uint8_t kHostsPage = 16;
constexpr uint32_t kTickMs = 200;           // the module is ticked this often
constexpr uint32_t kEventsWriteMs = 10000;  // the register goes to its file at most this often
constexpr uint8_t kEventsPage = 20;
constexpr uint8_t kDownAfter = 2;           // checks failed in a row before a service is down
constexpr uint32_t kPcEveryMs = 60000;
constexpr uint32_t kWakingEveryMs = 5000;
constexpr uint32_t kWakingMs = 180000;      // how long a PC is waited for, after the packet
constexpr uint32_t kPingWaitMs = 1000;
constexpr uint32_t kTcpWaitMs = 2000;
constexpr uint32_t kHttpWaitMs = 5000;
constexpr uint32_t kSweepWaitMs = 300;      // ARP: the wait of each group of addresses
constexpr uint32_t kScanPingMs = 300;
constexpr uint32_t kMdnsWaitMs = 1200;
constexpr uint32_t kSsdpWaitMs = 3000;
constexpr uint32_t kSsdpKindWaitMs = 2500;  // a search of one kind: the answers come within 2 s
constexpr uint32_t kVideoWaitMs = 500;      // the ports of cameras, all together, of one device
constexpr size_t kMakersPage = 3000;        // the prefixes of a page of the command makers, at most
constexpr uint32_t kFirstScanMs = 60000;    // the first search, a minute after the network
constexpr uint32_t kCacheKeepMs = 60000;    // the list in memory, a minute after the last look
constexpr uint32_t kLiveKeepMs = 15000;     // the live times, while somebody looks at them
constexpr uint32_t kIdleMs = 1800000;      // nobody uses the module this long: the search may rest
constexpr uint32_t kDaySec = 86400;
constexpr const char* kListFile = "/expnet/list.json";
constexpr const char* kEventsFile = "/expnet/log.bin";
constexpr const char* kHostsFile = "/expnet/hosts.bin";
constexpr const char* kCountFile = "/expnet/count.bin";
constexpr uint8_t kHostsStep = 32;          // the list grows by this many places
constexpr size_t kListMax = 6144;
constexpr uint32_t kRouterRetryMs = 60000;  // a router without UPnP is asked again a minute later
constexpr const char* kGatewayKind = "urn:schemas-upnp-org:device:InternetGatewayDevice:1";
// The questions to the router: its four counters, and the one about the line.
const char* const kRouterActions[] = {"GetTotalBytesReceived", "GetTotalBytesSent", "GetTotalPacketsReceived",
                                      "GetTotalPacketsSent", "GetCommonLinkProperties"};
const char* const kRouterTags[] = {"NewTotalBytesReceived", "NewTotalBytesSent", "NewTotalPacketsReceived",
                                   "NewTotalPacketsSent"};
constexpr uint8_t kRouterLine = 4;
// A round asks two counters: for each way the bytes, or the packets when the byte counter is stuck
// at its top. All four and the line once a minute. Four questions every round were too many for the
// router of a house, which stopped answering UPnP while the board asked them every second.
constexpr uint32_t kRouterFullMs = 60000;
// A router that counts the bytes received in a signed number stops at its top: then the bytes
// are told by the packets, about this many each (a download fills them).
constexpr uint32_t kBytesPerPacket = 1400;
// The same for the bytes sent (seen on a home router), less what a download makes
// the router send: a small acknowledgement, of about this many bytes, for two packets received.
constexpr uint32_t kAckBytes = 60;
// A counter at its top, as some routers have, signed or not: it stays there.
bool atTop(uint32_t value) {
  return value == 0x7FFFFFFFu || value == 0xFFFFFFFFu;
}

// The value of a header in an answer of HTTP or SSDP, whatever the case of its name.
bool headerOf(const char* text, const char* name, char* out, size_t cap) {
  const size_t n = strlen(name);
  for (const char* line = text; line && *line;) {
    size_t i = 0;
    while (i < n && line[i] && (line[i] | 0x20) == (name[i] | 0x20)) ++i;
    if (i == n && line[n] == ':') {
      const char* value = line + n + 1;
      while (*value == ' ' || *value == '\t') ++value;
      size_t len = 0;
      while (value[len] && value[len] != '\r' && value[len] != '\n') ++len;
      if (!len || len >= cap) return false;
      memcpy(out, value, len);
      out[len] = '\0';
      return true;
    }
    line = strchr(line, '\n');
    if (line) ++line;
  }
  return false;
}

// The kinds of service looked for with mDNS, and the word each one gives to a device.
struct MdnsKind {
  const char* service;
  const char* word;
};
constexpr MdnsKind kMdnsKinds[] = {
    {"_http", "web"},         {"_ipp", "printer"},       {"_googlecast", "cast"},
    {"_hap", "homekit"},      {"_home-assistant", "homeassistant"}, {"_smb", "files"},
    {"_ssh", "ssh"},          {"_mqtt", "mqtt"},          {"_esphomelib", "esphome"},
    {"_airplay", "airplay"},
};
constexpr uint8_t kMdnsCount = sizeof(kMdnsKinds) / sizeof(kMdnsKinds[0]);
// What a device is, asked with SSDP one kind at a time after the search of everybody: the word
// each kind gives to a device. Their bits follow the ones of mDNS in the kinds of a device.
struct SsdpKind {
  const char* st;
  const char* word;
};
constexpr SsdpKind kSsdpKinds[] = {
    {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "igd"},  // a router
    {"urn:schemas-wifialliance-org:device:WFADevice:1", "wps"},      // an access point, a repeater
    {"urn:dial-multiscreen-org:service:dial:1", "dial"},             // a television, a stick
    {"urn:schemas-upnp-org:device:MediaRenderer:1", "media"},        // televisions and speakers
};
constexpr uint8_t kSsdpCount = sizeof(kSsdpKinds) / sizeof(kSsdpKinds[0]);
static_assert(kMdnsCount + kSsdpCount <= 16, "the kinds of a device are 16 bits");

// The types the owner can give a device, as the page names them. A device keeps 1 + the place
// of its type here; 0, the page guesses it.
constexpr const char* kTypes[] = {"router", "ap", "pc", "phone", "camera", "nvr", "tv",
                                  "speaker", "printer", "nas", "server", "iot", "other"};
constexpr uint8_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);
uint8_t typeCode(const char* word) {
  for (uint8_t t = 0; t < kTypeCount; ++t) {
    if (strcmp(kTypes[t], word) == 0) return static_cast<uint8_t>(t + 1);
  }
  return 0;
}

// The ports typical of cameras and recorders, asked to every device at the end of a search: one
// bit each in the ports of a device. RTSP is the video of any of them; the others say the
// platform (Hikvision, Dahua, Xiongmai, on which many brands are made) and, for Hikvision, a
// recorder (9010, 9020) or a camera (7681).
constexpr uint16_t kVideoPorts[] = {554, 8000, 37777, 34567, 9010, 9020, 7681};
constexpr uint8_t kVideoCount = sizeof(kVideoPorts) / sizeof(kVideoPorts[0]);
static_assert(kVideoCount <= 16, "the ports of a device are 16 bits");
// On the boards of the least memory (the C3) the search asks less: no SSDP of one kind, no ports
// of cameras. The answers of a network come in bursts, and there a burst takes the memory of
// Wi-Fi.
constexpr uint8_t kSsdpAsked = LMP_MODULE_MEM >= 2 ? kSsdpCount : 0;
constexpr bool kVideoAsked = LMP_MODULE_MEM >= 2;   // on the C3 the 160 connections take it off the network too

uint16_t kindBit(const char* word) {
  for (uint8_t k = 0; k < kMdnsCount; ++k) {
    if (strcmp(kMdnsKinds[k].word, word) == 0) return static_cast<uint16_t>(1u << k);
  }
  return 0;
}

enum class Check : uint8_t { Ping, Tcp, Http };
const char* checkName(Check c) {
  switch (c) {
    case Check::Ping: return "ping";
    case Check::Tcp:  return "tcp";
    case Check::Http: return "http";
  }
  return "ping";
}
bool checkOf(const char* text, Check& out) {
  if (strcmp(text, "ping") == 0) out = Check::Ping;
  else if (strcmp(text, "tcp") == 0) out = Check::Tcp;
  else if (strcmp(text, "http") == 0) out = Check::Http;
  else return false;
  return true;
}

struct Service {
  uint8_t id = 0;              // 0: a free place
  char name[24] = "";
  Check check = Check::Ping;
  char host[40] = "";          // an address; for http also a name
  Ip4 ip;                      // ping, tcp: the address of host
  uint16_t port = 0;
  char path[40] = "/";
  uint16_t everySec = 60;
  // What is going on: not kept.
  bool known = false, up = false, asking = false;
  uint8_t fails = 0;
  uint32_t nextMs = 0, sinceMs = 0, lastMs = 0;
  Filling filling;
  uint32_t fillingBlock = 0;
  bool historyDue = false;     // a closed block waits to be written
  Block closed;
  uint32_t closedBlock = 0;
  int16_t up24 = -1, up7 = -1; // per thousand; -1: nothing known
  uint16_t avg24 = 0;
  bool summaryRead = false;    // the file was read for them, at the start
};

struct Pc {
  uint8_t id = 0;
  char name[24] = "";
  uint8_t mac[6] = {};
  char host[16] = "";          // for the state; empty: none
  Ip4 ip;
  char broadcast[16] = "";     // empty: the one of the network
  uint16_t port = 0;           // 0: ping tells whether it is on; else this TCP port
  char offUrl[96] = "";        // asked to turn it off; empty: it cannot be turned off
  // What is going on: not kept.
  bool known = false, on = false, asking = false;
  uint32_t nextMs = 0, sinceMs = 0;
  uint32_t wakeMs = 0;         // when the packet went; 0: not waking
  uint32_t wokeAfterMs = 0;    // how long it took, the last time
  uint32_t offRef = 0;         // the command that waits for the webhook
};

// What happened, for the register of the home and for the messages.
enum class Ev : uint8_t { None, ServiceDown, ServiceUp, NewDevice, PcOn, PcOff, NetDown, NetUp };
const char* evName(uint8_t kind) {
  switch (static_cast<Ev>(kind)) {
    case Ev::ServiceDown: return "down";
    case Ev::ServiceUp:   return "up";
    case Ev::NewDevice:   return "new";
    case Ev::PcOn:        return "pcOn";
    case Ev::PcOff:       return "pcOff";
    case Ev::NetDown:     return "netDown";
    case Ev::NetUp:       return "netUp";
    case Ev::None:        break;
  }
  return "?";
}

struct Event {
  uint32_t unix = 0;           // 0: the time was not known
  uint8_t kind = 0;
  uint8_t id = 0;              // the service or the PC
  uint16_t minutes = 0;        // back after this long
  uint8_t ip[4] = {};
  uint8_t mac[6] = {};
  char name[22] = "";
};

// A device of the network as the file keeps it: what the searches said of it.
struct Host {
  Ip4 ip;
  uint8_t mac[6] = {};
  int16_t pingMs = -1;         // the last search: -1 no answer to the ping
  uint16_t kinds = 0;          // the services found with mDNS, one bit for each of kMdnsKinds
  uint8_t flags = 0;
  uint8_t type = 0;             // the type the owner gave it: 1 + its place in kTypes; 0, guessed
  uint32_t firstSeen = 0;      // the time of the first search that found it; 0 not known
  uint32_t lastSeen = 0;       // the time of the last search that found it
  char name[28] = "";          // from mDNS or SSDP
  char alias[20] = "";         // the name the owner gave it
  char server[28] = "";        // what it says of itself to SSDP
  uint16_t ports = 0;          // the ports of kVideoPorts found open, one bit each
  uint16_t webPort = 0;        // its web page, from mDNS: the port of _http, else of Home Assistant
};
constexpr uint8_t kOnline = 1;     // the last search found it
constexpr uint8_t kSelf = 2;       // the device itself
constexpr uint8_t kFromStart = 4;  // there at the first search ever: never "new"
constexpr uint8_t kSeenNow = 8;    // found by the search that runs
constexpr uint8_t kNewNow = 16;    // never seen before this search
constexpr uint8_t kSystem = 32;    // a LoopMaxPro device when last seen: said even out of sight
constexpr uint8_t kProduct = 64;   // and a product, with its private modules

struct HostsHead {
  char magic[4] = {'D', 'B', 'I', '3'};   // DBI1 until 1.0.0, DBI2 in the first tries of 1.0.1
  uint16_t count = 0;
  uint8_t baseline = 0;        // 1: the first search ever is done
  uint8_t spare = 0;
  uint32_t scanUnix = 0;
};

// What the page and the chat show without the list in memory: kept apart, a few bytes.
struct Counts {
  uint16_t known = 0, online = 0, fresh = 0, spare = 0;
  uint32_t scanUnix = 0;
};

enum class Scan : uint8_t { Idle, Sweep, Ping, Mdns, Ssdp, Ports, Done };
const char* scanName(Scan s) {
  switch (s) {
    case Scan::Idle:  return "idle";
    case Scan::Done:  return "done";
    default:          return "running";
  }
}
const char* stepName(Scan s) {
  switch (s) {
    case Scan::Sweep: return "arp";
    case Scan::Ping:  return "ping";
    case Scan::Mdns:  return "mdns";
    case Scan::Ssdp:  return "ssdp";
    case Scan::Ports: return "ports";
    default:          return "";
  }
}

uint32_t key(const Ip4& ip) {
  return (static_cast<uint32_t>(ip.b[0]) << 24) | (static_cast<uint32_t>(ip.b[1]) << 16) |
         (static_cast<uint32_t>(ip.b[2]) << 8) | ip.b[3];
}

bool noMac(const uint8_t* mac) {
  static const uint8_t kNone[6] = {};
  return memcmp(mac, kNone, 6) == 0;
}

void copyText(char* out, size_t cap, const char* text) { snprintf(out, cap, "%s", text ? text : ""); }

// Gives a member its first values where it is. "x = T();" builds a T on the stack and copies it:
// the tools of a board with PSRAM take more than the stack of the application task (8 KB), and
// the memory beside the stack would be overwritten.
template <class T>
void renew(T& x) {
  x.~T();
  new (&x) T();
}

// Commands. At most six arguments each: what is not here is changed with update.
const lmp::ArgSpec kServiceAddArgs[] = {
    lmp::Arg::str("name", 1, 23),
    lmp::Arg::str("check", 3, 4),
    lmp::Arg::str("host", 1, 39),
    lmp::Arg::u16("port", 1, 65535).optional(),
    lmp::Arg::str("path", 1, 39).optional(),
    lmp::Arg::u16("everySec", 10, 3600).optional(),
};
const lmp::ArgSpec kServiceUpdateArgs[] = {
    lmp::Arg::u8("id", 1, 255),
    lmp::Arg::str("name", 1, 23).optional(),
    lmp::Arg::str("host", 1, 39).optional(),
    lmp::Arg::u16("port", 1, 65535).optional(),
    lmp::Arg::str("path", 1, 39).optional(),
    lmp::Arg::u16("everySec", 10, 3600).optional(),
};
const lmp::ArgSpec kIdArgs[] = {lmp::Arg::u8("id", 1, 255)};
const lmp::ArgSpec kHistoryArgs[] = {
    lmp::Arg::u8("id", 1, 255),
    lmp::Arg::str("range", 3, 4).optional(),
};
const lmp::ArgSpec kFromArgs[] = {lmp::Arg::u16("from", 0, 999).optional()};
const lmp::ArgSpec kPcAddArgs[] = {
    lmp::Arg::str("name", 1, 23),
    lmp::Arg::str("mac", 12, 17),
    lmp::Arg::str("host", 7, 15).optional(),
    lmp::Arg::str("broadcast", 7, 15).optional(),
    lmp::Arg::u16("port", 0, 65535).optional(),
    lmp::Arg::str("offUrl", 0, 95).optional(),
};
const lmp::ArgSpec kPcUpdateArgs[] = {
    lmp::Arg::u8("id", 1, 255),
    lmp::Arg::str("name", 1, 23).optional(),
    lmp::Arg::str("host", 0, 15).optional(),
    lmp::Arg::str("broadcast", 0, 15).optional(),
    lmp::Arg::u16("port", 0, 65535).optional(),
    lmp::Arg::str("offUrl", 0, 95).optional(),
};
const lmp::ArgSpec kAliasArgs[] = {
    lmp::Arg::str("ip", 7, 15),
    lmp::Arg::str("mac", 0, 17).optional(),
    lmp::Arg::str("alias", 0, 19).optional(),
    lmp::Arg::str("type", 0, 11).optional(),
};
class ExpNet;

class Settings final : public lmp::sdk::ModuleConfig<Cfg> {
 public:
  ExpNet* owner = nullptr;

 protected:
  void applied() override;
};

class ExpNet final : public lmp::sdk::Module {
  LMP_MODULE("expnet", "ExpNet", "\xF0\x9F\xA7\xB0", "1.0.4", lmp::sdk::ModuleKind::Online, kTickMs)

 public:
  Status onInit(lmp::sdk::ModuleContext& c) override {
    kernel_ = c.kernel;
    for (Service& s : services_) s = Service();
    for (Pc& p : pcs_) p = Pc();
    eventCount_ = 0;
    eventsDirty_ = false;
    eventsWrittenMs_ = 0;
    sent_[0] = '\0';
    sending_ = false;
    outHead_ = outCount_ = 0;
    if (!outbox_) outbox_ = static_cast<Outgoing*>(lmp::hal::allocLarge(sizeof(Outgoing) * kOutbox));
    if (outbox_) memset(static_cast<void*>(outbox_), 0, sizeof(Outgoing) * kOutbox);
    summaryDay_ = 0;
    netDownMs_ = 0;
    nextScanMs_ = 0;
    scanStartMs_ = scanTookMs_ = 0;
    scanAt_ = scanCount_ = 0;
    cacheUsedMs_ = 0;
    knownCount_ = onlineCount_ = newCount_ = 0;
    scan_ = Scan::Idle;
    scanRef_ = 0;
    scanAuto_ = false;
    hostsBuf_ = nullptr;
    cache_ = nullptr;
    cacheCount_ = 0;
    cacheCap_ = 0;
    lastScanUnix_ = 0;
    head_ = HostsHead();
    online_ = false;
    netKnown_ = false;
    renew(live_);
    renew(router_);
    descLen_ = 0;
    descFound_ = false;
    settings_.owner = this;
    LMP_TRY(useSettings(settings_));
    addCommands(c);
    readList();
    readEvents();
    readCounts();
    return Status::Ok;
  }

  Status onStart() override {
    nowMs_ = kernel_ ? kernel_->now() : 0;
    // The first checks start when the device is on its network.
    online_ = false;
    summariesDue_ = true;
    return Status::Ok;
  }

  void onStop() override {
    if (eventsDirty_) writeEvents();
    dropCache();
    renew(router_);
  }

  void onTick(uint32_t nowMs) override {
    nowMs_ = nowMs;
    // Without its network the device sees nobody: a service out of sight is not a service down.
    // The checks wait, and start again a little apart when the network is back.
    const bool online = networkUp();
    if (online != online_) {
      online_ = online;
      if (online) {
        spreadChecks(nowMs_ + 1000);
        // The first search a minute after the network, then at its time.
        if (settings_->scanEveryMin) nextScanMs_ = nowMs_ + kFirstScanMs;
      }
      markChanged();
    }
    if (online_) {
      watchServices();
      watchPcs();
      watchInternet();
      autoScan();
      sampleLive();
    }
    sendNext();
    morning();
    writeOneHistory();
    if (summariesDue_) loadSummaries();
    if (eventsDirty_ && nowMs_ - eventsWrittenMs_ >= kEventsWriteMs) writeEvents();
    // The list leaves the memory a minute after the last look, but not during a search.
    if (cache_ && (scan_ == Scan::Idle || scan_ == Scan::Done) && nowMs_ - cacheUsedMs_ > kCacheKeepMs) {
      dropCache();
    }
  }

  void describe(lmp::JsonWriter& w) const override {
    char ip[16];
    w.boolean("online", online_);
    w.beginArray("services");
    for (const Service& s : services_) {
      if (!s.id) continue;
      w.beginObject();
      w.u32("id", s.id);
      w.str("name", s.name);
      w.str("check", checkName(s.check));
      w.str("host", s.host);
      if (s.port) w.u32("port", s.port);
      if (s.check == Check::Http) w.str("path", s.path);
      w.u32("everySec", s.everySec);
      w.boolean("known", s.known);
      if (s.known) {
        w.boolean("up", s.up);
        w.u32("forSec", (nowMs_ - s.sinceMs) / 1000);
        if (s.up) w.u32("ms", s.lastMs);
      }
      if (s.up24 >= 0) w.i32("up24", s.up24);
      if (s.up7 >= 0) w.i32("up7", s.up7);
      if (s.up24 >= 0) w.u32("avg24", s.avg24);
      w.endObject();
    }
    w.endArray();

    w.beginArray("pcs");
    for (const Pc& p : pcs_) {
      if (!p.id) continue;
      w.beginObject();
      w.u32("id", p.id);
      w.str("name", p.name);
      char mac[18];
      lmp::macToString(p.mac, mac, sizeof(mac));
      w.str("mac", mac);
      if (p.host[0]) w.str("host", p.host);
      if (p.broadcast[0]) w.str("broadcast", p.broadcast);
      if (p.port) w.u32("port", p.port);
      w.boolean("canOff", p.offUrl[0] != '\0');
      w.boolean("known", p.known);
      if (p.known) {
        w.boolean("on", p.on);
        w.u32("forSec", (nowMs_ - p.sinceMs) / 1000);
      }
      if (p.wakeMs) w.u32("wakingSec", (nowMs_ - p.wakeMs) / 1000);
      if (p.wokeAfterMs) w.u32("wokeSec", (p.wokeAfterMs + 500) / 1000);
      w.endObject();
    }
    w.endArray();

    w.beginObject("scan");
    w.str("state", scanName(scan_));
    if (scan_ != Scan::Idle && scan_ != Scan::Done) {
      w.str("step", stepName(scan_));
      w.u32("at", scanAt_);
      w.u32("of", scan_ == Scan::Ping    ? scanCount_
                  : scan_ == Scan::Mdns  ? kMdnsCount
                  : scan_ == Scan::Ssdp  ? kSsdpAsked + 1
                  : scan_ == Scan::Ports ? cacheCount_
                                         : 0);
    }
    w.u32("known", knownCount_);
    w.u32("found", onlineCount_);
    w.u32("new", newCount_);
    if (lastScanUnix_) w.i64("lastUnix", lastScanUnix_);
    if (scanTookMs_) w.u32("tookSec", scanTookMs_ / 1000);
    w.u32("everyMin", settings_->scanEveryMin);
    if (settings_->scanEveryMin && online_ && (scan_ == Scan::Idle || scan_ == Scan::Done)) {
      const int32_t left = static_cast<int32_t>(nextScanMs_ - nowMs_);
      w.u32("nextSec", left > 0 ? static_cast<uint32_t>(left) / 1000 : 0);
    }
    Ip4 address, mask;
    if (probe() && probe()->network(address, mask)) {
      address.toString(ip, sizeof(ip));
      w.str("address", ip);
    }
    w.endObject();

    w.beginObject("notify");
    w.boolean("telegram", telegramReady());
    if (*sent_) w.str("last", sent_);
    w.u32("queued", outCount_);
    w.boolean("quiet", quietNow());
    w.endObject();

    w.u32("events", eventCount_);
    if (eventCount_ && events_) w.i64("lastEventUnix", events_[eventCount_ - 1].unix);
    w.beginObject("limits");
    w.u32("memory", LMP_MODULE_MEM);
    w.u32("hosts", kHostsMax);
    w.u32("events", kEventsMax);
    w.u32("services", kServicesMax);
    w.u32("pcs", kPcsMax);
    w.u32("liveMs", kLiveEveryMs);
    w.u32("routerMs", kRouterEveryMs);
    w.endObject();
  }

  // What a home system sees: a service up or down, how long it answers, a PC on or off and a
  // button that switches it on; how many devices are in the network.
  void things(lmp::sdk::ThingSink& out) const override {
    char id[8];
    char name[40];
    const bool it = italian();
    for (const Service& s : services_) {
      if (!s.id) continue;
      snprintf(id, sizeof(id), "s%u", static_cast<unsigned>(s.id));
      out.flag(id, s.name, s.known && s.up, "connectivity");
      snprintf(id, sizeof(id), "l%u", static_cast<unsigned>(s.id));
      snprintf(name, sizeof(name), it ? "Tempo di %s" : "%s time", s.name);
      out.technical();
      out.number(id, name, s.lastMs, "ms", "duration", 0, s.known && s.up);
    }
    for (const Pc& p : pcs_) {
      if (!p.id) continue;
      if (p.host[0]) {
        snprintf(id, sizeof(id), "p%u", static_cast<unsigned>(p.id));
        out.flag(id, p.name, p.known && p.on, "power");
      }
      snprintf(id, sizeof(id), "w%u", static_cast<unsigned>(p.id));
      snprintf(name, sizeof(name), it ? "Accendi %s" : "Wake %s", p.name);
      out.button(id, name);
    }
    out.number("net", it ? "Dispositivi in rete" : "Devices in the network", onlineCount_, "", "",
               0, lastScanUnix_ != 0);
  }

  Status thingSet(const char* id, const lmp::sdk::ThingAsk& ask) override {
    if (!id || id[0] != 'w' || !ask.press) return Status::NotFound;
    Pc* p = pcOf(static_cast<uint8_t>(atoi(id + 1)));
    if (!p) return Status::NotFound;
    return wake(*p);
  }

  void onSettings() {
    // Another device to read, or back to the one found by itself, or another way to ask a
    // router with SNMP: looked for again. An answer still on its way is not of this search.
    if (strcmp(upnpUsed_, settings_->upnpHost.c_str()) != 0 || fromUsed_ != settings_->trafficFrom ||
        strcmp(snmpUsed_, settings_->snmpCommunity.c_str()) != 0 || snmpIfUsed_ != settings_->snmpIf) {
      fromUsed_ = settings_->trafficFrom;
      copyText(upnpUsed_, sizeof(upnpUsed_), settings_->upnpHost.c_str());
      copyText(snmpUsed_, sizeof(snmpUsed_), settings_->snmpCommunity.c_str());
      snmpIfUsed_ = settings_->snmpIf;
      ++routerGen_;
      renew(router_);
    }
    // A search every so often: the next one never later than the new period.
    const uint32_t period = settings_->scanEveryMin * 60000u;
    if (period && (!nextScanMs_ || static_cast<int32_t>(nextScanMs_ - nowMs_) > static_cast<int32_t>(period))) {
      nextScanMs_ = nowMs_ + period;
    }
    markChanged();
  }

 private:
  // ---------------------------------------------------------------- helpers

  lmp::IProbeService* probe() const {
    return kernel_ ? const_cast<ExpNet*>(this)->ctx().probe : nullptr;
  }

  bool italian() const {
    const char* lang = kernel_ ? kernel_->config().sys.lang.c_str() : "";
    return lang[0] == 'i' && lang[1] == 't';
  }

  int64_t unixNow() const {
    const lmp::IClockService* clock = kernel_ ? const_cast<ExpNet*>(this)->ctx().clock : nullptr;
    return clock && clock->synced() ? clock->unixTime() : 0;
  }

  bool localNow(lmp::hal::DateTime& dt) const {
    const lmp::IClockService* clock = kernel_ ? const_cast<ExpNet*>(this)->ctx().clock : nullptr;
    return clock && clock->synced() && clock->localTime(dt);
  }

  // The station of the device: address, gateway, name server, signal.
  bool station(lmp::hal::StaInfo& sta) const {
    if (!kernel_ || !kernel_->board().net) return false;
    kernel_->board().net->staInfo(sta);
    return sta.state == lmp::hal::StaState::Connected && sta.ip.isSet();
  }

  Service* serviceOf(uint8_t id) {
    for (Service& s : services_) {
      if (s.id && s.id == id) return &s;
    }
    return nullptr;
  }
  Pc* pcOf(uint8_t id) {
    for (Pc& p : pcs_) {
      if (p.id && p.id == id) return &p;
    }
    return nullptr;
  }
  // The first number not used by a service or a PC: ids never move, a home system keeps them.
  template <class T, size_t N>
  static uint8_t freeId(const T (&list)[N]) {
    for (unsigned id = 1; id < 256; ++id) {
      bool used = false;
      for (const T& x : list) used = used || x.id == id;
      if (!used) return static_cast<uint8_t>(id);
    }
    return 0;
  }

  void historyPath(uint8_t id, char* out, size_t cap) const {
    snprintf(out, cap, "/expnet/h%u.bin", static_cast<unsigned>(id));
  }

  // ---------------------------------------------------------------- commands

  void addCommands(lmp::sdk::ModuleContext& c) {
    c.commands->add("service.add", LMP_HELP("A service to watch: ping, tcp or http"), kServiceAddArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return serviceAdd(a, ctx);
                    });
    c.commands->add("service.update", LMP_HELP("Changes a watched service"), kServiceUpdateArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return serviceUpdate(a, ctx);
                    });
    c.commands->add("service.remove", LMP_HELP("Stops watching a service; its history goes too"),
                    kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      Service* s = serviceOf(a.u8("id"));
                      if (!s) return Status::NotFound;
                      char path[24];
                      historyPath(s->id, path, sizeof(path));
                      if (ctx().storage) ctx().storage->remove(path);
                      *s = Service();
                      writeList();
                      markChanged();
                      return Status::Ok;
                    },
                    lmp::kCmdConfirm);
    c.commands->add("service.check", LMP_HELP("Checks a service now"), kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      Service* s = serviceOf(a.u8("id"));
                      if (!s) return Status::NotFound;
                      s->nextMs = nowMs_;
                      return Status::Ok;
                    });
    c.commands->add("history", LMP_HELP("The last day of a service, or its week"), kHistoryArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return history(a, ctx);
                    });
    c.commands->add("events", LMP_HELP("What happened in the network, the latest first"), kFromArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return events(a.u16("from"), ctx);
                    });
    c.commands->add("pc.add", LMP_HELP("A PC to switch on with Wake-on-LAN"), kPcAddArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return pcAdd(a, ctx);
                    });
    c.commands->add("pc.update", LMP_HELP("Changes a PC"), kPcUpdateArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return pcUpdate(a, ctx);
                    });
    c.commands->add("pc.remove", LMP_HELP("Forgets a PC"), kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      Pc* p = pcOf(a.u8("id"));
                      if (!p) return Status::NotFound;
                      *p = Pc();
                      writeList();
                      markChanged();
                      return Status::Ok;
                    },
                    lmp::kCmdConfirm);
    c.commands->add("pc.wake", LMP_HELP("Switches a PC on"), kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      Pc* p = pcOf(a.u8("id"));
                      return p ? wake(*p) : Status::NotFound;
                    });
    c.commands->add("pc.off", LMP_HELP("Turns a PC off with its webhook"), kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      Pc* p = pcOf(a.u8("id"));
                      return p ? turnOff(*p, ctx.ref) : Status::NotFound;
                    },
                    lmp::kCmdAsync | lmp::kCmdConfirm);
    c.commands->add("scan", LMP_HELP("Who is in the network: addresses, MAC, names, services"),
                    [this](const lmp::Args&, lmp::CmdContext& ctx) -> Status {
                      return startScan(ctx.ref, false);
                    },
                    lmp::kCmdAsync);
    c.commands->add("hosts", LMP_HELP("The devices of the network, a page at a time"), kFromArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return hostsPage(a.u16("from"), ctx);
                    });
    c.commands->add("makers", LMP_HELP("The extended table of the makers, a page at a time"), kFromArgs,
                    [](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return makersPage(a.u16("from"), ctx);
                    });
    c.commands->add("host.alias", LMP_HELP("Gives a device of the network a name or a type of yours"),
                    kAliasArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return setAlias(a, ctx);
                    });
    c.commands->add("live", LMP_HELP("The time of the answers of the router and of Internet, the signal"),
                    [this](const lmp::Args&, lmp::CmdContext& ctx) -> Status {
                      return live(ctx);
                    });
    c.commands->add("notify.test", LMP_HELP("Sends a message to Telegram, to try it"),
                    [this](const lmp::Args&, lmp::CmdContext& ctx) -> Status {
                      if (!telegramReady()) return Status::Denied;
                      return say(italian() ? "\xE2\x9C\x85 ExpNet: prova riuscita, i messaggi arrivano."
                                           : "\xE2\x9C\x85 ExpNet: test passed, the messages arrive.",
                                 ctx.ref)
                                 ? (ctx.ref ? Status::Pending : Status::Ok)
                                 : Status::Busy;
                    },
                    lmp::kCmdAsync);
  }

  // Ping and tcp want an address; http takes a name too, which its client resolves.
  static Status readHost(Service& s, const char* host, lmp::CmdContext& ctx) {
    Ip4 ip;
    if (s.check != Check::Http && !Ip4::parse(host, ip)) {
      ctx.errorField = "host";
      return Status::Invalid;
    }
    copyText(s.host, sizeof(s.host), host);
    s.ip = ip;
    return Status::Ok;
  }

  Status serviceAdd(const lmp::Args& a, lmp::CmdContext& ctx) {
    Service s;
    if (!checkOf(a.str("check"), s.check)) {
      ctx.errorField = "check";
      return Status::Invalid;
    }
    LMP_TRY(readHost(s, a.str("host"), ctx));
    if (s.check == Check::Tcp && !a.has("port")) {
      ctx.errorField = "port";
      return Status::Invalid;
    }
    Service* free = nullptr;
    for (Service& x : services_) {
      if (!x.id) {
        free = &x;
        break;
      }
    }
    if (!free) return Status::NoMem;
    s.id = freeId(services_);
    copyText(s.name, sizeof(s.name), a.str("name"));
    s.port = a.u16("port", s.check == Check::Http ? 80 : 0);
    if (a.has("path")) copyText(s.path, sizeof(s.path), a.str("path"));
    s.everySec = a.u16("everySec", settings_->everySec);
    s.nextMs = nowMs_;
    *free = s;
    writeList();
    markChanged();
    if (ctx.result) ctx.result->u32("id", s.id);
    return Status::Ok;
  }

  Status serviceUpdate(const lmp::Args& a, lmp::CmdContext& ctx) {
    Service* s = serviceOf(a.u8("id"));
    if (!s) return Status::NotFound;
    Service changed = *s;
    if (a.has("name")) copyText(changed.name, sizeof(changed.name), a.str("name"));
    if (a.has("host")) LMP_TRY(readHost(changed, a.str("host"), ctx));
    if (a.has("port")) changed.port = a.u16("port");
    if (a.has("path")) copyText(changed.path, sizeof(changed.path), a.str("path"));
    if (a.has("everySec")) changed.everySec = a.u16("everySec");
    // A service that is now another one starts again from nothing known.
    if (a.has("host") || a.has("port") || a.has("path")) {
      changed.known = false;
      changed.fails = 0;
    }
    changed.nextMs = nowMs_;
    *s = changed;
    writeList();
    markChanged();
    return Status::Ok;
  }

  Status pcAdd(const lmp::Args& a, lmp::CmdContext& ctx) {
    Pc p;
    if (!lmp::parseMac(a.str("mac"), p.mac)) {
      ctx.errorField = "mac";
      return Status::Invalid;
    }
    LMP_TRY(readPcFields(p, a, ctx));
    Pc* free = nullptr;
    for (Pc& x : pcs_) {
      if (!x.id) {
        free = &x;
        break;
      }
    }
    if (!free) return Status::NoMem;
    p.id = freeId(pcs_);
    copyText(p.name, sizeof(p.name), a.str("name"));
    p.nextMs = nowMs_;
    *free = p;
    writeList();
    markChanged();
    if (ctx.result) ctx.result->u32("id", p.id);
    return Status::Ok;
  }

  Status pcUpdate(const lmp::Args& a, lmp::CmdContext& ctx) {
    Pc* p = pcOf(a.u8("id"));
    if (!p) return Status::NotFound;
    Pc changed = *p;
    if (a.has("name")) copyText(changed.name, sizeof(changed.name), a.str("name"));
    LMP_TRY(readPcFields(changed, a, ctx));
    if (a.has("host") || a.has("port")) changed.known = false;
    changed.nextMs = nowMs_;
    *p = changed;
    writeList();
    markChanged();
    return Status::Ok;
  }

  static Status readPcFields(Pc& p, const lmp::Args& a, lmp::CmdContext& ctx) {
    if (a.has("host")) {
      const char* host = a.str("host");
      Ip4 ip;
      if (*host && !Ip4::parse(host, ip)) {
        ctx.errorField = "host";
        return Status::Invalid;
      }
      copyText(p.host, sizeof(p.host), host);
      p.ip = ip;
    }
    if (a.has("broadcast")) {
      const char* b = a.str("broadcast");
      Ip4 ip;
      if (*b && !Ip4::parse(b, ip)) {
        ctx.errorField = "broadcast";
        return Status::Invalid;
      }
      copyText(p.broadcast, sizeof(p.broadcast), b);
    }
    if (a.has("port")) p.port = a.u16("port");
    if (a.has("offUrl")) {
      const char* url = a.str("offUrl");
      if (*url && strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        ctx.errorField = "offUrl";
        return Status::Invalid;
      }
      copyText(p.offUrl, sizeof(p.offUrl), url);
    }
    return Status::Ok;
  }

  // ---------------------------------------------------------------- the monitor

  // True when the device is on its network: what the checks need.
  bool networkUp() const {
    lmp::IProbeService* asker = probe();
    Ip4 address, mask;
    return asker && asker->ready() && asker->network(address, mask);
  }

  // The next checks a little apart from at, not all in the same round.
  void spreadChecks(uint32_t at) {
    for (Service& s : services_) {
      if (!s.id) continue;
      s.nextMs = at;
      at += 700;
    }
    for (Pc& p : pcs_) {
      if (!p.id) continue;
      p.nextMs = at;
      at += 700;
    }
  }

  void watchServices() {
    lmp::IProbeService* asker = probe();
    for (Service& s : services_) {
      if (!s.id || s.asking || static_cast<int32_t>(nowMs_ - s.nextMs) < 0) continue;
      const uint8_t id = s.id;
      Status asked = Status::Unsupported;
      if (s.check == Check::Http) {
        asked = askHttp(s);
      } else if (asker && asker->ready()) {
        lmp::ProbeAsk what;
        what.kind = s.check == Check::Ping ? lmp::ProbeKind::Ping : lmp::ProbeKind::Tcp;
        what.ip = s.ip;
        what.port = s.port;
        what.timeoutMs = s.check == Check::Ping ? kPingWaitMs : kTcpWaitMs;
        asked = asker->ask(what, [this, id](const lmp::ProbeAnswer& answer) {
          // A port that refuses is a service that does not answer, on a device that does.
          answered(id, answer.result == Status::Ok, answer.rttMs);
        });
      }
      if (asked == Status::Ok) {
        s.asking = true;
      } else {
        // Busy, or nobody to ask now: in a few seconds.
        s.nextMs = nowMs_ + 3000;
      }
    }
  }

  Status askHttp(Service& s) {
    lmp::IHttpClientService* http = ctx().http;
    if (!http || !http->ready()) return Status::Denied;
    snprintf(url_[s.id % kServicesMax], sizeof(url_[0]), "http://%s:%u%s", s.host,
             static_cast<unsigned>(s.port ? s.port : 80), s.path[0] == '/' ? s.path : "/");
    lmp::HttpFetch what;
    what.url = url_[s.id % kServicesMax];
    what.timeoutMs = kHttpWaitMs;
    what.quiet = true;   // a service down is told by ExpNet, not by a warning a minute
    const uint8_t id = s.id;
    return http->fetch(what, [this, id](const lmp::HttpAnswer& answer) {
      // Up when it answers, with any page but an error: 200 to 399. The time is the one of the
      // request, not of its wait behind the others.
      answered(id, answer.result == Status::Ok && answer.status >= 200 && answer.status < 400,
               answer.ms);
    });
  }

  void answered(uint8_t id, bool ok, uint32_t ms) {
    Service* s = serviceOf(id);
    if (!s) return;
    s->asking = false;
    s->nextMs = nowMs_ + static_cast<uint32_t>(s->everySec) * 1000u;
    // The network fell while it was asked: the failure says nothing of the service.
    if (!ok && !networkUp()) return;
    remember(*s, ok, ms);
    if (ok) {
      const bool was = s->known && !s->up;
      const uint32_t downMs = nowMs_ - s->sinceMs;
      s->fails = 0;
      s->lastMs = ms;
      if (!s->known || !s->up) {
        s->known = true;
        s->up = true;
        s->sinceMs = nowMs_;
        if (was) serviceChanged(*s, true, downMs);
        markChanged();
      }
      return;
    }
    if (s->fails < 255) ++s->fails;
    // Down after two in a row: a packet lost is not a service down.
    if (s->fails >= kDownAfter && (s->up || !s->known)) {
      const bool was = s->known && s->up;
      s->known = true;
      s->up = false;
      s->sinceMs = nowMs_;
      if (was) serviceChanged(*s, false, 0);
      markChanged();
    }
  }

  // The block of 15 minutes that is filling takes the check; a block that ended waits to be
  // written, one file a round.
  void remember(Service& s, bool ok, uint32_t ms) {
    const int64_t unix = unixNow();
    if (!unix) return;   // without the time no block knows where it goes
    const uint32_t block = blockOf(unix);
    if (s.fillingBlock && block != s.fillingBlock && !s.filling.empty()) {
      s.closed = s.filling.block();
      s.closedBlock = s.fillingBlock;
      s.historyDue = true;
      s.filling.clear();
    }
    if (block != s.fillingBlock) s.filling.clear();
    s.fillingBlock = block;
    s.filling.add(ok, ms);
  }

  void writeOneHistory() {
    for (Service& s : services_) {
      if (!s.id || !s.historyDue) continue;
      s.historyDue = false;
      uint8_t* bytes = readHistory(s.id);
      if (!bytes) return;
      HistoryFile f(bytes);
      f.put(s.closedBlock, s.closed);
      summarizeInto(s, f, s.closedBlock);
      writeHistory(s.id, bytes);
      lmp::hal::freeLarge(bytes);
      markChanged();
      return;   // one file a round
    }
  }

  // The file of a service, whole, in a buffer taken here: the caller frees it. A missing file
  // is an empty history. Null without memory.
  uint8_t* readHistory(uint8_t id) {
    auto* bytes = static_cast<uint8_t*>(lmp::hal::allocLarge(kHistoryBytes + 1));
    if (!bytes) return nullptr;
    HistoryFile f(bytes);
    f.clear();
    char path[24];
    historyPath(id, path, sizeof(path));
    lmp::IStorage* storage = ctx().storage;
    size_t len = 0;
    if (storage && storage->exists(path) &&
        storage->readFile(path, reinterpret_cast<char*>(bytes), kHistoryBytes + 1, len) == Status::Ok &&
        len == kHistoryBytes && f.valid()) {
      return bytes;
    }
    f.clear();
    return bytes;
  }

  void writeHistory(uint8_t id, const uint8_t* bytes) {
    lmp::IStorage* storage = ctx().storage;
    if (!storage) return;
    char path[24];
    historyPath(id, path, sizeof(path));
    const Status s = storage->writeFile(path, lmp::StringView(reinterpret_cast<const char*>(bytes),
                                                               kHistoryBytes));
    if (s != Status::Ok) LMP_LOGW(ctx().log, "history of %u not written: %s", id, lmp::statusName(s));
  }

  void summarizeInto(Service& s, const HistoryFile& f, uint32_t now) {
    const Summary day = summarize(f, now, kBlocksDay);
    const Summary week = summarize(f, now, kBlocks);
    s.up24 = static_cast<int16_t>(day.uptime());
    s.up7 = static_cast<int16_t>(week.uptime());
    s.avg24 = static_cast<uint16_t>(day.avgMs > 65535 ? 65535 : day.avgMs);
  }

  // At the start, what the files say: one service a round.
  void loadSummaries() {
    const int64_t unix = unixNow();
    if (!unix) return;
    for (Service& s : services_) {
      if (!s.id || s.up24 >= 0 || s.up7 >= 0 || s.summaryRead) continue;
      s.summaryRead = true;
      uint8_t* bytes = readHistory(s.id);
      if (!bytes) return;
      HistoryFile f(bytes);
      summarizeInto(s, f, blockOf(unix));
      lmp::hal::freeLarge(bytes);
      markChanged();
      return;
    }
    summariesDue_ = false;
  }

  Status history(const lmp::Args& a, lmp::CmdContext& ctx) {
    usedMs_ = nowMs_;   // somebody looks
    Service* s = serviceOf(a.u8("id"));
    if (!s) return Status::NotFound;
    const char* range = a.str("range", "day");
    const bool week = strcmp(range, "week") == 0;
    if (!week && strcmp(range, "day") != 0) {
      ctx.errorField = "range";
      return Status::Invalid;
    }
    const int64_t unix = unixNow();
    if (!unix) return Status::Denied;   // without the time no block is known
    if (!ctx.result) return Status::Ok;
    uint8_t* bytes = readHistory(s->id);
    if (!bytes) return Status::NoMem;
    HistoryFile f(bytes);
    const uint32_t now = blockOf(unix);
    // A day: its 96 blocks. A week: its 168 hours, four blocks each, to stay short.
    const uint16_t step = week ? 4 : 1;
    const uint16_t points = week ? kBlocks / 4 : kBlocksDay;
    lmp::JsonWriter& w = *ctx.result;
    w.str("range", week ? "week" : "day");
    w.u32("stepSec", static_cast<uint32_t>(step) * kBlockSec);
    w.i64("endUnix", static_cast<int64_t>(now + 1) * kBlockSec);
    w.beginArray("ms");
    for (int p = points - 1; p >= 0; --p) {
      Summary sum;
      uint64_t msSum = 0;
      for (uint16_t i = 0; i < step; ++i) {
        const uint32_t n = now - static_cast<uint32_t>(p) * step - (step - 1 - i);
        const Block b = n == now && s->fillingBlock == now ? s->filling.block() : f.get(n, now);
        sum.total += b.total;
        sum.ok += b.ok;
        msSum += static_cast<uint64_t>(b.avgMs) * b.ok;
      }
      if (sum.ok) w.u64(msSum / sum.ok);
      else w.null();
    }
    w.endArray();
    w.beginArray("up");
    for (int p = points - 1; p >= 0; --p) {
      Summary sum;
      for (uint16_t i = 0; i < step; ++i) {
        const uint32_t n = now - static_cast<uint32_t>(p) * step - (step - 1 - i);
        const Block b = n == now && s->fillingBlock == now ? s->filling.block() : f.get(n, now);
        sum.total += b.total;
        sum.ok += b.ok;
      }
      if (sum.total) w.i64(sum.uptime());
      else w.null();
    }
    w.endArray();
    lmp::hal::freeLarge(bytes);
    return Status::Ok;
  }

  // ---------------------------------------------------------------- events and messages

  Event& addEvent(Ev kind, uint8_t id, const char* name) {
    static Event none;
    if (!events_) return none = Event();
    if (eventCount_ == kEventsMax) {
      memmove(&events_[0], &events_[1], sizeof(Event) * (kEventsMax - 1));
      --eventCount_;
    }
    Event& e = events_[eventCount_++];
    e = Event();
    e.unix = static_cast<uint32_t>(unixNow());
    e.kind = static_cast<uint8_t>(kind);
    e.id = id;
    copyText(e.name, sizeof(e.name), name);
    eventsDirty_ = true;
    markChanged();
    return e;
  }

  void serviceChanged(const Service& s, bool up, uint32_t downMs) {
    Event& e = addEvent(up ? Ev::ServiceUp : Ev::ServiceDown, s.id, s.name);
    const uint32_t minutes = (downMs + 30000) / 60000;
    e.minutes = static_cast<uint16_t>(minutes > 65535 ? 65535 : minutes);
    memcpy(e.ip, s.ip.b, 4);
    LMP_LOGI(ctx().log, "%s %s", s.name, up ? "is back" : "is down");
    if (!settings_->notify) return;
    char text[200];
    if (italian()) {
      if (up) {
        snprintf(text, sizeof(text), "\xF0\x9F\x9F\xA2 ExpNet: %s di nuovo raggiungibile (%s), era gi\xC3\xB9 da %u min",
                 s.name, s.host, static_cast<unsigned>(e.minutes));
      } else {
        snprintf(text, sizeof(text), "\xF0\x9F\x94\xB4 ExpNet: %s non risponde (%s %s)", s.name,
                 checkName(s.check), s.host);
      }
    } else if (up) {
      snprintf(text, sizeof(text), "\xF0\x9F\x9F\xA2 ExpNet: %s is back (%s), down for %u min", s.name,
               s.host, static_cast<unsigned>(e.minutes));
    } else {
      snprintf(text, sizeof(text), "\xF0\x9F\x94\xB4 ExpNet: %s does not answer (%s %s)", s.name,
               checkName(s.check), s.host);
    }
    say(text);
  }

  Status events(uint16_t from, lmp::CmdContext& ctx) {
    usedMs_ = nowMs_;   // somebody looks
    if (!ctx.result) return Status::Ok;
    lmp::JsonWriter& w = *ctx.result;
    w.u32("count", eventCount_);
    w.beginArray("events");
    uint16_t sent = 0;
    char text[18];
    for (int i = static_cast<int>(eventCount_) - 1 - from; i >= 0 && sent < kEventsPage; --i, ++sent) {
      const Event& e = events_[i];
      w.beginObject();
      if (e.unix) w.i64("unix", e.unix);
      w.str("kind", evName(e.kind));
      if (e.id) w.u32("id", e.id);
      w.str("name", e.name);
      Ip4 ip;
      memcpy(ip.b, e.ip, 4);
      if (ip.isSet()) {
        ip.toString(text, sizeof(text));
        w.str("ip", text);
      }
      if (!noMac(e.mac)) {
        lmp::macToString(e.mac, text, sizeof(text));
        w.str("mac", text);
      }
      if (e.minutes) w.u32("minutes", e.minutes);
      w.endObject();
    }
    w.endArray();
    if (from + sent < eventCount_) w.u32("next", from + sent);
    return Status::Ok;
  }

  // The register, in the large memory, read straight from its file.
  void readEvents() {
    if (!events_) events_ = static_cast<Event*>(lmp::hal::allocLarge(sizeof(Event) * kEventsMax + 1));
    eventCount_ = 0;
    lmp::IStorage* storage = ctx().storage;
    if (!events_ || !storage || !storage->exists(kEventsFile)) return;
    size_t len = 0;
    if (storage->readFile(kEventsFile, reinterpret_cast<char*>(events_), sizeof(Event) * kEventsMax + 1, len) ==
            Status::Ok && len % sizeof(Event) == 0) {
      eventCount_ = static_cast<uint16_t>(len / sizeof(Event));
    }
  }

  void writeEvents() {
    eventsDirty_ = false;
    eventsWrittenMs_ = nowMs_;
    if (!events_) return;
    lmp::IStorage* storage = ctx().storage;
    if (!storage) return;
    storage->writeFile(kEventsFile, lmp::StringView(reinterpret_cast<const char*>(events_),
                                                    eventCount_ * sizeof(Event)));
  }

  // ---------------------------------------------------------------- Telegram

  bool telegramReady() const {
    return !settings_->telegramToken.empty() && !settings_->telegramChat.empty();
  }

  // Between quietFrom and quietTo messages arrive without a sound: the night.
  bool quietNow() const {
    const uint8_t from = settings_->quietFrom;
    const uint8_t to = settings_->quietTo;
    lmp::hal::DateTime dt;
    if (from >= 24 || from == to || !localNow(dt)) return false;
    return from < to ? dt.hour >= from && dt.hour < to : dt.hour >= from || dt.hour < to;
  }

  // A message for the chat, in the queue. ref: the command that waits for the outcome.
  bool say(const char* text, uint32_t ref = 0) {
    if (!telegramReady() || !text || !*text || !outbox_) return false;
    if (outCount_ == kOutbox) {
      // The oldest goes: a message is worth less than the ones after it.
      outHead_ = static_cast<uint8_t>((outHead_ + 1) % kOutbox);
      --outCount_;
    }
    Outgoing& o = outbox_[(outHead_ + outCount_) % kOutbox];
    copyText(o.text, sizeof(o.text), text);
    o.silent = quietNow();
    o.ref = ref;
    ++outCount_;
    markChanged();
    return true;
  }

  void sendNext() {
    if (sending_ || !outCount_) return;
    lmp::IHttpClientService* http = ctx().http;
    if (!http || !http->ready() || !http->secure() || !telegramReady()) return;
    const Outgoing& o = outbox_[outHead_];
    snprintf(tgUrl_, sizeof(tgUrl_), "https://api.telegram.org/bot%s/sendMessage",
             settings_->telegramToken.c_str());
    lmp::BufferSink sink(tgBody_, sizeof(tgBody_));
    lmp::JsonWriter w(sink);
    w.beginObject();
    w.str("chat_id", settings_->telegramChat.c_str());
    w.str("text", o.text);
    if (o.silent) w.boolean("disable_notification", true);
    w.endObject();
    const uint32_t ref = o.ref;
    outHead_ = static_cast<uint8_t>((outHead_ + 1) % kOutbox);
    --outCount_;
    if (!w.complete()) {
      if (ref) kernel_->events().publish(lmp::commandDone(ref, Status::NoMem));
      return;
    }
    lmp::HttpFetch what;
    what.url = tgUrl_;
    what.method = lmp::hal::HttpMethod::Post;
    what.contentType = "application/json";
    what.body = sink.view();
    what.timeoutMs = 15000;
    sending_ = true;
    const Status s = http->fetch(what, [this, ref](const lmp::HttpAnswer& answer) {
      sending_ = false;
      const bool good = answer.good();
      copyText(sent_, sizeof(sent_), good ? "ok" : (answer.status ? "refused" : lmp::statusName(answer.result)));
      markChanged();
      if (!good) LMP_LOGW(ctx().log, "telegram: %d %s", answer.status, lmp::statusName(answer.result));
      if (ref) kernel_->events().publish(lmp::commandDone(ref, good ? Status::Ok : Status::Io));
    });
    if (s != Status::Ok) {
      sending_ = false;
      if (ref) kernel_->events().publish(lmp::commandDone(ref, s));
    }
  }

  // "23 devices in the network (2 new), services 3 of 3 up, PC on 1 of 2, Internet ok."
  void summary(char* out, size_t cap) const {
    uint8_t up = 0, services = 0, on = 0, pcs = 0;
    for (const Service& s : services_) {
      if (!s.id) continue;
      ++services;
      if (s.known && s.up) ++up;
    }
    for (const Pc& p : pcs_) {
      if (!p.id) continue;
      ++pcs;
      if (p.known && p.on) ++on;
    }
    lmp::INetService* net = const_cast<ExpNet*>(this)->ctx().net;
    const bool internet = net && net->internet();
    size_t at = static_cast<size_t>(snprintf(out, cap, italian()
        ? "\xF0\x9F\xA7\xB0 ExpNet: %u dispositivi in rete, %u nuovi nelle ultime 24 ore. Servizi su: %u di %u. PC accesi: %u di %u. Internet: %s."
        : "\xF0\x9F\xA7\xB0 ExpNet: %u devices in the network, %u new in the last 24 hours. Services up: %u of %u. PCs on: %u of %u. Internet: %s.",
        static_cast<unsigned>(onlineCount_), static_cast<unsigned>(newCount_), up, services, on, pcs,
        internet ? "ok" : (italian() ? "no" : "no")));
    for (const Service& s : services_) {
      if (!s.id || !s.known || s.up || at + 40 >= cap) continue;
      at += static_cast<size_t>(snprintf(out + at, cap - at, "\n\xF0\x9F\x94\xB4 %s", s.name));
    }
  }

  // The summary of the morning, once a day at its hour.
  void morning() {
    if (settings_->summaryHour >= 24 || !telegramReady()) return;
    lmp::hal::DateTime dt;
    if (!localNow(dt) || dt.hour != settings_->summaryHour || dt.day == summaryDay_) return;
    summaryDay_ = dt.day;
    char* text = msg_;
    summary(text, sizeof(msg_));
    say(text);
  }

  // ---------------------------------------------------------------- Internet

  void watchInternet() {
    lmp::INetService* net = ctx().net;
    if (!net) return;
    const bool up = net->internet();
    if (!netKnown_) {
      if (up) {
        netKnown_ = true;
        netUp_ = true;
      }
      return;
    }
    if (up == netUp_) return;
    netUp_ = up;
    if (!up) {
      netDownMs_ = nowMs_;
      addEvent(Ev::NetDown, 0, "Internet");
      return;
    }
    const uint32_t minutes = (nowMs_ - netDownMs_ + 30000) / 60000;
    Event& e = addEvent(Ev::NetUp, 0, "Internet");
    e.minutes = static_cast<uint16_t>(minutes > 65535 ? 65535 : minutes);
    if (!settings_->notifyNet) return;
    char text[160];
    snprintf(text, sizeof(text), italian() ? "\xF0\x9F\x8C\x90 ExpNet: Internet di nuovo raggiungibile, era assente da %u min."
                                           : "\xF0\x9F\x8C\x90 ExpNet: Internet is back, it was gone for %u min.",
             static_cast<unsigned>(e.minutes));
    say(text);
  }

  // ---------------------------------------------------------------- the PC

  Status wake(Pc& p) {
    lmp::IProbeService* asker = probe();
    if (!asker || !asker->ready()) return Status::Denied;
    lmp::ProbeAsk what;
    what.kind = lmp::ProbeKind::Wake;
    memcpy(what.mac, p.mac, 6);
    if (p.broadcast[0]) Ip4::parse(p.broadcast, what.ip);
    LMP_TRY(asker->ask(what, lmp::ProbeDone()));
    LMP_LOGI(ctx().log, "%s: woken", p.name);
    p.wakeMs = nowMs_ ? nowMs_ : 1;
    p.nextMs = nowMs_ + kWakingEveryMs;
    markChanged();
    return Status::Ok;
  }

  Status turnOff(Pc& p, uint32_t ref) {
    lmp::IHttpClientService* http = ctx().http;
    if (!p.offUrl[0]) return Status::Unsupported;
    if (!http || !http->ready()) return Status::Denied;
    lmp::HttpFetch what;
    what.url = p.offUrl;
    // A webhook of Home Assistant wants a POST; the programs on a PC take it as well.
    what.method = lmp::hal::HttpMethod::Post;
    what.contentType = "application/json";
    what.body = lmp::StringView("{}");
    what.timeoutMs = 10000;
    // The command that waits stays with the PC: the answer carries only who it is for.
    p.offRef = ref;
    const uint8_t id = p.id;
    LMP_TRY(http->fetch(what, [this, id](const lmp::HttpAnswer& answer) {
      Pc* pc = pcOf(id);
      if (!pc) return;
      pc->nextMs = nowMs_ + 15000;   // a PC takes some seconds to go off
      const uint32_t waiting = pc->offRef;
      pc->offRef = 0;
      if (waiting) {
        kernel_->events().publish(lmp::commandDone(waiting, answer.good() ? Status::Ok : Status::Io));
      }
    }));
    return ref ? Status::Pending : Status::Ok;
  }

  void watchPcs() {
    lmp::IProbeService* asker = probe();
    if (!asker || !asker->ready()) return;
    for (Pc& p : pcs_) {
      if (!p.id) continue;
      // Waking: the PC is given three minutes.
      if (p.wakeMs && nowMs_ - p.wakeMs > kWakingMs) {
        p.wakeMs = 0;
        markChanged();
      }
      if (!p.host[0] || p.asking || static_cast<int32_t>(nowMs_ - p.nextMs) < 0) continue;
      lmp::ProbeAsk what;
      what.kind = p.port ? lmp::ProbeKind::Tcp : lmp::ProbeKind::Ping;
      what.ip = p.ip;
      what.port = p.port;
      what.timeoutMs = p.port ? kTcpWaitMs : kPingWaitMs;
      const uint8_t id = p.id;
      const Status asked = asker->ask(what, [this, id](const lmp::ProbeAnswer& answer) {
        // A port that refuses is a PC that answers: it is on.
        pcAnswered(id, answer.result == Status::Ok || answer.result == Status::Denied);
      });
      if (asked == Status::Ok) {
        p.asking = true;
      } else {
        p.nextMs = nowMs_ + 3000;
      }
    }
  }

  void pcAnswered(uint8_t id, bool on) {
    Pc* p = pcOf(id);
    if (!p) return;
    p->asking = false;
    p->nextMs = nowMs_ + (p->wakeMs ? kWakingEveryMs : kPcEveryMs);
    if (!on && !networkUp()) return;   // out of sight, not off
    if (on && p->wakeMs) {
      p->wokeAfterMs = nowMs_ - p->wakeMs;
      p->wakeMs = 0;
    }
    if (!p->known || p->on != on) {
      const bool told = p->known;   // the first answer only says how it is
      p->known = true;
      p->on = on;
      p->sinceMs = nowMs_;
      markChanged();
      if (!told) return;
      Event& e = addEvent(on ? Ev::PcOn : Ev::PcOff, p->id, p->name);
      memcpy(e.ip, p->ip.b, 4);
      memcpy(e.mac, p->mac, 6);
      if (!settings_->notifyPc) return;
      char text[120];
      snprintf(text, sizeof(text), "\xF0\x9F\x92\xBB ExpNet: %s %s", p->name,
               on ? (italian() ? "acceso" : "is on") : (italian() ? "spento" : "is off"));
      say(text);
    }
  }

  // ---------------------------------------------------------------- the list of the network

  bool isNew(const Host& h, uint32_t now) const {
    return !(h.flags & (kFromStart | kSelf)) && h.firstSeen && now && now - h.firstSeen < kDaySec;
  }

  // The list in memory, read from its file the first time it is wanted: one buffer with the head
  // of the file and its devices, as large as needed and some places more. False without memory.
  bool loadCache() {
    cacheUsedMs_ = nowMs_;
    if (cache_) return true;
    lmp::IStorage* storage = ctx().storage;
    size_t size = 0;
    if (storage && storage->exists(kHostsFile)) {
      char none[1];
      storage->readFile(kHostsFile, none, sizeof(none), size);   // too small: it says the size
    }
    const size_t stored = size > sizeof(HostsHead) ? (size - sizeof(HostsHead)) / sizeof(Host) : 0;
    if (!growHosts(static_cast<uint16_t>(stored + kHostsStep))) return false;
    head_ = HostsHead();
    cacheCount_ = 0;
    size_t len = 0;
    if (stored && storage->readFile(kHostsFile, hostsBuf_, hostsBytes(cacheCap_) + 1, len) == Status::Ok &&
        len >= sizeof(HostsHead)) {
      HostsHead head;
      memcpy(&head, hostsBuf_, sizeof(head));
      // Lists of before: the places of the ports and of the web port were the end of the
      // server; 1.0.0 had no web port (DBI1), the first tries of 1.0.1 no ports (DBI2).
      const bool v1 = memcmp(head.magic, "DBI1", 4) == 0;
      const bool v2 = memcmp(head.magic, "DBI2", 4) == 0;
      if ((v1 || v2 || memcmp(head.magic, head_.magic, 4) == 0) && head.count <= cacheCap_ &&
          len == sizeof(HostsHead) + sizeof(Host) * head.count) {
        memcpy(head.magic, head_.magic, 4);
        head_ = head;
        cacheCount_ = head.count;
        for (uint16_t i = 0; (v1 || v2) && i < cacheCount_; ++i) {
          cache_[i].server[sizeof(cache_[i].server) - 1] = '\0';
          cache_[i].ports = 0;
          if (v1) cache_[i].webPort = 0;
        }
      }
    }
    return true;
  }

  static size_t hostsBytes(uint16_t places) { return sizeof(HostsHead) + sizeof(Host) * places; }

  // Room for at least places devices, at most kHostsMax: what is there comes along.
  bool growHosts(uint16_t places) {
    if (places < kHostsStep) places = kHostsStep;
    if (places > kHostsMax) places = kHostsMax;
    if (cache_ && places <= cacheCap_) return true;
    auto* bigger = static_cast<char*>(lmp::hal::allocLarge(hostsBytes(places) + 1));
    if (!bigger) return false;
    if (hostsBuf_) {
      memcpy(bigger, hostsBuf_, hostsBytes(cacheCount_));
      lmp::hal::freeLarge(hostsBuf_);
    }
    hostsBuf_ = bigger;
    cache_ = reinterpret_cast<Host*>(hostsBuf_ + sizeof(HostsHead));
    cacheCap_ = places;
    return true;
  }

  void dropCache() {
    if (hostsBuf_) lmp::hal::freeLarge(hostsBuf_);
    hostsBuf_ = nullptr;
    cache_ = nullptr;
    cacheCount_ = 0;
    cacheCap_ = 0;
  }

  // The list and its counts on file, from the buffer itself: no copy.
  void writeHosts() {
    lmp::IStorage* storage = ctx().storage;
    if (!storage || !cache_) return;
    head_.count = cacheCount_;
    memcpy(hostsBuf_, &head_, sizeof(head_));
    Status s = storage->writeFile(kHostsFile, lmp::StringView(hostsBuf_, hostsBytes(cacheCount_)));
    Counts c;
    c.known = knownCount_;
    c.online = onlineCount_;
    c.fresh = newCount_;
    c.scanUnix = lastScanUnix_;
    if (s == Status::Ok) s = storage->writeFile(kCountFile, lmp::StringView(reinterpret_cast<const char*>(&c), sizeof(c)));
    if (s != Status::Ok) LMP_LOGE(ctx().log, "network not written: %s", lmp::statusName(s));
  }

  // How many there are, how many the last search found, how many are new: for the page and the
  // chat, without the list in memory.
  void countHosts() {
    const uint32_t now = static_cast<uint32_t>(unixNow());
    knownCount_ = onlineCount_ = newCount_ = 0;
    for (uint16_t i = 0; i < cacheCount_; ++i) {
      const Host& h = cache_[i];
      ++knownCount_;
      if (h.flags & kOnline) ++onlineCount_;
      if (isNew(h, now)) ++newCount_;
    }
  }

  // At the start: the counts of the last search, without the list.
  void readCounts() {
    lmp::IStorage* storage = ctx().storage;
    char raw[sizeof(Counts) + 1];
    size_t len = 0;
    if (!storage || !storage->exists(kCountFile) ||
        storage->readFile(kCountFile, raw, sizeof(raw), len) != Status::Ok || len != sizeof(Counts)) {
      return;
    }
    Counts c;
    memcpy(&c, raw, sizeof(c));
    knownCount_ = c.known;
    onlineCount_ = c.online;
    newCount_ = c.fresh;
    lastScanUnix_ = c.scanUnix;
  }

  // The device of the list with this MAC, or with this address when the MAC is not known.
  Host* hostOf(const Ip4& ip, const uint8_t* mac) {
    for (uint16_t i = 0; i < cacheCount_; ++i) {
      Host& h = cache_[i];
      if (mac && !noMac(mac) ? memcmp(h.mac, mac, 6) == 0 : (h.ip == ip && noMac(h.mac))) return &h;
    }
    return nullptr;
  }

  // The device of the list at this address. Found without its MAC: the one seen by this search,
  // else the one seen last there. Found with a MAC never seen: the one there without a MAC.
  Host* atAddress(const Ip4& ip, bool withMac) {
    Host* last = nullptr;
    for (uint16_t i = 0; i < cacheCount_; ++i) {
      Host& h = cache_[i];
      if (!(h.ip == ip)) continue;
      if (withMac) {
        if (noMac(h.mac)) return &h;
        continue;
      }
      if (h.flags & kSeenNow) return &h;
      if (!last || h.lastSeen > last->lastSeen) last = &h;
    }
    return last;
  }

  // A device found by the search that runs: its place in the list, made when it is not there.
  // A full list gives the place of the one not seen for the longest time.
  Host* found(const Ip4& ip, const uint8_t* mac) {
    const bool withMac = mac && !noMac(mac);
    Host* h = withMac ? hostOf(ip, mac) : nullptr;
    // Without its MAC (mDNS, SSDP), or with a MAC never seen: the one at that address. A device
    // asleep misses the ARP of a search and answers mDNS all the same: it stays one.
    if (!h) h = atAddress(ip, withMac);
    const uint32_t now = static_cast<uint32_t>(unixNow());
    if (!h) {
      // A network larger than the list: the list grows, while it can.
      if (cacheCount_ == cacheCap_) growHosts(static_cast<uint16_t>(cacheCap_ + kHostsStep));
      if (cacheCount_ < cacheCap_) {
        h = &cache_[cacheCount_++];
      } else {
        Host* oldest = nullptr;
        for (uint16_t i = 0; i < cacheCount_; ++i) {
          Host& x = cache_[i];
          if (x.flags & (kSeenNow | kSelf)) continue;
          if (!oldest || x.lastSeen < oldest->lastSeen) oldest = &x;
        }
        if (!oldest) return nullptr;
        h = oldest;
      }
      *h = Host();
      if (mac) memcpy(h->mac, mac, 6);
      h->firstSeen = now;
      h->flags = head_.baseline ? kNewNow : kFromStart;
    }
    if (withMac && noMac(h->mac)) memcpy(h->mac, mac, 6);
    h->ip = ip;
    h->lastSeen = now ? now : h->lastSeen;
    h->flags = static_cast<uint8_t>(h->flags | kSeenNow);
    return h;
  }

  Status startScan(uint32_t ref, bool automatic) {
    if (!automatic) usedMs_ = nowMs_;
    lmp::IProbeService* asker = probe();
    if (!asker || !asker->ready()) return Status::Denied;
    if (scan_ != Scan::Idle && scan_ != Scan::Done) return Status::Busy;
    Ip4 address, mask;
    if (!asker->network(address, mask)) return Status::Denied;
    if (!loadCache()) return Status::NoMem;
    // The times of the last search stay in the list until this one has its own.
    for (uint16_t i = 0; i < cacheCount_; ++i) {
      cache_[i].flags = static_cast<uint8_t>(cache_[i].flags & ~(kSeenNow | kNewNow));
    }
    scanCount_ = 0;
    scanRef_ = ref;
    scanAuto_ = automatic;
    scanStartMs_ = nowMs_;
    // The devices of the system: the net service looks for them meanwhile. Not on the boards of the
    // least memory: together with the ARP of the search it took the memory of a C3 down to 3 KB.
    // There the net service looks for them every 6 hours and when asked, and
    // the list remembers who was one.
    lmp::INetService* net = ctx().net;
    if (net && LMP_MODULE_MEM >= 2) net->findPeers();
    // The addresses of the network, at most the 254 around the device: a network larger than
    // a /24 is asked only there.
    Ip4 first = address;
    uint16_t count = 254;
    if (mask.b[3] != 0 || (mask.b[0] == 255 && mask.b[1] == 255 && mask.b[2] == 255)) {
      const uint8_t hostBits = static_cast<uint8_t>(~mask.b[3]);
      first.b[3] = static_cast<uint8_t>(address.b[3] & mask.b[3]);
      count = hostBits > 1 ? static_cast<uint16_t>(hostBits - 1) : 1;
    } else {
      first.b[3] = 0;
    }
    first.b[3] = static_cast<uint8_t>(first.b[3] + 1);
    lmp::ProbeAsk what;
    what.kind = lmp::ProbeKind::Sweep;
    what.ip = first;
    what.count = count;
    what.timeoutMs = kSweepWaitMs;
    LMP_TRY(asker->ask(what, [this](const lmp::ProbeAnswer& answer) { swept(answer); }));
    scan_ = Scan::Sweep;
    scanAt_ = 0;
    markChanged();
    return ref ? Status::Pending : Status::Ok;
  }

  void autoScan() {
    if (!settings_->scanEveryMin || (scan_ != Scan::Idle && scan_ != Scan::Done)) {
      return;
    }
    if (static_cast<int32_t>(nowMs_ - nextScanMs_) < 0) return;
    // Nobody looks and nobody is told: the search waits for the first command, which starts it
    // at once, its time being past.
    if (resting()) return;
    nextScanMs_ = nowMs_ + settings_->scanEveryMin * 60000u;
    startScan(0, true);
  }

  // The search rests when nobody used the module for half an hour (a page, the remote console,
  // the serial, the chat) and nothing gets what it finds: no message of Telegram that tells of
  // it (a new device, the summary of the morning, the commands of the chat), no home system and
  // no server connected, which show how many devices are in the network. The watched services
  // and the PCs never rest: they are watched.
  bool resting() {
    if (nowMs_ - usedMs_ < kIdleMs) return false;
    if (telegramReady() && (settings_->notifyNew || settings_->summaryHour < 24 || settings_->botCommands)) {
      return false;
    }
    if (ctx().mqtt && ctx().mqtt->connected()) return false;
    const lmp::ICloudService* cloud = kernel_ ? kernel_->api<lmp::ICloudService>() : nullptr;
    return !(cloud && cloud->connected());
  }

  void swept(const lmp::ProbeAnswer& answer) {
    if (!cache_ && !loadCache()) return endScan(Status::NoMem);
    for (const lmp::ProbeHost& h : answer.hosts) found(h.ip, h.mac);
    // Who missed the ARP is not asked with ping: no time in this search. The device itself
    // never answers its own ARP.
    for (uint16_t i = 0; i < cacheCount_; ++i) {
      if (!(cache_[i].flags & (kSeenNow | kSelf))) cache_[i].pingMs = -1;
    }
    // The one asked with ping: everybody found now, in the order of the list.
    scanCount_ = cacheCount_;
    scan_ = Scan::Ping;
    scanAt_ = 0;
    nextScanStep();
  }

  // One question at a time, each one asked when the one before is answered.
  void nextScanStep() {
    lmp::IProbeService* asker = probe();
    if (!asker || !cache_) return endScan(Status::Denied);
    cacheUsedMs_ = nowMs_;
    lmp::ProbeAsk what;
    if (scan_ == Scan::Ping) {
      while (scanAt_ < scanCount_ && !(cache_[scanAt_].flags & kSeenNow)) ++scanAt_;
      if (scanAt_ >= scanCount_) {
        scan_ = Scan::Mdns;
        scanAt_ = 0;
        markChanged();
        return nextScanStep();
      }
      what.kind = lmp::ProbeKind::Ping;
      what.ip = cache_[scanAt_].ip;
      what.timeoutMs = kScanPingMs;
      if (asker->ask(what, [this](const lmp::ProbeAnswer& a) {
            if (cache_ && scanAt_ < cacheCount_) {
              int16_t ms = -1;
              if (a.result == Status::Ok) ms = static_cast<int16_t>(a.rttMs > 30000 ? 30000 : a.rttMs);
              cache_[scanAt_].pingMs = ms;
            }
            ++scanAt_;
            nextScanStep();
          }) != Status::Ok) {
        endScan(Status::Busy);
      }
      return;
    }
    if (scan_ == Scan::Mdns) {
      if (scanAt_ >= kMdnsCount) {
        scan_ = Scan::Ssdp;
        scanAt_ = 0;
        markChanged();
        return nextScanStep();
      }
      what.kind = lmp::ProbeKind::Mdns;
      what.service = kMdnsKinds[scanAt_].service;
      what.proto = "_tcp";
      what.timeoutMs = kMdnsWaitMs;
      if (asker->ask(what, [this](const lmp::ProbeAnswer& a) {
            for (const lmp::hal::MdnsHit& hit : a.services) foundService(hit, scanAt_);
            ++scanAt_;
            nextScanStep();
          }) != Status::Ok) {
        endScan(Status::Busy);
      }
      return;
    }
    if (scan_ == Scan::Ports) {
      // The ports of cameras and recorders, of every device found but this one, the LoopMaxPro
      // devices and the phones.
      while (kVideoAsked && scanAt_ < cacheCount_ && !videoAsked(cache_[scanAt_])) ++scanAt_;
      if (!kVideoAsked || scanAt_ >= cacheCount_) return endScan(Status::Ok);
      what.kind = lmp::ProbeKind::Ports;
      what.ip = cache_[scanAt_].ip;
      what.ports = kVideoPorts;
      what.count = kVideoCount;
      what.open = videoOpen_;
      what.timeoutMs = kVideoWaitMs;
      if (asker->ask(what, [this](const lmp::ProbeAnswer& a) {
            if (cache_ && scanAt_ < cacheCount_ && a.result == Status::Ok) {
              uint16_t bits = 0;
              for (uint8_t i = 0; i < kVideoCount; ++i) {
                if (videoOpen_[i] == 1) bits = static_cast<uint16_t>(bits | (1u << i));
              }
              cache_[scanAt_].ports = bits;
            }
            ++scanAt_;
            nextScanStep();
          }) != Status::Ok) {
        endScan(Status::Busy);
      }
      return;
    }
    if (scan_ == Scan::Ssdp) {
      // Everybody first, for what they say of themselves; then one kind at a time, for what
      // they are.
      if (scanAt_ > kSsdpAsked) {
        scan_ = Scan::Ports;
        scanAt_ = 0;
        markChanged();
        return nextScanStep();
      }
      what.kind = lmp::ProbeKind::Ssdp;
      what.service = scanAt_ ? kSsdpKinds[scanAt_ - 1].st : nullptr;
      what.timeoutMs = scanAt_ ? kSsdpKindWaitMs : kSsdpWaitMs;
      if (asker->ask(what, [this](const lmp::ProbeAnswer& a) {
            for (const lmp::hal::SsdpHit& hit : a.devices) foundDevice(hit, scanAt_);
            ++scanAt_;
            nextScanStep();
          }) != Status::Ok) {
        endScan(Status::Busy);
      }
    }
  }

  void foundService(const lmp::hal::MdnsHit& hit, uint8_t kind) {
    if (!cache_ || !hit.ip.isSet()) return;
    // A device that does not answer ARP but announces itself is in the network all the same.
    Host* h = found(hit.ip, nullptr);
    if (!h) return;
    if (!(h->flags & kSeenNow)) return;
    const uint16_t bit = static_cast<uint16_t>(1u << kind);
    // Its web page: the port of _http; of Home Assistant when it has no _http.
    if (hit.port && (bit == kindBit("web") || (bit == kindBit("homeassistant") && !(h->kinds & kindBit("web"))))) {
      h->webPort = hit.port;
    }
    h->kinds = static_cast<uint16_t>(h->kinds | bit);
    // Names are new at each search: the last one said wins, the first of this search.
    if (!(h->flags & kNewNow) || !h->name[0]) copyText(h->name, sizeof(h->name), hit.host[0] ? hit.host : hit.instance);
  }

  // An answer to SSDP: what a device says of itself to the search of everybody (step 0), or the
  // kind it is to the search of that kind.
  void foundDevice(const lmp::hal::SsdpHit& hit, uint8_t step) {
    if (!cache_ || !hit.ip.isSet()) return;
    Host* h = found(hit.ip, nullptr);
    if (!h) return;
    if (step) {
      // Only who says to be that kind: some devices answer every search with what they are. The
      // kind they say can be cut short.
      const char* want = kSsdpKinds[step - 1].st;
      if (!hit.st[0] || strncmp(want, hit.st, strlen(hit.st)) != 0) return;
      h->kinds = static_cast<uint16_t>(h->kinds | (1u << (kMdnsCount + step - 1)));
      return;
    }
    if (hit.server[0]) copyText(h->server, sizeof(h->server), hit.server);
  }

  // The device itself is in the network too: it does not answer its own ARP.
  void addSelf() {
    if (!kernel_ || !kernel_->board().net) return;
    lmp::hal::StaInfo sta;
    if (!station(sta)) return;
    lmp::hal::Mac mac;
    kernel_->board().net->staMac(mac);
    Host* h = found(sta.ip, mac.b);
    if (!h) return;
    h->flags = static_cast<uint8_t>((h->flags | kSelf) & ~kNewNow);
    h->pingMs = 0;
    copyText(h->name, sizeof(h->name), kernel_->config().sys.hostname.c_str());
  }

  void endScan(Status outcome) {
    if (cache_) {
      addSelf();
      mergeTwins();
      // The LoopMaxPro devices, remembered as such for when they are out of sight.
      for (uint16_t i = 0; i < cacheCount_; ++i) {
        Host& h = cache_[i];
        const lmp::hal::NetPeer* peer = (h.flags & kSeenNow) ? peerAt(h.ip) : nullptr;
        if (!peer) continue;
        h.flags = static_cast<uint8_t>((h.flags | kSystem) & ~kProduct);
        if (peer->code[0]) h.flags = static_cast<uint8_t>(h.flags | kProduct);
      }
      // Found now: in the network; the others are not, and keep what they were.
      Host* fresh[8];
      uint8_t freshCount = 0;
      uint16_t freshMore = 0;
      for (uint16_t i = 0; i < cacheCount_; ++i) {
        Host& h = cache_[i];
        h.flags = static_cast<uint8_t>(h.flags & ~kOnline);
        if (h.flags & kSeenNow) h.flags = static_cast<uint8_t>(h.flags | kOnline);
        if (h.flags & kNewNow) {
          if (freshCount < 8) fresh[freshCount++] = &h;
          else ++freshMore;
        }
      }
      // In the order of the addresses, as who reads a network expects; the ones here first.
      for (uint16_t i = 1; i < cacheCount_; ++i) {
        for (uint16_t j = i; j > 0 && before(cache_[j], cache_[j - 1]); --j) {
          const Host t = cache_[j];
          cache_[j] = cache_[j - 1];
          cache_[j - 1] = t;
          for (uint8_t k = 0; k < freshCount; ++k) {
            if (fresh[k] == &cache_[j]) fresh[k] = &cache_[j - 1];
            else if (fresh[k] == &cache_[j - 1]) fresh[k] = &cache_[j];
          }
        }
      }
      if (outcome == Status::Ok) {
        head_.baseline = 1;
        head_.scanUnix = static_cast<uint32_t>(unixNow());
        lastScanUnix_ = head_.scanUnix;
      }
      for (uint8_t k = 0; k < freshCount; ++k) newDevice(*fresh[k]);
      if (freshMore && settings_->notifyNew) {
        char text[120];
        snprintf(text, sizeof(text), italian() ? "\xF0\x9F\x86\x95 ExpNet: e altri %u dispositivi nuovi."
                                               : "\xF0\x9F\x86\x95 ExpNet: and %u more new devices.",
                 static_cast<unsigned>(freshMore));
        say(text);
      }
      for (uint16_t i = 0; i < cacheCount_; ++i) {
        cache_[i].flags = static_cast<uint8_t>(cache_[i].flags & ~(kSeenNow | kNewNow));
      }
      countHosts();
      writeHosts();
    }
    scan_ = Scan::Done;
    scanTookMs_ = nowMs_ - scanStartMs_;
    LMP_LOGI(ctx().log, "network: %u devices in %lu s, %u new", static_cast<unsigned>(onlineCount_),
             static_cast<unsigned long>(scanTookMs_ / 1000), static_cast<unsigned>(newCount_));
    if (scanRef_) kernel_->events().publish(lmp::commandDone(scanRef_, outcome));
    scanRef_ = 0;
    markChanged();
  }

  // A device kept twice, with its MAC and without it at the same address, as the lists of 1.0.0
  // could: one place, with what both knew.
  void mergeTwins() {
    for (uint16_t i = 0; i < cacheCount_;) {
      const Host& x = cache_[i];
      Host* twin = nullptr;
      for (uint16_t j = 0; noMac(x.mac) && j < cacheCount_ && !twin; ++j) {
        if (j != i && cache_[j].ip == x.ip && !noMac(cache_[j].mac)) twin = &cache_[j];
      }
      if (!twin) {
        ++i;
        continue;
      }
      twin->kinds = static_cast<uint16_t>(twin->kinds | x.kinds);
      if (!twin->webPort) twin->webPort = x.webPort;
      if (!twin->type) twin->type = x.type;
      if (!twin->ports) twin->ports = x.ports;
      if (!twin->name[0]) memcpy(twin->name, x.name, sizeof(x.name));
      if (!twin->alias[0]) memcpy(twin->alias, x.alias, sizeof(x.alias));
      if (!twin->server[0]) memcpy(twin->server, x.server, sizeof(x.server));
      if (x.firstSeen && (!twin->firstSeen || x.firstSeen < twin->firstSeen)) twin->firstSeen = x.firstSeen;
      if (x.lastSeen > twin->lastSeen) twin->lastSeen = x.lastSeen;
      if (twin->pingMs < 0) twin->pingMs = x.pingMs;
      // Seen by this search when one of them was; new only when both were.
      const uint8_t either = static_cast<uint8_t>(twin->flags | x.flags);
      const uint8_t both = static_cast<uint8_t>(twin->flags & x.flags);
      twin->flags = static_cast<uint8_t>((either & (kSeenNow | kFromStart | kSelf | kSystem | kProduct)) | (both & kNewNow));
      memmove(&cache_[i], &cache_[i + 1], sizeof(Host) * (cacheCount_ - i - 1));
      --cacheCount_;
    }
  }

  // Who is asked the ports of cameras: found by this search, not this device, not a LoopMaxPro
  // (it says what it is), not a phone (a random MAC).
  bool videoAsked(const Host& h) const {
    return (h.flags & kSeenNow) && !(h.flags & kSelf) && !(h.mac[0] & 0x02) && !peerAt(h.ip);
  }

  static bool before(const Host& a, const Host& b) {
    if (key(a.ip) != key(b.ip)) return key(a.ip) < key(b.ip);
    return (a.flags & kOnline) > (b.flags & kOnline);
  }

  void newDevice(const Host& h) {
    const char* name = h.alias[0] ? h.alias : h.name[0] ? h.name : h.server;
    Event& e = addEvent(Ev::NewDevice, 0, name);
    memcpy(e.ip, h.ip.b, 4);
    memcpy(e.mac, h.mac, 6);
    if (!settings_->notifyNew) return;
    char ip[16], mac[18], text[200];
    h.ip.toString(ip, sizeof(ip));
    lmp::macToString(h.mac, mac, sizeof(mac));
    const bool random = (h.mac[0] & 0x02) != 0;
    snprintf(text, sizeof(text), italian() ? "\xF0\x9F\x86\x95 ExpNet: nuovo dispositivo in rete: %s %s%s%s%s"
                                           : "\xF0\x9F\x86\x95 ExpNet: a new device in the network: %s %s%s%s%s",
             ip, noMac(h.mac) ? "" : mac, *name ? " \xC2\xB7 " : "", name,
             random ? (italian() ? " (MAC casuale: un telefono?)" : " (random MAC: a phone?)") : "");
    say(text);
  }

  // A device of the system at this address, from the last search of the net service.
  const lmp::hal::NetPeer* peerAt(const Ip4& ip) const {
    lmp::INetService* net = const_cast<ExpNet*>(this)->ctx().net;
    if (!net) return nullptr;
    for (const lmp::hal::NetPeer& peer : net->peers()) {
      if (peer.ip == ip) return &peer;
    }
    return nullptr;
  }

  Status hostsPage(uint16_t from, lmp::CmdContext& ctx) {
    usedMs_ = nowMs_;   // somebody looks
    if (!ctx.result) return Status::Ok;
    if (!loadCache()) return Status::NoMem;
    const uint32_t now = static_cast<uint32_t>(unixNow());
    lmp::JsonWriter& w = *ctx.result;
    w.str("state", scanName(scan_));
    w.u32("count", cacheCount_);
    if (now) w.i64("nowUnix", now);
    w.beginArray("hosts");
    char text[18];
    uint16_t i = from;
    for (; i < cacheCount_ && i < from + kHostsPage; ++i) {
      const Host& h = cache_[i];
      w.beginObject();
      h.ip.toString(text, sizeof(text));
      w.str("ip", text);
      if (!noMac(h.mac)) {
        lmp::macToString(h.mac, text, sizeof(text));
        w.str("mac", text);
        if (h.mac[0] & 0x02) w.boolean("random", true);
      }
      w.boolean("online", (h.flags & kOnline) != 0);
      if (h.flags & kSelf) w.boolean("self", true);
      if (isNew(h, now)) w.boolean("new", true);
      if (h.pingMs >= 0 && (h.flags & kOnline)) w.u32("pingMs", static_cast<uint32_t>(h.pingMs));
      if (h.name[0]) w.str("name", h.name);
      if (h.alias[0]) w.str("alias", h.alias);
      if (h.server[0]) w.str("server", h.server);
      if (h.webPort) w.u32("webPort", h.webPort);
      if (h.ports) {
        w.beginArray("ports");
        for (uint8_t i = 0; i < kVideoCount; ++i) {
          if (h.ports & (1u << i)) w.u64(kVideoPorts[i]);
        }
        w.endArray();
      }
      if (h.firstSeen) w.i64("firstUnix", h.firstSeen);
      if (h.lastSeen) w.i64("lastUnix", h.lastSeen);
      if (h.kinds & ((1u << kMdnsCount) - 1)) {
        w.beginArray("services");
        for (uint8_t k = 0; k < kMdnsCount; ++k) {
          if (h.kinds & (1u << k)) w.str(lmp::StringView(kMdnsKinds[k].word));
        }
        w.endArray();
      }
      // What it is, to SSDP.
      if (h.kinds >> kMdnsCount) {
        w.beginArray("upnp");
        for (uint8_t k = 0; k < kSsdpCount; ++k) {
          if (h.kinds & (1u << (kMdnsCount + k))) w.str(lmp::StringView(kSsdpKinds[k].word));
        }
        w.endArray();
      }
      if (h.type && h.type <= kTypeCount) w.str("type", kTypes[h.type - 1]);
      // A device of the system: what it is, its version, where its console is.
      if (const lmp::hal::NetPeer* peer = peerAt(h.ip)) {
        w.beginObject("loopmax");
        w.str("name", peer->name);
        if (peer->title[0]) w.str("title", peer->title);
        if (peer->icon[0]) w.str("icon", peer->icon);
        if (peer->fw[0]) w.str("fw", peer->fw);
        if (peer->board[0]) w.str("board", peer->board);
        if (peer->code[0]) w.boolean("pro", true);   // a product, with its private modules
        w.str("host", peer->host);
        w.u32("port", peer->port);
        w.endObject();
      } else if (h.flags & kSystem) {
        // Out of sight now: what it was.
        w.str("system", (h.flags & kProduct) ? "pro" : "free");
      }
      // Watched, or a PC to wake: what ExpNet does with it already.
      for (const Service& s : services_) {
        if (!s.id || !(s.ip == h.ip) || s.check == Check::Http) continue;
        w.beginObject("watch");
        w.u32("id", s.id);
        if (s.known) w.boolean("up", s.up);
        w.endObject();
        break;
      }
      for (const Pc& p : pcs_) {
        if (!p.id || memcmp(p.mac, h.mac, 6) != 0 || noMac(h.mac)) continue;
        w.beginObject("pc");
        w.u32("id", p.id);
        if (p.known) w.boolean("on", p.on);
        w.endObject();
        break;
      }
      w.endObject();
    }
    w.endArray();
    if (i < cacheCount_) w.u32("next", i);
    return Status::Ok;
  }

  // The extended table of the makers, a page at a time; empty on the boards without room.
  static Status makersPage(uint16_t from, lmp::CmdContext& ctx) {
    if (!ctx.result) return Status::Ok;
    lmp::JsonWriter& w = *ctx.result;
    w.beginArray("makers");
#if LMP_MODULE_MEM >= 2
    size_t used = 0;
    uint16_t i = from;
    for (; i < kMakerCount && (i == from || used + strlen(kMakers[i].packed) <= kMakersPage); ++i) {
      used += strlen(kMakers[i].packed);
      w.beginArray();
      w.str(lmp::StringView(kMakers[i].name));
      w.str(lmp::StringView(kMakers[i].type));
      w.str(lmp::StringView(kMakers[i].packed));
      w.endArray();
    }
    w.endArray();
    if (i < kMakerCount) w.u32("next", i);
#else
    (void)from;
    w.endArray();
#endif
    return Status::Ok;
  }

  Status setAlias(const lmp::Args& a, lmp::CmdContext& ctx) {
    Ip4 ip;
    if (!Ip4::parse(a.str("ip"), ip)) {
      ctx.errorField = "ip";
      return Status::Invalid;
    }
    uint8_t mac[6] = {};
    if (a.has("mac") && *a.str("mac") && !lmp::parseMac(a.str("mac"), mac)) {
      ctx.errorField = "mac";
      return Status::Invalid;
    }
    // A type of the list, or nothing: the page guesses it again.
    const uint8_t type = a.has("type") && *a.str("type") ? typeCode(a.str("type")) : 0;
    if (a.has("type") && *a.str("type") && !type) {
      ctx.errorField = "type";
      return Status::Invalid;
    }
    if (!a.has("alias") && !a.has("type")) {
      ctx.errorField = "alias";
      return Status::Invalid;
    }
    if (!loadCache()) return Status::NoMem;
    Host* h = hostOf(ip, mac);
    if (!h) return Status::NotFound;
    if (a.has("alias")) copyText(h->alias, sizeof(h->alias), a.str("alias"));
    if (a.has("type")) h->type = type;
    writeHosts();
    markChanged();
    return Status::Ok;
  }

  // ---------------------------------------------------------------- live

  struct Live {
    uint32_t askedMs = 0;        // the last look: the times are taken while somebody looks
    uint32_t nextMs = 0;
    bool gwAsking = false, netAsking = false;
    int16_t gw[kLiveSlots] = {};
    int16_t net[kLiveSlots] = {};
    int8_t rssi[kLiveSlots] = {};
    uint8_t at = 0, filled = 0;
    uint32_t seq = 0;            // samples taken so far: the page knows how many are new
    uint32_t gridMs = 0;         // when the newest sample was due: the page places it there
    Ip4 gateway;
  };

  // ---------------------------------------------------------------- the traffic of the router

  // The router of the house, when it says its counters with UPnP: found with SSDP, its
  // description read once, then a round of questions every kRouterEveryMs while somebody looks.
  // Without UPnP the traffic is asked to what else may say it (wan.h): the dish of Starlink, a
  // 4G or 5G router of Huawei, a router with SNMP, tried in this order.
  struct Router {
    enum State : uint8_t { Unknown, Searching, Reading, Ready, None };
    State state = Unknown;
    Ip4 ip;
    char location[64] = "";
    char control[112] = "";
    char service[64] = "";
    uint32_t retryMs = 0, nextMs = 0;
    uint32_t roundGridMs = 0, gridMs = 0;   // when the round was due; the one of the newest sample
    bool asking = false;
    uint8_t plan[5] = {};        // the questions of this round (kRouterActions), in order
    uint8_t planN = 0, planAt = 0;
    uint8_t fails = 0;
    uint32_t round = 0, roundMs = 0, lastMs = 0, fullMs = 0;   // lastMs: when the round before began
    // The counters: bytes received and sent, packets received and sent. What each was, what it
    // grew since it was read before, in which round, and whether that was the round before.
    uint32_t last[4] = {}, delta[4] = {}, readIn[4] = {};
    bool chain[4] = {};
    bool topRx = false, topTx = false;       // a byte counter stuck at its top: its packets instead
    bool byPkRx = false, byPkTx = false;     // this round tells by the packets
    bool estimate = false, estimateUp = false;
    uint32_t maxDown = 0, maxUp = 0;   // the line, bit/s
    uint32_t down[kRouterSlots] = {};  // kbit/s
    uint32_t up[kRouterSlots] = {};
    uint8_t at = 0, filled = 0;
    uint32_t seq = 0;
    // Who says the traffic; while a device is looked for, the kind being tried.
    enum Via : uint8_t { Upnp, Starlink, Huawei, Snmp };
    Via via = Upnp;
    Ip4 gateway, asked;             // the router of the network; the device being asked
    uint16_t port = 0;              // its port, when the settings say one; 0: the usual of its kind
    uint32_t askedMs = 0;
    uint8_t step = 0;               // Huawei: 1 a session asked, 2 with it. SNMP: the table of the routes
    uint32_t snmpIf = 0, snmpId = 0;
    bool snmpOld = false;           // counters of 32 bit: the router has not the ones of 64
    bool snmpHeard = false;         // a router answers SNMP: only its port is not known
    uint64_t counted[2] = {};       // bytes in and out, and when they were read
    uint32_t countedMs = 0;
    wan::Starlink dish;
  };

  static const char* routerStateName(Router::State s) {
    switch (s) {
      case Router::Searching:
      case Router::Reading:
      case Router::Unknown: return "searching";
      case Router::Ready:   return "ok";
      case Router::None:    return "none";
    }
    return "none";
  }

  void sampleRouter(const lmp::hal::StaInfo& sta) {
    Router& r = router_;
    if (r.asking) return;
    if (r.state == Router::None && static_cast<int32_t>(nowMs_ - r.retryMs) < 0) return;
    if (r.state == Router::Unknown || r.state == Router::None) return findRouter(sta);
    if (r.state != Router::Ready) return;
    if (r.via != Router::Upnp) return sampleDevice(r);
    if (r.planAt >= r.planN) {
      // On a grid, to the nearest tick: a round every kRouterEveryMs on average, and the page
      // places each one where it was due. Far behind, the grid starts again here.
      const int32_t late = static_cast<int32_t>(nowMs_ - r.nextMs);
      if (late < -static_cast<int32_t>(kTickMs / 2)) return;
      if (late > static_cast<int32_t>(kRouterEveryMs)) r.nextMs = nowMs_;
      r.lastMs = r.roundMs;
      r.roundMs = nowMs_;
      r.roundGridMs = r.nextMs;
      r.nextMs += kRouterEveryMs;
      planRound(r);
    }
    askRouter(kRouterActions[r.plan[r.planAt]]);
  }

  // The questions of a round: for each way the bytes, or the packets when the byte counter is
  // stuck at its top. The first round, and one a minute, all four and the line: a counter that
  // stops or starts again is seen, the line is known.
  void planRound(Router& r) {
    const bool full = r.round == 0 || nowMs_ - r.fullMs >= kRouterFullMs;
    ++r.round;
    r.planN = 0;
    r.planAt = 0;
    r.byPkRx = r.topRx;
    r.byPkTx = r.topTx;
    if (full) {
      r.fullMs = nowMs_;
      r.plan[r.planN++] = kRouterLine;
    }
    if (full || !r.byPkRx) r.plan[r.planN++] = 0;
    if (full || !r.byPkTx) r.plan[r.planN++] = 1;
    if (full || r.byPkRx) r.plan[r.planN++] = 2;
    if (full || r.byPkTx) r.plan[r.planN++] = 3;
  }

  void findRouter(const lmp::hal::StaInfo& sta) {
    lmp::IProbeService* asker = probe();
    if (!asker || !asker->ready()) return;
    router_.via = Router::Upnp;
    router_.gateway = sta.gateway;
    router_.snmpHeard = false;
    router_.maxDown = router_.maxUp = 0;
    if (settings_->trafficFrom >= 2) {
      // The kind of device chosen by the owner: only that one is asked.
      router_.via = static_cast<Router::Via>(settings_->trafficFrom - 1);
      router_.state = Router::Searching;
      return tryDevice(router_);
    }
    // A device chosen by the owner: asked by itself, in another subnet too.
    if (!settings_->upnpHost.empty()) return askRouterAt(asker);
    lmp::ProbeAsk what;
    what.kind = lmp::ProbeKind::Ssdp;
    what.service = kGatewayKind;
    what.timeoutMs = 2500;
    const uint16_t gen = routerGen_;
    if (asker->ask(what, [this, gen](const lmp::ProbeAnswer& a) {
          if (gen != routerGen_) return;
          router_.asking = false;
          // Only the router of the network: another device, an access point too, only when the
          // owner writes it in the settings. An access point that answered as a router (seen
          // on a home network) gave the address of a network of its own, and counted nothing.
          const lmp::hal::SsdpHit* best = nullptr;
          for (const lmp::hal::SsdpHit& hit : a.devices) {
            if (hit.ip == router_.gateway && strncmp(hit.location, "http://", 7) == 0) best = &hit;
          }
          if (!best) return routerMissing();
          router_.ip = best->ip;
          copyText(router_.location, sizeof(router_.location), best->location);
          readDescription();
        }) == Status::Ok) {
      router_.asking = true;
      router_.state = Router::Searching;
    }
  }

  // The UPnP device of the settings: an M-SEARCH sent to it alone, which goes through the routers
  // that the one to everybody does not cross. Its answer says where its description is.
  void askRouterAt(lmp::IProbeService* asker) {
    Ip4 ip;
    uint16_t port = 0;
    if (!deviceOfSettings(ip, port)) return routerMissing();
    char host[16];
    ip.toString(host, sizeof(host));
    const int len = snprintf(soapBody_, sizeof(soapBody_),
                             "M-SEARCH * HTTP/1.1\r\nHOST: %s:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\n"
                             "ST: %s\r\n\r\n",
                             host, kGatewayKind);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(soapBody_)) return routerMissing();
    lmp::ProbeAsk what;
    what.kind = lmp::ProbeKind::Udp;
    what.ip = ip;
    what.port = 1900;
    what.anyPort = true;   // a device answers from a port of its own
    what.data = reinterpret_cast<const uint8_t*>(soapBody_);
    what.len = static_cast<size_t>(len);
    what.reply = reinterpret_cast<uint8_t*>(soapIn_);
    what.cap = sizeof(soapIn_) - 1;
    what.timeoutMs = 2500;
    router_.asked = ip;
    const uint16_t gen = routerGen_;
    if (asker->ask(what, [this, gen](const lmp::ProbeAnswer& a) {
          if (gen != routerGen_) return;
          router_.asking = false;
          if (a.result != Status::Ok || !a.got) return routerMissing();
          soapIn_[a.got < sizeof(soapIn_) ? a.got : sizeof(soapIn_) - 1] = '\0';
          if (!headerOf(soapIn_, "LOCATION", router_.location, sizeof(router_.location)) ||
              strncmp(router_.location, "http://", 7) != 0) {
            return routerMissing();
          }
          router_.ip = router_.asked;
          readDescription();
        }) == Status::Ok) {
      router_.asking = true;
      router_.state = Router::Searching;
    }
  }

  // What was tried is not there. While a device is looked for, the next kind is tried; after
  // the last one, or when the device that was read stops answering, nothing is read, and a
  // minute later the search starts again.
  void routerMissing() {
    Router& r = router_;
    r.asking = false;
    if (settings_->trafficFrom == 0 && r.state != Router::Ready && r.via != Router::Snmp) {
      r.via = static_cast<Router::Via>(r.via + 1);
      r.state = Router::Searching;
      return tryDevice(r);
    }
    r.state = Router::None;
    r.retryMs = nowMs_ + kRouterRetryMs;
  }

  // ---------------------------------------------------------------- the devices without UPnP

  static const char* routerViaName(Router::Via via) {
    switch (via) {
      case Router::Upnp:     return "upnp";
      case Router::Starlink: return "starlink";
      case Router::Huawei:   return "huawei";
      case Router::Snmp:     return "snmp";
    }
    return "";
  }

  // The device of the settings: its address and, after a colon, its port when it is not the
  // usual one of its kind. False when the address is not one.
  bool deviceOfSettings(Ip4& ip, uint16_t& port) const {
    char text[24];
    copyText(text, sizeof(text), settings_->upnpHost.c_str());
    port = 0;
    char* colon = strchr(text, ':');
    if (colon) {
      *colon = '\0';
      port = static_cast<uint16_t>(atoi(colon + 1));
    }
    return Ip4::parse(text, ip);
  }

  // A kind of device is tried where it would be: at the address of the settings, or the dish at
  // its own, or the router of the network.
  void tryDevice(Router& r) {
    r.asked = r.gateway;
    r.port = 0;
    if (!settings_->upnpHost.empty()) {
      if (!deviceOfSettings(r.asked, r.port)) r.asked = Ip4();
    } else if (r.via == Router::Starlink) {
      Ip4::parse(wan::kStarlinkDish, r.asked);
    }
    r.step = 0;
    r.snmpIf = settings_->snmpIf;
    r.snmpOld = false;
    r.countedMs = 0;
    // No word is sent that the owner did not write: without one, a router is not asked SNMP.
    if (r.via == Router::Snmp && settings_->snmpCommunity.empty()) return routerMissing();
    if (!r.asked.isSet()) return routerMissing();
    askDevice(r);
  }

  // One question to the device, as its kind wants it. Its answer says the traffic, or that the
  // device is not of that kind.
  void askDevice(Router& r) {
    char host[16];
    r.asked.toString(host, sizeof(host));
    r.askedMs = nowMs_;
    const uint16_t gen = routerGen_;
    if (r.via == Router::Snmp) {
      lmp::IProbeService* asker = probe();
      if (!asker || !asker->ready()) return deviceFailed(r);
      wan::Oid oids[3];
      uint8_t count = 0, kind = wan::kSnmpGet;
      if (!r.snmpIf) {
        // The port towards Internet first: the one of the route to everywhere, in one of the
        // tables a router may keep.
        if (r.step == 0) {
          oids[count++] = wan::oidOf(wan::kOidRoute);
        } else {
          kind = wan::kSnmpNext;
          oids[count++] = r.step == 1 ? wan::oidOf(wan::kOidRouteCidr) : wan::oidOf(wan::kOidRouteInet);
        }
      } else if (r.snmpOld) {
        oids[count++] = wan::oidOf(wan::kOidIn, r.snmpIf);
        oids[count++] = wan::oidOf(wan::kOidOut, r.snmpIf);
        oids[count++] = wan::oidOf(wan::kOidBit, r.snmpIf);
      } else {
        oids[count++] = wan::oidOf(wan::kOidInHc, r.snmpIf);
        oids[count++] = wan::oidOf(wan::kOidOutHc, r.snmpIf);
        oids[count++] = wan::oidOf(wan::kOidMbit, r.snmpIf);
      }
      size_t len = 0;
      const uint8_t* ask = wan::snmpAsk(reinterpret_cast<uint8_t*>(soapBody_), sizeof(soapBody_),
                                        settings_->snmpCommunity.c_str(), ++r.snmpId, kind, oids, count, len);
      if (!ask) return deviceFailed(r);
      lmp::ProbeAsk what;
      what.kind = lmp::ProbeKind::Udp;
      what.ip = r.asked;
      what.port = r.port ? r.port : wan::kSnmpPort;
      what.data = ask;
      what.len = len;
      what.reply = reinterpret_cast<uint8_t*>(soapIn_);
      what.cap = sizeof(soapIn_) - 1;
      what.timeoutMs = 1500;
      r.asking = true;
      if (asker->ask(what, [this, gen](const lmp::ProbeAnswer& a) {
            if (gen == routerGen_) deviceAnswered(a.result == Status::Ok ? a.got : 0);
          }) != Status::Ok) {
        deviceBusy(r);
      }
      return;
    }
    lmp::IHttpClientService* http = ctx().http;
    if (!http || !http->ready()) return deviceFailed(r);
    lmp::HttpFetch what;
    if (r.via == Router::Starlink) {
      snprintf(r.control, sizeof(r.control), "http://%s:%u%s", host,
               static_cast<unsigned>(r.port ? r.port : wan::kStarlinkPort), wan::kStarlinkPath);
      what.method = lmp::hal::HttpMethod::Post;
      what.contentType = wan::kStarlinkType;
      what.body = lmp::StringView(wan::kStarlinkAsk, sizeof(wan::kStarlinkAsk));
      soapHeader_[0].name = "X-Grpc-Web";
      soapHeader_[0].value = "1";
      what.headers = lmp::Span<const lmp::hal::HttpHeader>(soapHeader_, 1);
    } else {
      // Huawei. Step 1 asks for a session, step 2 asks the traffic with its cookie, kept where
      // the description of a UPnP router is read: the two are never read together.
      char port[8] = "";
      if (r.port) snprintf(port, sizeof(port), ":%u", static_cast<unsigned>(r.port));
      snprintf(r.control, sizeof(r.control), "http://%s%s%s", host, port,
               r.step == 1 ? wan::kHuaweiSession : wan::kHuaweiTraffic);
      if (r.step == 2) {
        soapHeader_[0].name = "Cookie";
        soapHeader_[0].value = descWin_;
        what.headers = lmp::Span<const lmp::hal::HttpHeader>(soapHeader_, 1);
      }
    }
    what.url = r.control;
    what.out = soapIn_;
    what.cap = sizeof(soapIn_);
    what.timeoutMs = 2500;
    what.quiet = true;
    r.asking = true;
    if (http->fetch(what, [this, gen](const lmp::HttpAnswer& a) {
          if (gen == routerGen_) deviceAnswered(a.good() ? a.length : 0);
        }) != Status::Ok) {
      deviceBusy(r);
    }
  }

  // The question could not even leave: a device being looked for is looked for again from the
  // start, one that is read waits for its next round.
  void deviceBusy(Router& r) {
    r.asking = false;
    if (r.state != Router::Ready) r.state = Router::Unknown;
  }

  // No answer, or not the answer of that kind of device.
  void deviceFailed(Router& r) {
    r.asking = false;
    if (r.state != Router::Ready) return routerMissing();
    if (++r.fails >= 5) {
      r.state = Router::None;
      r.retryMs = nowMs_ + kRouterRetryMs;
      markChanged();
    }
  }

  // got: the bytes of the answer in soapIn_, 0 for none.
  void deviceAnswered(size_t got) {
    Router& r = router_;
    r.asking = false;
    if (r.state != Router::Ready && r.state != Router::Searching) return;
    if (got >= sizeof(soapIn_)) got = sizeof(soapIn_) - 1;
    soapIn_[got] = '\0';
    uint32_t down = 0, up = 0;   // kbit/s
    bool good = false, sample = false;
    if (r.via == Router::Starlink) {
      good = wan::starlinkRead(reinterpret_cast<const uint8_t*>(soapIn_), got, r.dish);
      down = static_cast<uint32_t>(r.dish.downBps / 1000);
      up = static_cast<uint32_t>(r.dish.upBps / 1000);
      sample = good;
    } else if (r.via == Router::Huawei) {
      if (r.step == 1) {
        if (!got || !wan::huaweiSession(soapIn_, descWin_, sizeof(descWin_))) return deviceFailed(r);
        r.step = 2;
        return askDevice(r);
      }
      uint32_t bytesDown = 0, bytesUp = 0;
      bool refused = false;
      good = got && wan::huaweiRead(soapIn_, bytesDown, bytesUp, refused);
      // A router that wants a session: asked for once. One whose session ended: the next
      // round starts again from the plain question.
      if (!good && refused && r.step == 0) {
        r.step = 1;
        return askDevice(r);
      }
      if (!good) r.step = 0;
      down = bytesDown / 125;
      up = bytesUp / 125;
      sample = good;
    } else if (r.via == Router::Snmp) {
      wan::SnmpValue v[3];
      uint8_t count = 0;
      const bool heard = got && wan::snmpRead(reinterpret_cast<const uint8_t*>(soapIn_), got, r.snmpId, v, 3, count);
      if (!r.snmpIf) {
        if (!heard) return deviceFailed(r);
        r.snmpHeard = true;
        bool found = count >= 1 && v[0].has && v[0].number > 0 && v[0].number <= 0xFFFFFFFFull;
        if (found && r.step == 1) found = wan::snmpUnder(v[0], wan::oidOf(wan::kOidRouteCidr));
        if (found && r.step == 2) found = wan::snmpUnder(v[0], wan::oidOf(wan::kOidRouteInet));
        if (found) {
          r.snmpIf = static_cast<uint32_t>(v[0].number);
        } else if (r.step < 2) {
          ++r.step;
        } else {
          return deviceFailed(r);
        }
        return askDevice(r);
      }
      if (heard && count >= 2 && v[0].has && v[1].has) {
        good = true;
        if (r.countedMs && r.askedMs != r.countedMs) {
          const uint32_t ms = r.askedMs - r.countedMs;
          uint64_t in = v[0].number - r.counted[0], out = v[1].number - r.counted[1];
          if (r.snmpOld) {
            in = static_cast<uint32_t>(in);
            out = static_cast<uint32_t>(out);
          }
          // A counter that started again gives a growth without sense: no sample.
          if (in < (1ull << 40) && out < (1ull << 40)) {
            down = static_cast<uint32_t>(in * 8 / ms);
            up = static_cast<uint32_t>(out * 8 / ms);
            sample = true;
          }
        }
        r.counted[0] = v[0].number;
        r.counted[1] = v[1].number;
        r.countedMs = r.askedMs;
        if (count >= 3 && v[2].has && v[2].number) {
          const uint64_t line = r.snmpOld ? v[2].number : v[2].number * 1000000ull;
          r.maxDown = r.maxUp = line > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(line);
        }
      } else if (heard && !r.snmpOld) {
        // The router has not the counters of 64 bit: the old ones.
        r.snmpHeard = true;
        r.snmpOld = true;
        return askDevice(r);
      }
    }
    if (!good) return deviceFailed(r);
    r.fails = 0;
    if (r.state != Router::Ready) {
      // This is the device: read from now on, a round every kRouterEveryMs.
      r.ip = r.asked;
      r.state = Router::Ready;
      r.planN = r.planAt = 0;
      r.snmpHeard = false;
      r.roundGridMs = nowMs_;
      r.nextMs = nowMs_ + kRouterEveryMs;
      char text[16];
      r.ip.toString(text, sizeof(text));
      LMP_LOGI(ctx().log, "traffic of the house read from %s at %s", routerViaName(r.via), text);
      markChanged();
    }
    if (!sample) return;
    r.estimate = r.estimateUp = false;
    r.at = static_cast<uint8_t>((r.at + 1) % kRouterSlots);
    if (r.filled < kRouterSlots) ++r.filled;
    ++r.seq;
    r.gridMs = r.roundGridMs;
    r.down[r.at] = down;
    r.up[r.at] = up;
  }

  // A round of a device without UPnP: one question, on the same grid as the rounds of a router.
  void sampleDevice(Router& r) {
    const int32_t late = static_cast<int32_t>(nowMs_ - r.nextMs);
    if (late < -static_cast<int32_t>(kTickMs / 2)) return;
    if (late > static_cast<int32_t>(kRouterEveryMs)) r.nextMs = nowMs_;
    r.roundGridMs = r.nextMs;
    r.nextMs += kRouterEveryMs;
    askDevice(r);
  }

  // In the context of the request: the description in pieces, through a window of 512
  // characters. The name of the service of the counters is found in it, and its address after.
  bool descPiece(lmp::Span<const uint8_t> piece) {
    for (size_t i = 0; i < piece.size(); ++i) {
      if (descLen_ + 1 >= sizeof(descWin_)) {
        if (descFound_) return false;   // its address did not follow: nothing more to read
        // The end stays: a name cut between two pieces is whole again.
        memmove(descWin_, descWin_ + descLen_ - 96, 96);
        descLen_ = 96;
      }
      descWin_[descLen_++] = static_cast<char>(piece[i]);
      descWin_[descLen_] = '\0';
      if (!descFound_ && piece[i] == '<') {
        const char* at = strstr(descWin_, ":service:WANCommonInterfaceConfig:");
        if (at) {
          const char* begin = at;
          while (begin > descWin_ && *(begin - 1) != '>') --begin;
          descLen_ -= static_cast<size_t>(begin - descWin_);
          memmove(descWin_, begin, descLen_ + 1);
          descFound_ = true;
        }
      }
    }
    // Its address is there: the rest of the description is not needed.
    return !(descFound_ && strstr(descWin_, "</controlURL>"));
  }

  // The description of the router: where its counters are asked.
  void readDescription() {
    lmp::IHttpClientService* http = ctx().http;
    if (!http || !http->ready()) return routerMissing();
    descLen_ = 0;
    descWin_[0] = '\0';
    descFound_ = false;
    lmp::HttpFetch what;
    what.url = router_.location;
    what.sink = [this](lmp::Span<const uint8_t> piece) { return descPiece(piece); };
    what.timeoutMs = 5000;
    what.quiet = true;
    router_.state = Router::Reading;
    router_.asking = true;
    const uint16_t gen = routerGen_;
    if (http->fetch(what, [this, gen](const lmp::HttpAnswer&) {
          if (gen != routerGen_) return;
          // Stopped by the window when it had what it needed: the outcome is in the window.
          router_.asking = false;
          if (!descFound_ || !controlOf(descWin_)) return routerMissing();
          router_.state = Router::Ready;
          router_.planN = router_.planAt = 0;
          router_.round = 0;   // the first round asks everything, the line too
          memset(router_.readIn, 0, sizeof(router_.readIn));
          router_.topRx = router_.topTx = false;
          router_.fails = 0;
          router_.nextMs = nowMs_;
          markChanged();
        }) != Status::Ok) {
      routerMissing();
    }
  }

  // The service of the counters, and its address: "<serviceType>…WANCommonInterfaceConfig…" and
  // the <controlURL> after it, joined to the address of the description.
  bool controlOf(const char* xml) {
    const char* type = strstr(xml, ":service:WANCommonInterfaceConfig:");
    if (!type) return false;
    const char* begin = type;
    while (begin > xml && *(begin - 1) != '>') --begin;
    const char* end = strchr(type, '<');
    if (!end || static_cast<size_t>(end - begin) >= sizeof(router_.service)) return false;
    memcpy(router_.service, begin, static_cast<size_t>(end - begin));
    router_.service[end - begin] = '\0';
    const char* ctl = strstr(end, "<controlURL>");
    if (!ctl) return false;
    ctl += 12;
    const char* ctlEnd = strchr(ctl, '<');
    if (!ctlEnd) return false;
    char path[80];
    const size_t len = static_cast<size_t>(ctlEnd - ctl) < sizeof(path) - 1 ? static_cast<size_t>(ctlEnd - ctl) : sizeof(path) - 1;
    memcpy(path, ctl, len);
    path[len] = '\0';
    if (strncmp(path, "http://", 7) == 0) {
      copyText(router_.control, sizeof(router_.control), path);
      return true;
    }
    // "http://192.168.1.1:38400" from the address of the description.
    const char* host = router_.location + 7;
    const char* slash = strchr(host, '/');
    const size_t baseLen = slash ? static_cast<size_t>(slash - router_.location) : strlen(router_.location);
    snprintf(router_.control, sizeof(router_.control), "%.*s%s%s", static_cast<int>(baseLen),
             router_.location, path[0] == '/' ? "" : "/", path);
    return true;
  }

  void askRouter(const char* action) {
    lmp::IHttpClientService* http = ctx().http;
    if (!http || !http->ready()) return;
    snprintf(soapAction_, sizeof(soapAction_), "\"%s#%s\"", router_.service, action);
    const int len = snprintf(soapBody_, sizeof(soapBody_),
                             "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                             "<u:%s xmlns:u=\"%s\"/></s:Body></s:Envelope>",
                             action, router_.service);
    if (len <= 0 || len >= static_cast<int>(sizeof(soapBody_))) return routerMissing();
    soapHeader_[0].name = "SOAPAction";
    soapHeader_[0].value = soapAction_;
    lmp::HttpFetch what;
    what.url = router_.control;
    what.method = lmp::hal::HttpMethod::Post;
    what.contentType = "text/xml; charset=\"utf-8\"";
    what.body = lmp::StringView(soapBody_, static_cast<size_t>(len));
    what.headers = lmp::Span<const lmp::hal::HttpHeader>(soapHeader_, 1);
    what.out = soapIn_;
    what.cap = sizeof(soapIn_);
    what.timeoutMs = 3000;
    what.quiet = true;
    router_.asking = true;
    const uint16_t gen = routerGen_;
    if (http->fetch(what, [this, gen](const lmp::HttpAnswer& a) {
          if (gen == routerGen_) routerAnswered(a);
        }) != Status::Ok) {
      router_.asking = false;
    }
  }

  // The number in <tag>...</tag> of an answer of the router.
  static bool soapValue(const char* xml, const char* tag, uint32_t& out) {
    char open[48];
    snprintf(open, sizeof(open), "<%s>", tag);
    const char* at = strstr(xml, open);
    if (!at) return false;
    at += strlen(open);
    if (*at < '0' || *at > '9') return false;
    out = static_cast<uint32_t>(strtoul(at, nullptr, 10));
    return true;
  }

  void routerAnswered(const lmp::HttpAnswer& a) {
    Router& r = router_;
    r.asking = false;
    if (r.state != Router::Ready || r.planAt >= r.planN) return;
    const uint8_t q = r.plan[r.planAt];
    uint32_t value = 0;
    bool good = a.good();
    if (good && q == kRouterLine) {
      if (soapValue(soapIn_, "NewLayer1DownstreamMaxBitRate", value)) r.maxDown = value;
      if (soapValue(soapIn_, "NewLayer1UpstreamMaxBitRate", value)) r.maxUp = value;
    } else if (good) {
      good = soapValue(soapIn_, kRouterTags[q], value);
      if (good) {
        // A counter that wraps still gives the difference.
        r.chain[q] = r.readIn[q] && r.readIn[q] + 1 == r.round;
        r.delta[q] = value - r.last[q];
        r.last[q] = value;
        r.readIn[q] = r.round;
        if (q == 0) r.topRx = atTop(value);
        if (q == 1) r.topTx = atTop(value);
      }
    }
    // Not every router says its line or counts its packets: a router that stopped answering is
    // one that does not give a counter this round needs.
    if (!good && (q == 0 || q == 1 || (q == 2 && r.byPkRx))) {
      // Looked for again, a minute later.
      if (++r.fails >= 5) {
        r.state = Router::None;
        r.retryMs = nowMs_ + kRouterRetryMs;
        markChanged();
      }
      r.planAt = r.planN;
      r.nextMs = nowMs_ + kRouterEveryMs;
      return;
    }
    if (good) r.fails = 0;
    if (++r.planAt < r.planN) {
      // The next question at once, not at the next tick: a round must end within its step.
      askRouter(kRouterActions[r.plan[r.planAt]]);
      return;
    }
    roundDone(r);
  }

  // A round is complete: the traffic since the round before. A byte counter stuck at its top, as
  // some routers have, tells by its packets; the bytes sent so, less the acknowledgements of what
  // came in. A way that could not be measured this round (its counter has just stopped, or the
  // router does not count its packets) is 0; with none of the two, no sample.
  void roundDone(Router& r) {
    const auto fresh = [&r](uint8_t q) { return r.readIn[q] == r.round && r.chain[q]; };
    const bool down = fresh(r.byPkRx ? 2 : 0);
    const bool up = fresh(r.byPkTx ? 3 : 1);
    const uint32_t ms = r.roundMs - r.lastMs;
    if ((!down && !up) || !ms) return;
    const uint64_t received = !down ? 0 : r.byPkRx ? static_cast<uint64_t>(r.delta[2]) * kBytesPerPacket : r.delta[0];
    uint64_t sent = up && !r.byPkTx ? r.delta[1] : 0;
    if (up && r.byPkTx) {
      const uint64_t packets = r.delta[3];
      const uint64_t acks = received / kBytesPerPacket / 2 < packets ? received / kBytesPerPacket / 2 : packets;
      sent = (packets - acks) * kBytesPerPacket + acks * kAckBytes;
    }
    r.estimate = down && r.byPkRx;
    r.estimateUp = up && r.byPkTx;
    r.at = static_cast<uint8_t>((r.at + 1) % kRouterSlots);
    if (r.filled < kRouterSlots) ++r.filled;
    ++r.seq;
    r.gridMs = r.roundGridMs;
    r.down[r.at] = static_cast<uint32_t>(received * 8 / ms);   // bit per ms: kbit/s
    r.up[r.at] = static_cast<uint32_t>(sent * 8 / ms);
  }

  void sampleLive() {
    if (!live_.askedMs || nowMs_ - live_.askedMs > kLiveKeepMs) return;
    {
      lmp::hal::StaInfo sta;
      if (station(sta)) sampleRouter(sta);
    }
    // On a grid, to the nearest tick: a sample every kLiveEveryMs on average, whatever the ticks
    // (250 ms from "now + 250" on ticks of 200 ms gave one every 400, and the graphs of the page
    // went back at every answer). Far behind, as after a pause, the grid starts again here.
    const int32_t late = static_cast<int32_t>(nowMs_ - live_.nextMs);
    if (late < -static_cast<int32_t>(kTickMs / 2)) return;
    if (late > static_cast<int32_t>(kLiveEveryMs)) live_.nextMs = nowMs_;
    const uint32_t due = live_.nextMs;
    live_.nextMs += kLiveEveryMs;
    lmp::hal::StaInfo sta;
    if (!station(sta)) return;
    lmp::IProbeService* asker = probe();
    if (!asker || !asker->ready()) return;
    live_.at = static_cast<uint8_t>((live_.at + 1) % kLiveSlots);
    if (live_.filled < kLiveSlots) ++live_.filled;
    ++live_.seq;
    live_.gridMs = due;
    const uint8_t slot = live_.at;
    live_.gw[slot] = -2;
    live_.net[slot] = -2;
    live_.rssi[slot] = sta.rssi;
    live_.gateway = sta.gateway;
    lmp::ProbeAsk what;
    what.kind = lmp::ProbeKind::Ping;
    what.timeoutMs = 900;
    if (!live_.gwAsking && sta.gateway.isSet()) {
      what.ip = sta.gateway;
      if (asker->ask(what, [this, slot](const lmp::ProbeAnswer& a) {
            live_.gwAsking = false;
            live_.gw[slot] = a.result == Status::Ok ? static_cast<int16_t>(a.rttMs > 30000 ? 30000 : a.rttMs) : -1;
          }) == Status::Ok) {
        live_.gwAsking = true;
      }
    }
    if (!live_.netAsking) {
      what.ip = Ip4::of(1, 1, 1, 1);
      if (asker->ask(what, [this, slot](const lmp::ProbeAnswer& a) {
            live_.netAsking = false;
            live_.net[slot] = a.result == Status::Ok ? static_cast<int16_t>(a.rttMs > 30000 ? 30000 : a.rttMs) : -1;
          }) == Status::Ok) {
        live_.netAsking = true;
      }
    }
  }

  Status live(lmp::CmdContext& ctx) {
    usedMs_ = nowMs_;   // somebody looks
    // Somebody looks again after a while: the router is looked for at once.
    if (router_.state == Router::None && (!live_.askedMs || nowMs_ - live_.askedMs > kLiveKeepMs)) {
      router_.retryMs = nowMs_;
    }
    live_.askedMs = nowMs_ ? nowMs_ : 1;
    if (!ctx.result) return Status::Ok;
    lmp::JsonWriter& w = *ctx.result;
    char text[16];
    lmp::hal::StaInfo sta;
    if (station(sta)) {
      w.i32("rssi", sta.rssi);
      w.u32("channel", sta.channel);
      sta.gateway.toString(text, sizeof(text));
      w.str("gateway", text);
    }
    w.str("target", "1.1.1.1");
    w.u32("everyMs", kLiveEveryMs);
    w.u32("slots", kLiveSlots);
    w.u32("seq", live_.seq);
    // How long ago the newest sample was due, at this moment, not at the last tick (up to 200 ms
    // before): the page puts it on its own clock.
    if (live_.seq) w.i32("ageMs", static_cast<int32_t>(kernel_->now() - live_.gridMs));
    const uint8_t n = live_.filled;
    const auto series = [&](const char* name, const int16_t* values) {
      w.beginArray(name);
      for (uint8_t i = 0; i < n; ++i) {
        const int16_t v = values[(live_.at + kLiveSlots - n + 1 + i) % kLiveSlots];
        if (v == -2) w.null();
        else w.i64(v);
      }
      w.endArray();
    };
    series("gwMs", live_.gw);
    series("netMs", live_.net);
    w.beginArray("rssiHist");
    for (uint8_t i = 0; i < n; ++i) w.i64(live_.rssi[(live_.at + kLiveSlots - n + 1 + i) % kLiveSlots]);
    w.endArray();
    // The traffic of the house, from the router: kbit/s, the last minute.
    const Router& r = router_;
    w.beginObject("router");
    w.str("state", routerStateName(r.state));
    if (!settings_->upnpHost.empty()) w.str("manual", settings_->upnpHost.c_str());
    if (settings_->trafficFrom) w.str("only", routerViaName(static_cast<Router::Via>(settings_->trafficFrom - 1)));
    if (r.ip.isSet()) {
      r.ip.toString(text, sizeof(text));
      w.str("ip", text);
    }
    if (r.state == Router::None && r.snmpHeard) w.boolean("needPort", true);
    if (r.state == Router::Ready) {
      w.str("via", routerViaName(r.via));
      w.u32("everyMs", kRouterEveryMs);
      w.u32("slots", kRouterSlots);
      w.u32("seq", r.seq);
      if (r.seq) w.i32("ageMs", static_cast<int32_t>(kernel_->now() - r.gridMs));
      if (r.maxDown) w.u32("maxDown", r.maxDown);
      if (r.maxUp) w.u32("maxUp", r.maxUp);
      if (r.estimate) w.boolean("estimate", true);
      if (r.estimateUp) w.boolean("estimateUp", true);
      w.beginArray("down");
      for (uint8_t i = 0; i < r.filled; ++i) w.u64(r.down[(r.at + kRouterSlots - r.filled + 1 + i) % kRouterSlots]);
      w.endArray();
      w.beginArray("up");
      for (uint8_t i = 0; i < r.filled; ++i) w.u64(r.up[(r.at + kRouterSlots - r.filled + 1 + i) % kRouterSlots]);
      w.endArray();
    }
    w.endObject();
    return Status::Ok;
  }

  // ---------------------------------------------------------------- the list on file

  void writeList() {
    lmp::IStorage* storage = ctx().storage;
    if (!storage) return;
    char* text = static_cast<char*>(lmp::hal::allocLarge(kListMax));
    if (!text) {
      LMP_LOGE(ctx().log, "no memory to write the list");
      return;
    }
    lmp::BufferSink sink(text, kListMax);
    lmp::JsonWriter w(sink);
    w.beginObject();
    w.beginArray("s");
    for (const Service& s : services_) {
      if (!s.id) continue;
      w.beginObject();
      w.u32("id", s.id);
      w.str("n", s.name);
      w.str("c", checkName(s.check));
      w.str("h", s.host);
      w.u32("p", s.port);
      w.str("u", s.path);
      w.u32("e", s.everySec);
      w.endObject();
    }
    w.endArray();
    w.beginArray("p");
    char mac[18];
    for (const Pc& p : pcs_) {
      if (!p.id) continue;
      w.beginObject();
      w.u32("id", p.id);
      w.str("n", p.name);
      lmp::macToString(p.mac, mac, sizeof(mac));
      w.str("m", mac);
      w.str("h", p.host);
      w.str("b", p.broadcast);
      w.u32("p", p.port);
      w.str("o", p.offUrl);
      w.endObject();
    }
    w.endArray();
    w.endObject();
    const Status s = w.complete() ? storage->writeFile(kListFile, sink.view()) : Status::NoMem;
    lmp::hal::freeLarge(text);
    if (s != Status::Ok) LMP_LOGE(ctx().log, "list not written: %s", lmp::statusName(s));
  }

  void readList() {
    lmp::IStorage* storage = ctx().storage;
    if (!storage || !storage->exists(kListFile)) return;
    char* text = static_cast<char*>(lmp::hal::allocLarge(kListMax));
    if (!text) return;
    size_t len = 0;
    if (storage->readFile(kListFile, text, kListMax, len) == Status::Ok) {
      const lmp::JsonValue doc = lmp::JsonValue::parse(lmp::StringView(text, len));
      const lmp::JsonValue list = doc["s"];
      uint8_t n = 0;
      for (size_t i = 0; i < list.size() && n < kServicesMax; ++i) {
        const lmp::JsonValue x = list.at(i);
        Service s;
        s.id = static_cast<uint8_t>(x["id"].asInt(0));
        char word[8] = "";
        x["c"].getString(word, sizeof(word));
        if (!s.id || !checkOf(word, s.check)) continue;
        x["n"].getString(s.name, sizeof(s.name));
        x["h"].getString(s.host, sizeof(s.host));
        if (s.check != Check::Http && !Ip4::parse(s.host, s.ip)) continue;
        s.port = static_cast<uint16_t>(x["p"].asInt(0));
        x["u"].getString(s.path, sizeof(s.path));
        s.everySec = static_cast<uint16_t>(x["e"].asInt(60));
        services_[n++] = s;
      }
      const lmp::JsonValue pcs = doc["p"];
      n = 0;
      for (size_t i = 0; i < pcs.size() && n < kPcsMax; ++i) {
        const lmp::JsonValue x = pcs.at(i);
        Pc p;
        p.id = static_cast<uint8_t>(x["id"].asInt(0));
        char mac[18] = "";
        x["m"].getString(mac, sizeof(mac));
        if (!p.id || !lmp::parseMac(mac, p.mac)) continue;
        x["n"].getString(p.name, sizeof(p.name));
        x["h"].getString(p.host, sizeof(p.host));
        if (p.host[0] && !Ip4::parse(p.host, p.ip)) p.host[0] = '\0';
        x["b"].getString(p.broadcast, sizeof(p.broadcast));
        p.port = static_cast<uint16_t>(x["p"].asInt(0));
        x["o"].getString(p.offUrl, sizeof(p.offUrl));
        pcs_[n++] = p;
      }
    }
    lmp::hal::freeLarge(text);
  }

  struct Outgoing {
    char text[kMessageCap] = "";
    bool silent = false;
    uint32_t ref = 0;
  };

  lmp::Kernel* kernel_ = nullptr;
  Settings settings_;
  Service services_[kServicesMax];
  Pc pcs_[kPcsMax];
  Event* events_ = nullptr;      // in the large memory: kEventsMax places
  uint16_t eventCount_ = 0;
  uint32_t eventsWrittenMs_ = 0;
  bool eventsDirty_ = false;
  bool summariesDue_ = false;
  bool online_ = false;   // on its network at the last round: the checks need it
  uint32_t nowMs_ = 0;
  char url_[kServicesMax][96] = {};
  // Telegram: a queue of messages, one travelling at a time.
  Outgoing* outbox_ = nullptr;   // in the large memory: kOutbox places
  uint8_t outHead_ = 0, outCount_ = 0;
  char tgUrl_[160] = "";
  char tgBody_[kMessageCap + 256] = "";
  char sent_[16] = "";
  bool sending_ = false;
  uint8_t summaryDay_ = 0;
  // Internet.
  bool netKnown_ = false, netUp_ = false;
  uint32_t netDownMs_ = 0;
  // The search in the network, and the list it keeps.
  Scan scan_ = Scan::Idle;
  bool scanAuto_ = false;
  uint32_t scanRef_ = 0;
  uint32_t scanStartMs_ = 0, scanTookMs_ = 0, nextScanMs_ = 0;
  uint32_t usedMs_ = 0;   // the last command of somebody: a page, the remote console, the serial, the chat
  uint16_t scanAt_ = 0, scanCount_ = 0;
  uint8_t videoOpen_[kVideoCount] = {};   // the answer of the ports of one device
  char* hostsBuf_ = nullptr;   // the head of the file, then the devices
  Host* cache_ = nullptr;
  uint16_t cacheCount_ = 0;
  uint16_t cacheCap_ = 0;
  uint32_t lastScanUnix_ = 0;
  uint32_t cacheUsedMs_ = 0;
  HostsHead head_;
  uint16_t knownCount_ = 0, onlineCount_ = 0, newCount_ = 0;
  Live live_;
  // The router.
  Router router_;
  char descWin_[512] = "";
  char msg_[kMessageCap] = "";   // a message being written, before its queue
  size_t descLen_ = 0;
  bool descFound_ = false;
  char soapAction_[112] = "";
  char upnpUsed_[24] = "";   // the device of the settings the router was looked for with
  uint8_t fromUsed_ = 0;     // and the kind of device chosen
  char snmpUsed_[25] = "";   // and the word and the port for a router with SNMP
  uint32_t snmpIfUsed_ = 0;
  uint16_t routerGen_ = 0;   // the search the answers on their way belong to
  char soapBody_[400] = "";
  char soapIn_[768] = "";
  lmp::hal::HttpHeader soapHeader_[1];
};

void Settings::applied() {
  if (owner) owner->onSettings();
}

}  // namespace
}  // namespace expnet

LMP_REGISTER_MODULE(expnet, expnet::ExpNet)

#endif  // LMP_FEATURE_PROBE && LMP_FEATURE_HTTPCLIENT
