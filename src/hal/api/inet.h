// LoopMaxPro HAL - network link. Station and access point are tracked separately.
// No policy here: reconnection, fallback and tests belong to the net service.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

enum class StaState : uint8_t { Idle, Connecting, Connected, Failed, Lost };

// Why the last attempt failed, as far as the driver can tell.
enum class StaFail : uint8_t {
  None,
  NotFound,        // no network with that name around
  WrongPassword,   // the network is there and did not accept the device
  Other
};
enum class ScanState : uint8_t { Idle, Running, Done, Failed };

enum class NetEvent : uint8_t {
  StaConnected,     // associated, no address yet
  StaGotIp,
  StaLost,
  StaFailed,        // connection attempt failed
  ApStarted,
  ApStopped,
  ApClientJoined,
  ApClientLeft,
  ScanDone,
  LinkGotIp,        // the link that is not the Wi-Fi (ilink.h) has an address
  LinkLost          // and has lost it: cable out, network gone
};

struct WifiNetwork {
  char ssid[33] = {0};
  int8_t rssi = 0;
  uint8_t channel = 0;
  bool secured = false;
};

struct StaInfo {
  StaState state = StaState::Idle;
  char ssid[33] = {0};
  Ip4 ip;
  Ip4 gateway;
  Ip4 mask;
  Ip4 dns;
  int8_t rssi = 0;
  uint8_t channel = 0;
  StaFail fail = StaFail::None;
  uint8_t failReason = 0;  // driver specific, for logs
};

struct ApInfo {
  bool up = false;
  char ssid[33] = {0};
  Ip4 ip;
  uint8_t clients = 0;
  uint8_t channel = 0;
};

// The service every LoopMaxPro device announces on the network next to the web one (DNS-SD,
// "_loopmaxpro._tcp"): who looks for it finds the devices of the system, and only those.
constexpr const char* kPeerService = "_loopmaxpro";

// One datum of the announcement: "fw" = "1.0.6".
struct NetText {
  const char* key;
  const char* value;
};

// Another device of the system, as its announcement tells it.
struct NetPeer {
  char host[33] = {0};      // host name, without ".local"
  Ip4 ip;
  uint16_t port = 0;
  uint8_t api = 0;          // how to talk to it; 0 when it did not say
  char id[18] = {0};        // its public id: the MAC address
  char name[33] = {0};      // the name its owner gave it
  char code[17] = {0};      // code of the product; empty for a free module
  char modules[41] = {0};   // its modules, separated by commas
  char title[25] = {0};     // what it is: the title of its first module, "Zappy 2"
  char icon[9] = {0};       // the icon of that module
  char fw[13] = {0};
  char board[21] = {0};
  char edition[6] = {0};    // "full" or "light"
};

// A text that came from the network, into a field: it ends at a whole character when it does
// not fit, and what is not UTF-8 becomes a question mark. So the documents that carry it are
// always valid, whoever announced it.
inline void peerText(char* to, size_t cap, const char* value) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
  size_t n = 0;
  while (*p) {
    const unsigned char c = *p;
    size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c < 0xE0) ? 2 : (c >= 0xE0 && c < 0xF0) ? 3
               : (c >= 0xF0 && c < 0xF5) ? 4 : 0;
    for (size_t i = 1; len && i < len; ++i) {
      if ((p[i] & 0xC0) != 0x80) len = 0;
    }
    // Longer than it must be, a half of a pair, past the last character there is.
    if ((c == 0xE0 && p[1] < 0xA0) || (c == 0xED && p[1] >= 0xA0) || (c == 0xF0 && p[1] < 0x90) ||
        (c == 0xF4 && p[1] >= 0x90)) {
      len = 0;
    }
    if (n + (len ? len : 1) >= cap) break;
    if (!len) {
      to[n++] = '?';
      ++p;
      continue;
    }
    for (size_t i = 0; i < len; ++i) to[n++] = static_cast<char>(p[i]);
    p += len;
  }
  to[n] = '\0';
}

// One datum of an announcement into the device it describes. The keys are the ones the net
// service announces: unknown ones are left alone, a newer device may say more.
inline void peerTake(NetPeer& peer, const char* key, const char* value) {
  if (!key || !value) return;
  struct Field {
    const char* key;
    char* to;
    size_t cap;
  };
  const Field fields[] = {
      {"id", peer.id, sizeof(peer.id)},          {"name", peer.name, sizeof(peer.name)},
      {"code", peer.code, sizeof(peer.code)},    {"mod", peer.modules, sizeof(peer.modules)},
      {"title", peer.title, sizeof(peer.title)}, {"icon", peer.icon, sizeof(peer.icon)},
      {"fw", peer.fw, sizeof(peer.fw)},          {"board", peer.board, sizeof(peer.board)},
      {"ed", peer.edition, sizeof(peer.edition)},
  };
  if (key[0] == 'a' && key[1] == 'p' && key[2] == 'i' && key[3] == '\0') {
    unsigned api = 0;
    for (const char* c = value; *c >= '0' && *c <= '9' && api < 250; ++c) api = api * 10 + (*c - '0');
    peer.api = static_cast<uint8_t>(api);
    return;
  }
  for (const Field& f : fields) {
    size_t i = 0;
    while (f.key[i] && f.key[i] == key[i]) ++i;
    if (f.key[i] || key[i]) continue;
    peerText(f.to, f.cap, value);
    return;
  }
}

// Called from the driver context. The net service forwards it to the application task.
using NetEventHandler = Delegate<void(NetEvent)>;

class INet {
 public:
  virtual ~INet() = default;

  virtual Status init(const char* hostname) = 0;
  virtual void setEventHandler(NetEventHandler handler) = 0;
  virtual void poll() = 0;

  // Station. One attempt for each call: the driver never tries again by itself.
  // The outcome arrives as StaGotIp or StaFailed.
  virtual Status staConnect(const char* ssid, const char* password) = 0;
  // Leaving on request raises no event: StaLost is only for a connection that went away.
  virtual Status staDisconnect() = 0;
  virtual void staInfo(StaInfo& out) const = 0;
  virtual void staMac(Mac& out) const = 0;

  // Access point. It can run together with the station.
  virtual Status apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) = 0;
  virtual Status apStop() = 0;
  virtual void apInfo(ApInfo& out) const = 0;

  // Asynchronous scan
  virtual Status scanStart() = 0;
  virtual ScanState scanState() const = 0;
  virtual size_t scanCount() const = 0;
  virtual bool scanResult(size_t index, WifiNetwork& out) const = 0;
  // Forgets the results and stops a scan in progress.
  virtual void scanClear() = 0;

  // Name services
  virtual Status mdnsStart(const char* hostname, uint16_t httpPort) = 0;
  virtual void mdnsStop() = 0;
  virtual bool mdnsActive() const = 0;
  // The service of the system, with what the device says of itself. After mdnsStart, and
  // again when the data change. Unsupported on the boards without names.
  virtual Status mdnsAnnounce(const char* instance, uint16_t port, const NetText* texts,
                              size_t count) {
    (void)instance;
    (void)port;
    (void)texts;
    (void)count;
    return Status::Unsupported;
  }
  // Asks the network who announces the service and waits for the answers, timeoutMs long: it
  // is called from the worker, never from the round of the system. The device itself may be
  // among the answers.
  virtual Status peersFind(uint32_t timeoutMs, NetPeer* out, size_t cap, size_t& count) {
    (void)timeoutMs;
    (void)out;
    (void)cap;
    count = 0;
    return Status::Unsupported;
  }
  // Answers every DNS query with the given address: captive portal in AP mode.
  virtual Status captiveStart(Ip4 address) = 0;
  virtual void captiveStop() = 0;
  virtual bool captiveActive() const = 0;
};

}  // namespace lmp::hal
