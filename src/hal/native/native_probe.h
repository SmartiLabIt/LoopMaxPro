// LoopMaxPro HAL - PC: the network the tests decide, for the questions of sys.probe. Who is
// there, with which MAC, who answers a ping and on which ports, what is announced with mDNS
// and SSDP; and every datagram that was sent, to be read back.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hal/api/iprobe.h"

namespace lmp::hal::native {

class Probe : public INetProbe {
 public:
  Status ping(Ip4 ip, uint32_t timeoutMs, uint32_t& rttMs) override;
  Status tcp(Ip4 ip, uint16_t port, uint32_t timeoutMs, uint32_t& rttMs) override;
  Status udpSend(Ip4 ip, uint16_t port, const uint8_t* data, size_t len) override;
  Status arpSweep(Ip4 first, uint16_t count, uint32_t waitMs, ArpHit hit) override;
  Status mdnsBrowse(const char* service, const char* proto, uint32_t timeoutMs,
                    MdnsSink hit) override;
  Status ssdpSearch(const char* st, uint32_t timeoutMs, SsdpSink hit) override;
  Status hop(Ip4 ip, uint8_t ttl, uint32_t timeoutMs, Ip4& from, uint32_t& rttMs,
             bool& reached) override;
  Status udpAsk(Ip4 ip, uint16_t port, const uint8_t* data, size_t len, uint8_t* reply,
                size_t cap, size_t& got, uint32_t timeoutMs, bool anyPort) override;
  Status tcpMany(Ip4 ip, const uint16_t* ports, size_t count, uint32_t timeoutMs,
                 uint8_t* open) override;

  // ---- hooks for the tests
  struct Sent {
    Ip4 ip;
    uint16_t port = 0;
    std::vector<uint8_t> data;
  };
  void reset();
  // A device at ip, with its MAC. pingMs: the time of its answer, -1 when it does not answer.
  void addHost(Ip4 ip, const uint8_t* mac, int32_t pingMs = 1);
  void removeHost(Ip4 ip);
  void setPing(Ip4 ip, int32_t pingMs);
  // A port that takes connections; the other ports of a device there refuse them.
  void openPort(Ip4 ip, uint16_t port, uint32_t ms = 1);
  void closePort(Ip4 ip, uint16_t port);
  void addMdns(const char* service, const char* proto, const MdnsHit& hit);
  void addSsdp(const SsdpHit& hit);
  // The routers on the way out, in order: a trace meets them before the device. An address not
  // set is a router that does not answer.
  void setRoute(const std::vector<Ip4>& routers) { route_ = routers; }
  // Who answers a UDP question: the answer for ip, port and the question; empty, no answer.
  using UdpAnswer = std::function<std::vector<uint8_t>(Ip4, uint16_t, const std::vector<uint8_t>&)>;
  void onUdp(UdpAnswer answer) { udp_ = std::move(answer); }
  const std::vector<Sent>& sent() const { return sent_; }
  // How many questions were asked, of any kind.
  unsigned calls() const { return calls_; }

 private:
  struct Host {
    uint8_t mac[6] = {};
    int32_t pingMs = 1;
    std::map<uint16_t, uint32_t> ports;
  };
  static uint32_t key(Ip4 ip);

  std::map<uint32_t, Host> hosts_;
  std::vector<std::pair<std::string, MdnsHit>> mdns_;
  std::vector<SsdpHit> ssdp_;
  std::vector<Ip4> route_;
  UdpAnswer udp_;
  std::vector<Sent> sent_;
  unsigned calls_ = 0;
};

}  // namespace lmp::hal::native
