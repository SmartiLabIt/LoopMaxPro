// LoopMaxPro HAL - what a device can ask the network beyond its own connection: who answers a
// ping, which ports are open, who is at an address, what is announced there. For the tools of
// a network (ExpNet) and for the diagnosis of any board (sys.probe).
//
// Every call waits for its answer or for the end of its time: they run on the task of the
// service that asks (services/probe), never on the application task.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

// A service announced with mDNS.
struct MdnsHit {
  char instance[48] = "";   // "Office printer"
  char host[40] = "";       // "printer", without .local
  Ip4 ip;
  uint16_t port = 0;
};

// A device that answered an SSDP search (UPnP): televisions, routers, NAS, repeaters.
struct SsdpHit {
  Ip4 ip;
  char server[48] = "";     // the header SERVER: system and product
  char st[48] = "";         // what it says it is: "urn:schemas-upnp-org:device:MediaRenderer:1"
  char location[64] = "";   // where its description is
};

using ArpHit = Delegate<void(Ip4 ip, const uint8_t* mac)>;
using MdnsSink = Delegate<void(const MdnsHit& hit)>;
using SsdpSink = Delegate<void(const SsdpHit& hit)>;

class INetProbe {
 public:
  virtual ~INetProbe() = default;

  // Echo request. Ok with the time of the answer; Timeout when none came.
  virtual Status ping(Ip4 ip, uint32_t timeoutMs, uint32_t& rttMs) = 0;
  // A TCP connection, opened and closed at once. Ok: the port takes it; Denied: the device is
  // there and refuses that port; Timeout: nobody answered.
  virtual Status tcp(Ip4 ip, uint16_t port, uint32_t timeoutMs, uint32_t& rttMs) = 0;
  // One UDP datagram, also to a broadcast address.
  virtual Status udpSend(Ip4 ip, uint16_t port, const uint8_t* data, size_t len) = 0;
  // Who is at the count addresses from first: asked with ARP a few at a time, waiting waitMs
  // for each group. hit runs for each one that answered, with its MAC. The device itself is
  // not asked. It finds also who does not answer a ping, as most PC do.
  virtual Status arpSweep(Ip4 first, uint16_t count, uint32_t waitMs, ArpHit hit) = 0;
  // The services of one kind announced with mDNS: service "_http", proto "_tcp".
  virtual Status mdnsBrowse(const char* service, const char* proto, uint32_t timeoutMs,
                            MdnsSink hit) = 0;
  // The devices that answer an SSDP search, for timeoutMs. One device can answer more times.
  // st: the kind asked, "urn:schemas-upnp-org:device:InternetGatewayDevice:1"; null, all.
  virtual Status ssdpSearch(const char* st, uint32_t timeoutMs, SsdpSink hit) = 0;
  // An echo request that lives only ttl routers: the router where it dies says so. Ok with the
  // address that answered and the time; reached is true when ip itself answered. Denied when a
  // router says ip cannot be reached; Timeout when nobody answered.
  virtual Status hop(Ip4 ip, uint8_t ttl, uint32_t timeoutMs, Ip4& from, uint32_t& rttMs,
                     bool& reached) = 0;
  // One UDP datagram, and the first answer that comes back from ip and port: a question to a
  // name server. anyPort: from any port of ip, as an SSDP device answers an M-SEARCH sent to it
  // alone. got: the length of the answer.
  virtual Status udpAsk(Ip4 ip, uint16_t port, const uint8_t* data, size_t len, uint8_t* reply,
                        size_t cap, size_t& got, uint32_t timeoutMs, bool anyPort) = 0;
  // Many TCP ports of one device, a few connections at a time: open[i] is 1 when ports[i] takes
  // them. A port that refuses, or that does not answer within timeoutMs, is closed.
  virtual Status tcpMany(Ip4 ip, const uint16_t* ports, size_t count, uint32_t timeoutMs,
                         uint8_t* open) = 0;
};

}  // namespace lmp::hal
