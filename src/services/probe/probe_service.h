// LoopMaxPro - questions to the network: who answers a ping, which ports are open, who is in the
// network and what it announces, and the packet that wakes a PC.
// A question lasts up to seconds, a whole network half a minute: it runs on a task of this
// service, never on the application task, and not on the worker of the system either, which
// keeps writing files and asking servers meanwhile. Who asks gets the answer later, on the
// application task. One question travels at a time, the others wait.
//
// For the tools of a network (ExpNet) and for the diagnosis of any board: ping, tcp
// and wake are commands of the console and of the serial port too.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_PROBE

#include "hal/api/iprobe.h"
#include "kernel/kernel.h"
#include "kernel/worker/worker.h"

namespace lmp {

enum class ProbeKind : uint8_t { Ping, Tcp, Wake, Sweep, Mdns, Ssdp, Hop, Udp, Ports };
const char* probeKindName(ProbeKind kind);

// A device found in the network, with its MAC.
struct ProbeHost {
  hal::Ip4 ip;
  uint8_t mac[6] = {};
};

struct ProbeAsk {
  ProbeKind kind = ProbeKind::Ping;
  // Ping, tcp: who. Wake: where the packet goes, a broadcast address. Sweep: the first address.
  hal::Ip4 ip;
  uint16_t port = 0;        // tcp, udp
  uint16_t count = 0;       // sweep: how many addresses from ip; ports: how many ports
  uint32_t timeoutMs = 1000;
  uint8_t mac[6] = {};      // wake: the PC
  uint8_t ttl = 0;          // hop: how many routers the echo lives
  // Mdns: the kind of service, "_http" and "_tcp". Ssdp: the kind of device, null all of
  // them. Must stay valid until the answer.
  const char* service = nullptr;
  const char* proto = "_tcp";
  // Udp: the question, and room for the answer. Ports: the ports, and one byte each for the
  // answer, 1 open. Of who asks: valid, and not read, until the answer.
  const uint8_t* data = nullptr;
  size_t len = 0;
  uint8_t* reply = nullptr;
  size_t cap = 0;
  const uint16_t* ports = nullptr;
  uint8_t* open = nullptr;
  // Udp: the answer from any port of ip, as an SSDP device answers an M-SEARCH sent to it alone.
  bool anyPort = false;
};

struct ProbeAnswer {
  Status result = Status::Ok;
  // Ping, tcp: the time of the answer. Tcp: Ok the port takes it, Denied it is refused by a
  // device that is there, Timeout nobody answered.
  uint32_t rttMs = 0;
  // Hop: who answered, and whether it was the device itself. Denied: unreachable, by from.
  hal::Ip4 from;
  bool reached = false;
  // Udp: the length of the answer, in reply.
  size_t got = 0;
  // What a sweep, a search with mDNS or SSDP found: valid only while done runs.
  Span<const ProbeHost> hosts;
  Span<const hal::MdnsHit> services;
  Span<const hal::SsdpHit> devices;
};

using ProbeDone = Delegate<void(const ProbeAnswer&)>;

class IProbeService {
 public:
  static constexpr ServiceId kId = ServiceId::Probe;
  virtual ~IProbeService() = default;

  // Queues a question. done runs on the application task. Busy when the queue is full,
  // Denied when the service is not running, Invalid for a question that cannot be asked.
  virtual Status ask(const ProbeAsk& what, ProbeDone done) = 0;
  // True when questions are taken: the device is on a network and the board can ask.
  virtual bool ready() const = 0;
  // The network of the device: its address and mask. False when it is not on one.
  virtual bool network(hal::Ip4& address, hal::Ip4& mask) const = 0;
};

class ProbeService : public Service, public IProbeService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<IProbeService*>(this); }

  Status ask(const ProbeAsk& what, ProbeDone done) override;
  bool ready() const override { return probe_ && running(); }
  bool network(hal::Ip4& address, hal::Ip4& mask) const override;

  size_t waiting() const { return count_; }

 private:
  struct Entry {
    ProbeAsk what;
    ProbeDone done;
  };
  // A question of the console: its answer goes to the event that closes the command.
  struct Mine {
    uint32_t ref = 0;
  };

  void launch();
  // Task of the service.
  void travel();
  void finish(const Entry& ended, const ProbeAnswer& answer);
  void freeRoom();
  Status askFromConsole(const ProbeAsk& what, CmdContext& ctx);

  Kernel* kernel_ = nullptr;
  hal::INetProbe* probe_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  Worker worker_;
  bool workerUp_ = false;
  LogSource src_ = kLogSys;

  Entry queue_[LMP_PROBE_QUEUE];
  size_t head_ = 0;
  size_t count_ = 0;

  // The question that travels. Written by the task of the service until finished_ is set,
  // read by the application task after.
  Entry flying_;
  ProbeAnswer answer_;
  bool busy_ = false;
  bool finished_ = false;
  bool dropped_ = false;
  // Room for what a question finds: taken when it leaves, freed when its answer is given.
  ProbeHost* hosts_ = nullptr;
  size_t hostCount_ = 0, hostCap_ = 0;
  hal::MdnsHit* services_ = nullptr;
  size_t serviceCount_ = 0;
  hal::SsdpHit* devices_ = nullptr;
  size_t deviceCount_ = 0;

  // The last question, for who looks at the service.
  ProbeKind lastKind_ = ProbeKind::Ping;
  hal::Ip4 lastIp_;
  uint16_t lastPort_ = 0;
  Status lastResult_ = Status::Ok;
  uint32_t lastRttMs_ = 0;
  uint32_t lastFound_ = 0;
  bool asked_ = false;
  uint32_t questions_ = 0;
  uint32_t failures_ = 0;
};

// "aa:bb:cc:dd:ee:ff", or with "-" between the parts, or the 12 digits alone.
bool parseMac(const char* text, uint8_t* mac);
// Writes "AA:BB:CC:DD:EE:FF". The buffer must hold at least 18 characters.
void macToString(const uint8_t* mac, char* out, size_t cap);
// The 102 bytes that wake a PC: six times 0xFF, then its MAC sixteen times.
void magicPacket(const uint8_t* mac, uint8_t* out);
// The broadcast address of a network: the address with every bit outside the mask at one.
hal::Ip4 broadcastOf(hal::Ip4 address, hal::Ip4 mask);

}  // namespace lmp

#endif  // LMP_FEATURE_PROBE
