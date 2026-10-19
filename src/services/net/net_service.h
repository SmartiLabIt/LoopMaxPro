// LoopMaxPro - network service.
// Access point and client of a network are two separate things: "connected" always means the
// client. The service decides when to try again, when to bring the access point up as a
// fallback, and takes the device back to the network it had when a change does not work.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_NET

#include "base/fixed_string.h"
#include "hal/api/ilink.h"
#include "hal/api/inet.h"
#include "kernel/kernel.h"

namespace lmp {
struct HttpAnswer;
}

namespace lmp {

enum class NetState : uint8_t {
  Off,
  ApOnly,       // mode AP: access point up, no client
  Connecting,   // mode LAN: first attempts
  Connected,    // mode LAN: the client has an address
  Retrying,     // mode LAN: connection lost or refused, trying again
  Fallback      // mode LAN: too many failures, access point up while it keeps trying
};
const char* netStateName(NetState state);

enum class JoinPhase : uint8_t {
  None,
  Trying,      // a new network is being tried
  Reverting    // it did not work: going back to the network of before
};
const char* joinPhaseName(JoinPhase phase);

enum class JoinResult : uint8_t {
  None,
  Joined,      // new network accepted and saved
  Tested,      // credentials tried and found good, nothing saved
  NotFound,    // no network with that name around
  Refused,     // the network did not accept the device: most of the times, the password
  TimedOut     // no address within the time allowed
};
const char* joinResultName(JoinResult result);

// Interface offered to the other services.
class INetService {
 public:
  static constexpr ServiceId kId = ServiceId::Net;
  virtual ~INetService() = default;

  virtual NetState netState() const = 0;
  // True when the client of the network has an address.
  virtual bool connected() const = 0;
  virtual bool apUp() const = 0;
  // Address of the client when connected, otherwise of the access point. Not set when off.
  virtual hal::Ip4 address() const = 0;
  virtual hal::Ip4 apAddress() const = 0;
  virtual bool captive() const = 0;
  // True when the address is of a client of the access point: it got in with its password.
  virtual bool fromAccessPoint(const hal::Ip4& client) const = 0;
  // True when the last check found Internet reachable. False also when nobody checked.
  virtual bool internet() const = 0;
  // The signal of the network the client is on, in dBm; 0 when it is on none.
  virtual int8_t signal() const = 0;
  // The devices of the system the last search found in this network (sys.net near). Empty
  // where the board does not look for them.
  virtual Span<const hal::NetPeer> peers() const = 0;
  // Starts a search of them now: they are in peers() a few seconds later. Busy while one runs,
  // Denied out of a network.
  virtual Status findPeers() = 0;
  // How the device reaches the network: its Wi-Fi, or a cable chosen in the settings.
  virtual hal::LinkKind link() const { return hal::LinkKind::Wifi; }
};

class NetService : public Service, public INetService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "net"; }
  void* api() override { return static_cast<INetService*>(this); }

  NetState netState() const override { return state_; }
  bool connected() const override { return state_ == NetState::Connected; }
  bool apUp() const override { return apUp_; }
  hal::Ip4 address() const override;
  hal::Ip4 apAddress() const override;
  bool captive() const override { return captiveUp_; }
  bool fromAccessPoint(const hal::Ip4& client) const override;
  bool internet() const override { return internet_; }
  int8_t signal() const override;
#if LMP_FEATURE_MDNS
  Span<const hal::NetPeer> peers() const override {
    return Span<const hal::NetPeer>(near_, nearCount_);
  }
  Status findPeers() override { return kernel_ ? searchNear(kernel_->now()) : Status::Denied; }
#else
  Span<const hal::NetPeer> peers() const override { return Span<const hal::NetPeer>(); }
  Status findPeers() override { return Status::Unsupported; }
#endif
  hal::LinkKind link() const override { return link_ ? link_->kind() : hal::LinkKind::Wifi; }

  JoinPhase joinPhase() const { return join_; }
  JoinResult lastJoin() const { return lastJoin_; }
  size_t scanCount() const { return scanCount_; }

 private:
  using Ssid = FixedString<32>;
  using Password = FixedString<63>;

  // Called by the driver, in its own context: only takes note.
  void noteEvent(hal::NetEvent event);
  uint32_t takeEvents();
  void handleEvents(uint32_t events, uint32_t nowMs);

  void applyMode(uint32_t nowMs);
  Status startAp();
  void stopAp();
  void startNames();
  void stopNames();
  // The device among the others of the system: what it says of itself on the
  // network, the search of the others, what the search found.
  void announce();
  Status searchNear(uint32_t nowMs);
  void collectNear(uint32_t nowMs);
  void connect(const Ssid& ssid, const Password& password, uint32_t nowMs);
  void dropClient();
  void onConnected(uint32_t nowMs);
  void onAttemptFailed(uint32_t nowMs, const char* why, hal::StaFail fail);
  void enterFallback(uint32_t nowMs);
  void scheduleRetry(uint32_t nowMs);
  // The way to the network that is not the Wi-Fi: a cable or a modem. It needs no attempts:
  // the address comes by itself, and the access point comes up while it does not.
  void startLink(uint32_t nowMs);
  void onLinkLost(uint32_t nowMs);
  void armLinkWait(uint32_t nowMs);
  // The way to the network chosen in the settings: "wifi", or one the board has and answers.
  Status changeLink(const char* name, CmdContext& ctx);

  Status beginJoin(const char* ssid, const char* password, bool test, CmdContext& ctx);
  void joinAccepted(uint32_t nowMs);
  void joinFailed(uint32_t nowMs, JoinResult why);
  void collectScan(uint32_t nowMs);
  bool usingDefaultApPassword() const;

  // Check that Internet is reachable.
  void armProbe(uint32_t atMs);
  void probe(uint32_t nowMs);
  void onProbe(const HttpAnswer& answer);
  void internetIs(bool up);

  // The guard of the network: a device that should be on it and has lost it for long restarts.
  void guardNetwork(uint32_t nowMs);

  Kernel* kernel_ = nullptr;
  hal::INet* net_ = nullptr;
  // The cable of the board, when the build looks for one, and whether its chip answered.
  hal::ILink* cable_ = nullptr;
  bool cableFound_ = false;
  // The way in use when it is not the Wi-Fi: the cable, chosen in the settings.
  hal::ILink* link_ = nullptr;
  bool linkWaitArmed_ = false;     // the access point comes up when the address is late
  uint32_t linkWaitAtMs_ = 0;
  hal::IMutex* eventsMutex_ = nullptr;
  LogSource src_ = kLogSys;
  uint32_t pending_ = 0;
  uint32_t nowMs_ = 0;

  NetState state_ = NetState::Off;
  bool apUp_ = false;
  bool captiveUp_ = false;
  bool namesUp_ = false;
  FixedString<32> namesHost_;

  // client of the network
  Ssid ssid_;                  // what the client is using now
  Password password_;
  bool attempting_ = false;    // an attempt is in progress
  uint32_t attemptStartMs_ = 0;
  uint8_t failures_ = 0;       // attempts failed in a row
  bool retryArmed_ = false;
  uint32_t retryAtMs_ = 0;
  bool apDownArmed_ = false;   // the access point goes down a while after the connection
  uint32_t apDownAtMs_ = 0;

  // change of network
  JoinPhase join_ = JoinPhase::None;
  JoinResult lastJoin_ = JoinResult::None;
  bool joinIsTest_ = false;
  uint8_t joinFailures_ = 0;
  uint32_t joinDeadlineMs_ = 0;
  uint32_t joinRef_ = 0;
  Ssid joinSsid_;
  Ssid previousSsid_;
  Password previousPassword_;
  Mode previousMode_ = Mode::Ap;
  NetState previousState_ = NetState::Off;

  // scan
  hal::ScanState scanState_ = hal::ScanState::Idle;
  hal::WifiNetwork scan_[LMP_NET_SCAN_MAX];
  size_t scanCount_ = 0;
  uint32_t scanAtMs_ = 0;
  uint32_t scanRef_ = 0;

  // Internet
  bool internet_ = false;
  bool internetKnown_ = false;   // at least one check gave an outcome
  bool probeArmed_ = false;
  bool probing_ = false;
  uint32_t probeAtMs_ = 0;
  FixedString<96> probeUrl_;     // the address of the check in progress

  // guard
  bool lostArmed_ = false;
  uint32_t lostSinceMs_ = 0;

#if LMP_FEATURE_MDNS
  // the others of the system
  char selfId_[18] = "";               // the public id of this device: its MAC address
  hal::NetPeer near_[LMP_NET_NEAR_MAX];
  size_t nearCount_ = 0;
  hal::ScanState nearState_ = hal::ScanState::Idle;
  uint32_t nearAtMs_ = 0;              // when the last search ended
  uint32_t nearRef_ = 0;
  bool nearArmed_ = false;             // a search by itself is due
  uint32_t nearDueMs_ = 0;
  // The search waits for the answers of the network: it runs in the worker, and leaves
  // here what it found for the round of the system to take.
  hal::NetPeer* nearFound_ = nullptr;
  size_t nearFoundCount_ = 0;
  Status nearResult_ = Status::Ok;
  volatile bool nearBack_ = false;
#endif
};

}  // namespace lmp

#endif  // LMP_FEATURE_NET
