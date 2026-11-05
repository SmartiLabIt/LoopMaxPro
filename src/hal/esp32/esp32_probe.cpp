#include "hal/esp32/esp32_probe.h"

#if LMP_FEATURE_PROBE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/etharp.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/priv/tcpip_priv.h>
#include <lwip/prot/ip4.h>
#include <lwip/sockets.h>

#if __has_include(<mdns.h>)
#include <mdns.h>
#define LMP_PROBE_MDNS 1
#else
#define LMP_PROBE_MDNS 0
#endif

namespace lmp::hal::esp32 {
namespace {

// ARP asks this many addresses together: the table of lwIP has 10 places, and the gateway and
// whoever spoke last keep theirs.
constexpr size_t kArpGroup = 6;

sockaddr_in socketAddress(Ip4 ip, uint16_t port) {
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  memcpy(&to.sin_addr.s_addr, ip.b, 4);
  return to;
}

Ip4 ipOf(uint32_t networkOrder) {
  Ip4 ip;
  memcpy(ip.b, &networkOrder, 4);
  return ip;
}

uint32_t msSince(int64_t startUs) {
  return static_cast<uint32_t>((esp_timer_get_time() - startUs) / 1000);
}

// ---- ping

// The echo requests of this device: an id of its own, "LM", to know its answers from others.
constexpr uint16_t kPingId = 0x4C4D;
constexpr size_t kPingData = 32;
// ICMP: the echo that died on the way, and the router that says a device cannot be reached.
constexpr uint8_t kIcmpTimeExceeded = 11;
constexpr uint8_t kIcmpUnreachable = 3;
// Connections opened together by tcpMany: the sockets of lwIP are few, and the web server and
// the other services keep theirs. More on the boards with more memory (lmp_config.h).
constexpr size_t kTcpTogether = LMP_PROBE_TCP_TOGETHER;

// An echo request ready to go, with this sequence number.
void echoPacket(uint8_t* packet, size_t len, uint16_t seq) {
  auto* echo = reinterpret_cast<icmp_echo_hdr*>(packet);
  ICMPH_TYPE_SET(echo, ICMP_ECHO);
  ICMPH_CODE_SET(echo, 0);
  echo->id = htons(kPingId);
  echo->seqno = htons(seq);
  for (size_t i = sizeof(icmp_echo_hdr); i < len; ++i) packet[i] = static_cast<uint8_t>(i);
  echo->chksum = 0;
  echo->chksum = inet_chksum(packet, static_cast<u16_t>(len));
}

// ---- ARP, inside the task of lwIP: its table is not to be touched from anywhere else

struct ArpCall {
  tcpip_api_call_data call;   // first: lwIP hands back this pointer
  netif* nif = nullptr;
  ip4_addr_t ips[kArpGroup];
  size_t count = 0;
  bool ask = true;            // true: send the requests; false: read the answers
  bool found[kArpGroup] = {};
  uint8_t macs[kArpGroup][6] = {};
};

err_t arpInLwip(tcpip_api_call_data* call) {
  auto* a = reinterpret_cast<ArpCall*>(call);
  for (size_t i = 0; i < a->count; ++i) {
    if (a->ask) {
      etharp_request(a->nif, &a->ips[i]);
      continue;
    }
    eth_addr* mac = nullptr;
    const ip4_addr_t* ip = nullptr;
    if (etharp_find_addr(a->nif, &a->ips[i], &mac, &ip) >= 0 && mac) {
      memcpy(a->macs[i], mac->addr, 6);
      a->found[i] = true;
    }
  }
  return ERR_OK;
}

netif* stationNetif() {
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  return sta ? static_cast<netif*>(esp_netif_get_netif_impl(sta)) : nullptr;
}

// ---- SSDP

// The value of a header of an answer, case as it comes: "SERVER: Linux/4.9 UPnP/1.0".
void header(const char* text, const char* name, char* out, size_t cap) {
  out[0] = '\0';
  const size_t n = strlen(name);
  for (const char* line = text; line && *line;) {
    if (strncasecmp(line, name, n) == 0 && line[n] == ':') {
      const char* v = line + n + 1;
      while (*v == ' ') ++v;
      size_t len = 0;
      while (v[len] && v[len] != '\r' && v[len] != '\n' && len + 1 < cap) {
        out[len] = v[len];
        ++len;
      }
      out[len] = '\0';
      return;
    }
    line = strchr(line, '\n');
    if (line) ++line;
  }
}

}  // namespace

// An echo request on a socket of its own, on the task that asks: no task is made for it, and
// no memory is taken and given back at every question.
Status NetProbe::ping(Ip4 ip, uint32_t timeoutMs, uint32_t& rttMs) {
  const int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s < 0) return Status::NoMem;
  timeval tv = {0, 100000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  uint8_t packet[sizeof(icmp_echo_hdr) + kPingData];
  echoPacket(packet, sizeof(packet), ++seq_);
  const auto* echo = reinterpret_cast<const icmp_echo_hdr*>(packet);
  const sockaddr_in to = socketAddress(ip, 0);
  const int64_t start = esp_timer_get_time();
  if (sendto(s, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) < 0) {
    close(s);
    return Status::Io;
  }
  uint8_t in[96];
  const int64_t end = start + static_cast<int64_t>(timeoutMs) * 1000;
  Status result = Status::Timeout;
  while (esp_timer_get_time() < end) {
    sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    const int n = recvfrom(s, in, sizeof(in), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n <= 0) continue;
    // The answer comes with its IP header first.
    const auto* header = reinterpret_cast<const ip_hdr*>(in);
    const int skip = IPH_HL(header) * 4;
    if (n < skip + static_cast<int>(sizeof(icmp_echo_hdr))) continue;
    const auto* reply = reinterpret_cast<const icmp_echo_hdr*>(in + skip);
    if (from.sin_addr.s_addr != to.sin_addr.s_addr || ICMPH_TYPE(reply) != ICMP_ER ||
        reply->id != echo->id || reply->seqno != echo->seqno) {
      continue;   // the answer to somebody else, or to an echo before
    }
    rttMs = msSince(start);
    result = Status::Ok;
    break;
  }
  close(s);
  return result;
}

Status NetProbe::tcp(Ip4 ip, uint16_t port, uint32_t timeoutMs, uint32_t& rttMs) {
  const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return Status::NoMem;
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  const sockaddr_in to = socketAddress(ip, port);
  const int64_t start = esp_timer_get_time();
  Status result = Status::Io;
  if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0) {
    result = Status::Ok;
  } else if (errno == EINPROGRESS) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(s, &writable);
    timeval tv;
    tv.tv_sec = static_cast<long>(timeoutMs / 1000);
    tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
    const int ready = select(s + 1, nullptr, &writable, nullptr, &tv);
    if (ready == 0) {
      result = Status::Timeout;
    } else if (ready > 0) {
      int error = 0;
      socklen_t len = sizeof(error);
      getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len);
      result = error == 0 ? Status::Ok : (error == ECONNREFUSED ? Status::Denied : Status::Timeout);
    }
  } else if (errno == ECONNREFUSED) {
    result = Status::Denied;
  }
  if (result == Status::Ok || result == Status::Denied) rttMs = msSince(start);
  close(s);
  return result;
}

Status NetProbe::udpSend(Ip4 ip, uint16_t port, const uint8_t* data, size_t len) {
  if (!data && len) return Status::Invalid;
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return Status::NoMem;
  const int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  const sockaddr_in to = socketAddress(ip, port);
  const int sent = sendto(s, data, len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  close(s);
  return sent == static_cast<int>(len) ? Status::Ok : Status::Io;
}

Status NetProbe::arpSweep(Ip4 first, uint16_t count, uint32_t waitMs, ArpHit hit) {
  netif* nif = stationNetif();
  if (!nif) return Status::Denied;
  const uint32_t self = netif_ip4_addr(nif)->addr;   // the address of the device, network order
  uint32_t at = (static_cast<uint32_t>(first.b[0]) << 24) | (static_cast<uint32_t>(first.b[1]) << 16) |
                (static_cast<uint32_t>(first.b[2]) << 8) | first.b[3];
  for (uint16_t done = 0; done < count;) {
    ArpCall call;
    call.nif = nif;
    while (call.count < kArpGroup && done < count) {
      const uint32_t net = htonl(at++);
      ++done;
      if (net == self) continue;
      memcpy(&call.ips[call.count].addr, &net, 4);
      ++call.count;
    }
    if (!call.count) continue;
    call.ask = true;
    tcpip_api_call(arpInLwip, &call.call);
    vTaskDelay(pdMS_TO_TICKS(waitMs));
    call.ask = false;
    tcpip_api_call(arpInLwip, &call.call);
    for (size_t i = 0; i < call.count; ++i) {
      if (call.found[i] && hit) hit(ipOf(call.ips[i].addr), call.macs[i]);
    }
  }
  return Status::Ok;
}

Status NetProbe::mdnsBrowse(const char* service, const char* proto, uint32_t timeoutMs,
                            MdnsSink hit) {
#if LMP_PROBE_MDNS
  if (!service || !proto) return Status::Invalid;
  mdns_result_t* results = nullptr;
  if (mdns_query_ptr(service, proto, timeoutMs, LMP_PROBE_HITS, &results) != ESP_OK) {
    return Status::Io;
  }
  for (const mdns_result_t* r = results; r; r = r->next) {
    MdnsHit h;
    for (const mdns_ip_addr_t* a = r->addr; a; a = a->next) {
      if (a->addr.type == ESP_IPADDR_TYPE_V4) {
        h.ip = ipOf(a->addr.u_addr.ip4.addr);
        break;
      }
    }
    if (r->instance_name) snprintf(h.instance, sizeof(h.instance), "%s", r->instance_name);
    if (r->hostname) snprintf(h.host, sizeof(h.host), "%s", r->hostname);
    h.port = r->port;
    if (hit) hit(h);
  }
  mdns_query_results_free(results);
  return Status::Ok;
#else
  (void)service;
  (void)proto;
  (void)timeoutMs;
  (void)hit;
  return Status::Unsupported;
#endif
}

// The echo with a short life: a router where it dies answers "time exceeded", and carries the
// head of the echo, by which it is known.
Status NetProbe::hop(Ip4 ip, uint8_t ttl, uint32_t timeoutMs, Ip4& from, uint32_t& rttMs,
                     bool& reached) {
  reached = false;
  const int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s < 0) return Status::NoMem;
  timeval tv = {0, 100000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  const int life = ttl;
  setsockopt(s, IPPROTO_IP, IP_TTL, &life, sizeof(life));
  uint8_t packet[sizeof(icmp_echo_hdr) + kPingData];
  const uint16_t seq = ++seq_;
  echoPacket(packet, sizeof(packet), seq);
  const sockaddr_in to = socketAddress(ip, 0);
  const int64_t start = esp_timer_get_time();
  if (sendto(s, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) < 0) {
    close(s);
    return Status::Io;
  }
  uint8_t in[128];
  const int64_t end = start + static_cast<int64_t>(timeoutMs) * 1000;
  Status result = Status::Timeout;
  while (esp_timer_get_time() < end) {
    sockaddr_in who = {};
    socklen_t whoLen = sizeof(who);
    const int n = recvfrom(s, in, sizeof(in), 0, reinterpret_cast<sockaddr*>(&who), &whoLen);
    if (n <= 0) continue;
    const int skip = IPH_HL(reinterpret_cast<const ip_hdr*>(in)) * 4;
    if (n < skip + static_cast<int>(sizeof(icmp_echo_hdr))) continue;
    const auto* reply = reinterpret_cast<const icmp_echo_hdr*>(in + skip);
    const uint8_t type = ICMPH_TYPE(reply);
    if (type == ICMP_ER) {
      if (who.sin_addr.s_addr != to.sin_addr.s_addr || reply->id != htons(kPingId) ||
          reply->seqno != htons(seq)) {
        continue;
      }
      reached = true;
    } else if (type == kIcmpTimeExceeded || type == kIcmpUnreachable) {
      // After the 8 bytes of the ICMP head: the IP head of the echo, then the head of the echo.
      const int inner = skip + 8;
      if (n < inner + 20) continue;
      const int innerSkip = IPH_HL(reinterpret_cast<const ip_hdr*>(in + inner)) * 4;
      if (n < inner + innerSkip + static_cast<int>(sizeof(icmp_echo_hdr))) continue;
      const auto* mine = reinterpret_cast<const icmp_echo_hdr*>(in + inner + innerSkip);
      if (mine->id != htons(kPingId) || mine->seqno != htons(seq)) continue;
    } else {
      continue;
    }
    from = ipOf(who.sin_addr.s_addr);
    rttMs = msSince(start);
    result = type == kIcmpUnreachable ? Status::Denied : Status::Ok;
    break;
  }
  close(s);
  return result;
}

Status NetProbe::udpAsk(Ip4 ip, uint16_t port, const uint8_t* data, size_t len, uint8_t* reply,
                        size_t cap, size_t& got, uint32_t timeoutMs, bool anyPort) {
  got = 0;
  if (!data || !len || !reply || !cap) return Status::Invalid;
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return Status::NoMem;
  timeval tv = {0, 100000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  const sockaddr_in to = socketAddress(ip, port);
  if (sendto(s, data, len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != static_cast<int>(len)) {
    close(s);
    return Status::Io;
  }
  const int64_t end = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
  Status result = Status::Timeout;
  while (esp_timer_get_time() < end) {
    sockaddr_in who = {};
    socklen_t whoLen = sizeof(who);
    const int n = recvfrom(s, reply, cap, 0, reinterpret_cast<sockaddr*>(&who), &whoLen);
    if (n <= 0) continue;
    if (who.sin_addr.s_addr != to.sin_addr.s_addr || (!anyPort && who.sin_port != to.sin_port)) continue;
    got = static_cast<size_t>(n);
    result = Status::Ok;
    break;
  }
  close(s);
  return result;
}

// A few connections at a time, each one with its own wait: a port that refuses answers at once,
// one that nobody guards answers never, and the others do not wait for it.
Status NetProbe::tcpMany(Ip4 ip, const uint16_t* ports, size_t count, uint32_t timeoutMs,
                         uint8_t* open) {
  if (!ports || !open) return Status::Invalid;
  memset(open, 0, count);
  size_t next = 0;
  while (next < count) {
    int socks[kTcpTogether];
    size_t which[kTcpTogether];
    size_t used = 0;
    while (used < kTcpTogether && next < count) {
      const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (s < 0) break;   // no socket now: the ones open go first
      fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
      const sockaddr_in to = socketAddress(ip, ports[next]);
      if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0) {
        open[next] = 1;
        close(s);
        ++next;
        continue;
      }
      if (errno != EINPROGRESS) {
        close(s);
        ++next;
        continue;
      }
      socks[used] = s;
      which[used] = next++;
      ++used;
    }
    if (!used) {
      if (next < count) return Status::NoMem;
      break;
    }
    const int64_t end = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
    size_t left = used;
    while (left && esp_timer_get_time() < end) {
      fd_set writable;
      FD_ZERO(&writable);
      int top = -1;
      for (size_t i = 0; i < used; ++i) {
        if (socks[i] < 0) continue;
        FD_SET(socks[i], &writable);
        if (socks[i] > top) top = socks[i];
      }
      const int64_t wait = (end - esp_timer_get_time()) / 1000;
      timeval tv;
      tv.tv_sec = static_cast<long>(wait / 1000);
      tv.tv_usec = static_cast<long>((wait % 1000) * 1000);
      if (select(top + 1, nullptr, &writable, nullptr, &tv) <= 0) break;
      for (size_t i = 0; i < used; ++i) {
        if (socks[i] < 0 || !FD_ISSET(socks[i], &writable)) continue;
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(socks[i], SOL_SOCKET, SO_ERROR, &error, &len);
        if (error == 0) open[which[i]] = 1;
        close(socks[i]);
        socks[i] = -1;
        --left;
      }
    }
    for (size_t i = 0; i < used; ++i) {
      if (socks[i] >= 0) close(socks[i]);
    }
  }
  return Status::Ok;
}

Status NetProbe::ssdpSearch(const char* st, uint32_t timeoutMs, SsdpSink hit) {
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return Status::NoMem;
  timeval tv = {0, 100000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  char search[200];
  const int len = snprintf(search, sizeof(search),
                           "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                           "MX: 2\r\nST: %s\r\n\r\n",
                           st && *st ? st : "ssdp:all");
  if (len <= 0 || len >= static_cast<int>(sizeof(search))) {
    close(s);
    return Status::Invalid;
  }
  const sockaddr_in group = socketAddress(Ip4::of(239, 255, 255, 250), 1900);
  if (sendto(s, search, static_cast<size_t>(len), 0, reinterpret_cast<const sockaddr*>(&group),
             sizeof(group)) < 0) {
    close(s);
    return Status::Io;
  }
  char answer[512];
  const int64_t end = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
  while (esp_timer_get_time() < end) {
    sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    const int n = recvfrom(s, answer, sizeof(answer) - 1, 0, reinterpret_cast<sockaddr*>(&from),
                           &fromLen);
    if (n <= 0) continue;
    answer[n] = '\0';
    SsdpHit h;
    h.ip = ipOf(from.sin_addr.s_addr);
    header(answer, "SERVER", h.server, sizeof(h.server));
    header(answer, "ST", h.st, sizeof(h.st));
    header(answer, "LOCATION", h.location, sizeof(h.location));
    if (hit) hit(h);
  }
  close(s);
  return Status::Ok;
}

}  // namespace lmp::hal::esp32

#endif  // LMP_FEATURE_PROBE
