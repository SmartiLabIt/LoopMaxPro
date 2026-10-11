#include "services/probe/probe_service.h"

#if LMP_FEATURE_PROBE

#include <stdio.h>
#include <string.h>

#include "hal/api/memory.h"

namespace lmp {
namespace {

// The packet that wakes a PC goes three times: a lost datagram is not asked again.
constexpr uint8_t kWakeTimes = 3;
constexpr uint16_t kWakePort = 9;
constexpr size_t kMagicLen = 102;
// A sweep larger than a /23 is not a network of home or of a small office.
constexpr uint16_t kSweepMost = 512;

constexpr ArgSpec kPingArgs[] = {
    Arg::str("host", 7, 15),
    Arg::u32("timeoutMs", 100, 10000).optional(),
};
constexpr ArgSpec kTcpArgs[] = {
    Arg::str("host", 7, 15),
    Arg::u16("port", 1, 65535),
    Arg::u32("timeoutMs", 100, 10000).optional(),
};
constexpr ArgSpec kWakeArgs[] = {
    Arg::str("mac", 12, 17),
    Arg::str("broadcast", 7, 15).optional(),
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

const char* probeKindName(ProbeKind kind) {
  switch (kind) {
    case ProbeKind::Ping:  return "ping";
    case ProbeKind::Tcp:   return "tcp";
    case ProbeKind::Wake:  return "wake";
    case ProbeKind::Sweep: return "sweep";
    case ProbeKind::Mdns:  return "mdns";
    case ProbeKind::Ssdp:  return "ssdp";
    case ProbeKind::Hop:   return "hop";
    case ProbeKind::Udp:   return "udp";
    case ProbeKind::Ports: return "ports";
  }
  return "?";
}

bool parseMac(const char* text, uint8_t* mac) {
  if (!text || !mac) return false;
  uint8_t out[6] = {};
  for (int i = 0; i < 6; ++i) {
    const int hi = hexValue(text[0]);
    const int lo = hi < 0 ? -1 : hexValue(text[1]);
    if (lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi * 16 + lo);
    text += 2;
    // The parts go together, or with the same sign between all of them.
    if (i < 5 && (*text == ':' || *text == '-')) ++text;
  }
  if (*text) return false;
  memcpy(mac, out, sizeof(out));
  return true;
}

void macToString(const uint8_t* mac, char* out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
}

void magicPacket(const uint8_t* mac, uint8_t* out) {
  memset(out, 0xFF, 6);
  for (int i = 0; i < 16; ++i) memcpy(out + 6 + i * 6, mac, 6);
}

hal::Ip4 broadcastOf(hal::Ip4 address, hal::Ip4 mask) {
  hal::Ip4 b;
  for (int i = 0; i < 4; ++i) b.b[i] = static_cast<uint8_t>(address.b[i] | ~mask.b[i]);
  return b;
}

const ServiceInfo& ProbeService::info() const {
  static const ServiceInfo kInfo{ServiceId::Probe, "probe", "\xF0\x9F\x94\x8E", Mode::Lan,
                                 RestartPolicy::Retry, 50,
                                 DepList(ServiceId::Net, ServiceId::Log)};
  return kInfo;
}

Status ProbeService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  probe_ = kernel.board().probe;
  if (!probe_) {
    fail("no questions to the network on this board");
    return Status::Unsupported;
  }
  if (!mutex_ && kernel.board().rtos) mutex_ = kernel.board().rtos->createMutex();
  if (!workerUp_) {
    // A task of its own, made at the first question: a device that never asks has none.
    worker_.setTask("lmp_probe", LMP_PROBE_STACK);
    worker_.begin(kernel.board().rtos);
    workerUp_ = true;
  }
  hal::LockGuard guard(mutex_);
  // A question that was travelling when the service stopped is still out there.
  dropped_ = busy_;
  return Status::Ok;
}

void ProbeService::stop() {
  // Who was waiting is told: nothing stays pending for ever.
  while (count_) {
    const Entry e = queue_[head_];
    head_ = (head_ + 1) % LMP_PROBE_QUEUE;
    --count_;
    ProbeAnswer gone;
    gone.result = Status::Denied;
    if (e.done) e.done(gone);
  }
  hal::LockGuard guard(mutex_);
  if (busy_ && !dropped_) {
    dropped_ = true;
    ProbeAnswer gone;
    gone.result = Status::Denied;
    if (flying_.done) flying_.done(gone);
  }
}

bool ProbeService::network(hal::Ip4& address, hal::Ip4& mask) const {
  if (!kernel_ || !kernel_->board().net) return false;
  hal::StaInfo sta;
  kernel_->board().net->staInfo(sta);
  if (sta.state != hal::StaState::Connected || !sta.ip.isSet()) return false;
  address = sta.ip;
  mask = sta.mask;
  return true;
}

Status ProbeService::ask(const ProbeAsk& what, ProbeDone done) {
  if (!running() || !probe_) return Status::Denied;
  switch (what.kind) {
    case ProbeKind::Ping:
      if (!what.ip.isSet()) return Status::Invalid;
      break;
    case ProbeKind::Tcp:
      if (!what.ip.isSet() || what.port == 0) return Status::Invalid;
      break;
    case ProbeKind::Wake: {
      static const uint8_t kNone[6] = {};
      if (memcmp(what.mac, kNone, sizeof(kNone)) == 0) return Status::Invalid;
      break;
    }
    case ProbeKind::Sweep:
      if (!what.ip.isSet() || what.count == 0 || what.count > kSweepMost) return Status::Invalid;
      break;
    case ProbeKind::Mdns:
      if (!what.service || !*what.service || !what.proto) return Status::Invalid;
      break;
    case ProbeKind::Ssdp:
      break;
    case ProbeKind::Hop:
      if (!what.ip.isSet() || what.ttl == 0 || what.ttl > 64) return Status::Invalid;
      break;
    case ProbeKind::Udp:
      if (!what.ip.isSet() || !what.port || !what.data || !what.len || !what.reply || !what.cap) {
        return Status::Invalid;
      }
      break;
    case ProbeKind::Ports:
      if (!what.ip.isSet() || !what.ports || !what.open || what.count == 0 || what.count > 256) {
        return Status::Invalid;
      }
      break;
  }
  if (count_ >= LMP_PROBE_QUEUE) return Status::Busy;
  Entry& e = queue_[(head_ + count_) % LMP_PROBE_QUEUE];
  e.what = what;
  e.done = done;
  // The packet that wakes goes to the broadcast of the network, when who asks does not say.
  if (what.kind == ProbeKind::Wake && !what.ip.isSet()) {
    hal::Ip4 address, mask;
    e.what.ip = network(address, mask) ? broadcastOf(address, mask)
                                       : hal::Ip4::of(255, 255, 255, 255);
  }
  ++count_;
  launch();
  return Status::Ok;
}

void ProbeService::freeRoom() {
  if (hosts_) hal::freeLarge(hosts_);
  if (services_) hal::freeLarge(services_);
  if (devices_) hal::freeLarge(devices_);
  hosts_ = nullptr;
  services_ = nullptr;
  devices_ = nullptr;
  hostCount_ = hostCap_ = serviceCount_ = deviceCount_ = 0;
}

void ProbeService::launch() {
  Status room = Status::Ok;
  {
    hal::LockGuard guard(mutex_);
    if (busy_ || count_ == 0) return;
    flying_ = queue_[head_];
    queue_[head_].done.reset();
    head_ = (head_ + 1) % LMP_PROBE_QUEUE;
    --count_;
    answer_ = ProbeAnswer();
    busy_ = true;
    finished_ = false;
    dropped_ = false;
    // Room for what it finds, taken now: the task of the service does not allocate.
    freeRoom();
    if (flying_.what.kind == ProbeKind::Sweep) {
      hostCap_ = flying_.what.count;
      hosts_ = static_cast<ProbeHost*>(hal::allocLarge(hostCap_ * sizeof(ProbeHost)));
      if (!hosts_) room = Status::NoMem;
    } else if (flying_.what.kind == ProbeKind::Mdns) {
      services_ = static_cast<hal::MdnsHit*>(hal::allocLarge(LMP_PROBE_HITS * sizeof(hal::MdnsHit)));
      if (!services_) room = Status::NoMem;
    } else if (flying_.what.kind == ProbeKind::Ssdp) {
      devices_ = static_cast<hal::SsdpHit*>(hal::allocLarge(LMP_PROBE_HITS * sizeof(hal::SsdpHit)));
      if (!devices_) room = Status::NoMem;
    }
    if (room != Status::Ok) {
      answer_.result = room;
      finished_ = true;
      return;
    }
  }
  ++questions_;
  const Status posted = worker_.post([this]() { travel(); });
  if (posted != Status::Ok) {
    hal::LockGuard guard(mutex_);
    answer_.result = posted;
    finished_ = true;
  }
}

void ProbeService::travel() {
  const ProbeAsk& what = flying_.what;
  ProbeAnswer a;
  switch (what.kind) {
    case ProbeKind::Ping:
      a.result = probe_->ping(what.ip, what.timeoutMs, a.rttMs);
      break;
    case ProbeKind::Tcp:
      a.result = probe_->tcp(what.ip, what.port, what.timeoutMs, a.rttMs);
      break;
    case ProbeKind::Wake: {
      uint8_t packet[kMagicLen];
      magicPacket(what.mac, packet);
      a.result = Status::Ok;
      for (uint8_t i = 0; i < kWakeTimes; ++i) {
        const Status s = probe_->udpSend(what.ip, kWakePort, packet, sizeof(packet));
        if (s != Status::Ok) a.result = s;
      }
      break;
    }
    case ProbeKind::Sweep:
      a.result = probe_->arpSweep(what.ip, what.count, what.timeoutMs,
                                  [this](hal::Ip4 ip, const uint8_t* mac) {
                                    if (hostCount_ >= hostCap_) return;
                                    ProbeHost& h = hosts_[hostCount_++];
                                    h.ip = ip;
                                    memcpy(h.mac, mac, sizeof(h.mac));
                                  });
      break;
    case ProbeKind::Mdns:
      a.result = probe_->mdnsBrowse(what.service, what.proto, what.timeoutMs,
                                    [this](const hal::MdnsHit& hit) {
                                      if (serviceCount_ < LMP_PROBE_HITS) services_[serviceCount_++] = hit;
                                    });
      break;
    case ProbeKind::Ssdp:
      a.result = probe_->ssdpSearch(what.service, what.timeoutMs, [this](const hal::SsdpHit& hit) {
        // A device answers once for each thing it is: the first answer with its name is kept.
        for (size_t i = 0; i < deviceCount_; ++i) {
          if (devices_[i].ip == hit.ip) {
            if (!devices_[i].server[0] && hit.server[0]) devices_[i] = hit;
            return;
          }
        }
        if (deviceCount_ < LMP_PROBE_HITS) devices_[deviceCount_++] = hit;
      });
      break;
    case ProbeKind::Hop:
      a.result = probe_->hop(what.ip, what.ttl, what.timeoutMs, a.from, a.rttMs, a.reached);
      break;
    case ProbeKind::Udp:
      a.result = probe_->udpAsk(what.ip, what.port, what.data, what.len, what.reply, what.cap,
                                a.got, what.timeoutMs, what.anyPort);
      break;
    case ProbeKind::Ports:
      a.result = probe_->tcpMany(what.ip, what.ports, what.count, what.timeoutMs, what.open);
      break;
  }
  hal::LockGuard guard(mutex_);
  answer_ = a;
  answer_.hosts = Span<const ProbeHost>(hosts_, hostCount_);
  answer_.services = Span<const hal::MdnsHit>(services_, serviceCount_);
  answer_.devices = Span<const hal::SsdpHit>(devices_, deviceCount_);
  finished_ = true;
}

void ProbeService::tick(uint32_t) {
  worker_.poll();
  Entry ended;
  ProbeAnswer answer;
  bool tell;
  {
    hal::LockGuard guard(mutex_);
    if (!busy_ || !finished_) return;
    ended = flying_;
    flying_.done.reset();
    answer = answer_;
    tell = !dropped_;
    busy_ = false;
    finished_ = false;
  }
  lastKind_ = ended.what.kind;
  lastIp_ = ended.what.ip;
  lastPort_ = ended.what.port;
  lastResult_ = answer.result;
  lastRttMs_ = answer.rttMs;
  lastFound_ = static_cast<uint32_t>(answer.hosts.size() + answer.services.size() + answer.devices.size());
  asked_ = true;
  // A device that does not answer, or a port that refuses, is an answer; a question that
  // could not travel is a failure.
  if (answer.result != Status::Ok && answer.result != Status::Timeout &&
      answer.result != Status::Denied) {
    ++failures_;
    LMP_LOGW(src_, "%s: %s", probeKindName(ended.what.kind), statusName(answer.result));
  }
  // The room of the answer goes with it: who is told can ask again at once, and the question
  // that leaves then takes room of its own, which must not be freed here.
  ProbeHost* hosts = hosts_;
  hal::MdnsHit* services = services_;
  hal::SsdpHit* devices = devices_;
  hosts_ = nullptr;
  services_ = nullptr;
  devices_ = nullptr;
  hostCount_ = hostCap_ = serviceCount_ = deviceCount_ = 0;
  if (tell && ended.done) ended.done(answer);
  if (hosts) hal::freeLarge(hosts);
  if (services) hal::freeLarge(services);
  if (devices) hal::freeLarge(devices);
  launch();
}

Status ProbeService::askFromConsole(const ProbeAsk& what, CmdContext& ctx) {
  const uint32_t ref = ctx.ref;
  const Status s = ask(what, [this, ref](const ProbeAnswer& a) {
    if (ref) kernel_->events().publish(commandDone(ref, a.result));
  });
  return s == Status::Ok ? Status::Pending : s;
}

void ProbeService::commands(CommandSet& set) {
  set.add("ping", LMP_HELP("Asks a device of the network for an answer: how long it takes"),
          kPingArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            ProbeAsk what;
            what.kind = ProbeKind::Ping;
            if (!hal::Ip4::parse(a.str("host"), what.ip)) {
              ctx.errorField = "host";
              return Status::Invalid;
            }
            what.timeoutMs = a.u32("timeoutMs", 1000);
            return askFromConsole(what, ctx);
          },
          kCmdAsync);
  set.add("tcp", LMP_HELP("Whether a port of a device takes connections"), kTcpArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            ProbeAsk what;
            what.kind = ProbeKind::Tcp;
            if (!hal::Ip4::parse(a.str("host"), what.ip)) {
              ctx.errorField = "host";
              return Status::Invalid;
            }
            what.port = a.u16("port");
            what.timeoutMs = a.u32("timeoutMs", 1000);
            return askFromConsole(what, ctx);
          },
          kCmdAsync);
  set.add("wake", LMP_HELP("Wakes a PC with its MAC (Wake-on-LAN)"), kWakeArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            ProbeAsk what;
            what.kind = ProbeKind::Wake;
            if (!parseMac(a.str("mac"), what.mac)) {
              ctx.errorField = "mac";
              return Status::Invalid;
            }
            if (a.has("broadcast") && !hal::Ip4::parse(a.str("broadcast"), what.ip)) {
              ctx.errorField = "broadcast";
              return Status::Invalid;
            }
            return askFromConsole(what, ctx);
          },
          kCmdAsync);
}

void ProbeService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  w.boolean("available", probe_ != nullptr);
  w.boolean("busy", busy_);
  w.u32("waiting", static_cast<uint32_t>(count_));
  w.u32("questions", questions_);
  w.u32("failures", failures_);
  if (!asked_) return;
  w.beginObject("last");
  w.str("what", probeKindName(lastKind_));
  char ip[16];
  lastIp_.toString(ip, sizeof(ip));
  if (lastKind_ != ProbeKind::Mdns && lastKind_ != ProbeKind::Ssdp) w.str("host", ip);
  if (lastKind_ == ProbeKind::Tcp) w.u32("port", lastPort_);
  w.str("result", statusName(lastResult_));
  if ((lastKind_ == ProbeKind::Ping || lastKind_ == ProbeKind::Tcp) && lastResult_ == Status::Ok) {
    w.u32("rttMs", lastRttMs_);
  }
  if (lastKind_ == ProbeKind::Sweep || lastKind_ == ProbeKind::Mdns || lastKind_ == ProbeKind::Ssdp) {
    w.u32("found", lastFound_);
  }
  w.endObject();
}

}  // namespace lmp

#endif  // LMP_FEATURE_PROBE
