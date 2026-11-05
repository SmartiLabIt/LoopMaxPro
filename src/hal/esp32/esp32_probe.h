// LoopMaxPro HAL - ESP32 family: the questions of sys.probe to the network (iprobe.h). Ping,
// TCP and UDP with the sockets of lwIP, on the task that asks; ARP inside the task of lwIP; mDNS
// with its component; SSDP with a socket. The same code for Arduino and ESP-IDF.
#pragma once
#include "hal/api/iprobe.h"
#include "lmp_config.h"

#if LMP_FEATURE_PROBE

namespace lmp::hal::esp32 {

class NetProbe : public INetProbe {
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

 private:
  uint16_t seq_ = 0;   // the number of the last echo request
};

}  // namespace lmp::hal::esp32

#endif  // LMP_FEATURE_PROBE
