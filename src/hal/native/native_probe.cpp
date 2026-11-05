#include "hal/native/native_probe.h"

#include <string.h>

namespace lmp::hal::native {

uint32_t Probe::key(Ip4 ip) {
  return (static_cast<uint32_t>(ip.b[0]) << 24) | (static_cast<uint32_t>(ip.b[1]) << 16) |
         (static_cast<uint32_t>(ip.b[2]) << 8) | ip.b[3];
}

void Probe::reset() {
  hosts_.clear();
  mdns_.clear();
  ssdp_.clear();
  sent_.clear();
  route_.clear();
  udp_ = nullptr;
  calls_ = 0;
}

void Probe::addHost(Ip4 ip, const uint8_t* mac, int32_t pingMs) {
  Host& h = hosts_[key(ip)];
  if (mac) memcpy(h.mac, mac, sizeof(h.mac));
  h.pingMs = pingMs;
}

void Probe::removeHost(Ip4 ip) { hosts_.erase(key(ip)); }

void Probe::setPing(Ip4 ip, int32_t pingMs) {
  const auto found = hosts_.find(key(ip));
  if (found != hosts_.end()) found->second.pingMs = pingMs;
}

void Probe::openPort(Ip4 ip, uint16_t port, uint32_t ms) { hosts_[key(ip)].ports[port] = ms; }

void Probe::closePort(Ip4 ip, uint16_t port) {
  const auto found = hosts_.find(key(ip));
  if (found != hosts_.end()) found->second.ports.erase(port);
}

void Probe::addMdns(const char* service, const char* proto, const MdnsHit& hit) {
  mdns_.emplace_back(std::string(service) + "." + proto, hit);
}

void Probe::addSsdp(const SsdpHit& hit) { ssdp_.push_back(hit); }

Status Probe::ping(Ip4 ip, uint32_t, uint32_t& rttMs) {
  ++calls_;
  const auto found = hosts_.find(key(ip));
  if (found == hosts_.end() || found->second.pingMs < 0) return Status::Timeout;
  rttMs = static_cast<uint32_t>(found->second.pingMs);
  return Status::Ok;
}

Status Probe::tcp(Ip4 ip, uint16_t port, uint32_t, uint32_t& rttMs) {
  ++calls_;
  const auto found = hosts_.find(key(ip));
  if (found == hosts_.end()) return Status::Timeout;
  const auto open = found->second.ports.find(port);
  if (open == found->second.ports.end()) return Status::Denied;
  rttMs = open->second;
  return Status::Ok;
}

Status Probe::udpSend(Ip4 ip, uint16_t port, const uint8_t* data, size_t len) {
  ++calls_;
  if (!data && len) return Status::Invalid;
  Sent s;
  s.ip = ip;
  s.port = port;
  if (len) s.data.assign(data, data + len);
  sent_.push_back(s);
  return Status::Ok;
}

Status Probe::arpSweep(Ip4 first, uint16_t count, uint32_t, ArpHit hit) {
  ++calls_;
  uint32_t at = key(first);
  for (uint16_t i = 0; i < count; ++i, ++at) {
    const auto found = hosts_.find(at);
    if (found == hosts_.end()) continue;
    const Ip4 ip = Ip4::of(static_cast<uint8_t>(at >> 24), static_cast<uint8_t>(at >> 16),
                           static_cast<uint8_t>(at >> 8), static_cast<uint8_t>(at));
    if (hit) hit(ip, found->second.mac);
  }
  return Status::Ok;
}

Status Probe::mdnsBrowse(const char* service, const char* proto, uint32_t, MdnsSink hit) {
  ++calls_;
  if (!service || !proto) return Status::Invalid;
  const std::string kind = std::string(service) + "." + proto;
  for (const auto& m : mdns_) {
    if (m.first == kind && hit) hit(m.second);
  }
  return Status::Ok;
}

Status Probe::hop(Ip4 ip, uint8_t ttl, uint32_t, Ip4& from, uint32_t& rttMs, bool& reached) {
  ++calls_;
  reached = false;
  if (ttl == 0) return Status::Invalid;
  if (ttl <= route_.size()) {
    const Ip4 router = route_[ttl - 1];
    if (!router.isSet()) return Status::Timeout;
    from = router;
    rttMs = ttl * 2u;
    return Status::Ok;
  }
  const auto found = hosts_.find(key(ip));
  if (found == hosts_.end() || found->second.pingMs < 0) return Status::Timeout;
  from = ip;
  rttMs = static_cast<uint32_t>(found->second.pingMs);
  reached = true;
  return Status::Ok;
}

Status Probe::udpAsk(Ip4 ip, uint16_t port, const uint8_t* data, size_t len, uint8_t* reply,
                     size_t cap, size_t& got, uint32_t, bool) {
  ++calls_;
  got = 0;
  if (!data || !len || !reply || !cap) return Status::Invalid;
  Sent s;
  s.ip = ip;
  s.port = port;
  s.data.assign(data, data + len);
  sent_.push_back(s);
  if (!udp_) return Status::Timeout;
  const std::vector<uint8_t> answer = udp_(ip, port, s.data);
  if (answer.empty()) return Status::Timeout;
  got = answer.size() < cap ? answer.size() : cap;
  memcpy(reply, answer.data(), got);
  return Status::Ok;
}

Status Probe::tcpMany(Ip4 ip, const uint16_t* ports, size_t count, uint32_t, uint8_t* open) {
  ++calls_;
  if (!ports || !open) return Status::Invalid;
  const auto found = hosts_.find(key(ip));
  for (size_t i = 0; i < count; ++i) {
    open[i] = found != hosts_.end() && found->second.ports.count(ports[i]) ? 1 : 0;
  }
  return Status::Ok;
}

Status Probe::ssdpSearch(const char* st, uint32_t, SsdpSink hit) {
  ++calls_;
  for (const SsdpHit& s : ssdp_) {
    // A search for one kind: only who says to be that.
    if (st && *st && strncmp(s.st, st, strlen(s.st)) != 0) continue;
    if (hit) hit(s);
  }
  return Status::Ok;
}

}  // namespace lmp::hal::native
