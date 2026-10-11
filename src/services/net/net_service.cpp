#include "services/net/net_service.h"

#if LMP_FEATURE_NET

#include <stdio.h>
#include <string.h>

#include "hal/api/memory.h"
#include "lmp_version.h"
#if LMP_FEATURE_MODULES
#include "services/modules/modules_service.h"
#endif

#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif

namespace lmp {
namespace {

constexpr hal::Ip4 kApAddress = hal::Ip4::of(192, 168, 4, 1);
// The access point gives addresses that differ from its own in the last number only.
constexpr hal::Ip4 kApMask = hal::Ip4::of(255, 255, 255, 0);
// One attempt to reach a network, from the request to the address.
constexpr uint32_t kAttemptTimeoutMs = 20000;
constexpr uint32_t kRetryFirstMs = 2000;
constexpr uint32_t kRetryMaxMs = 30000;
// Time the access point stays up after the client connected: who was using it can read
// the new address.
constexpr uint32_t kApGraceMs = 30000;
constexpr uint32_t kTestTimeoutMs = 30000;
constexpr uint32_t kScanTimeoutMs = 15000;
// A new network that refuses the device this many times is given up before the time is over.
constexpr uint8_t kJoinAttempts = 3;
// The first check of Internet comes a moment after the address, the following ones when the
// service for the requests was not ready yet.
constexpr uint32_t kProbeFirstMs = 1500;
constexpr uint32_t kProbeWaitMs = 3000;
constexpr uint32_t kProbeTimeoutMs = 6000;
#if LMP_FEATURE_MDNS
// The search of the other devices of the system: how long the answers of the network are
// waited for, and how long after the device joined the network it looks by itself.
constexpr uint32_t kNearSearchMs = 2500;
constexpr uint32_t kNearFirstMs = 45000;
// One more than what is kept: the device itself is among the answers.
constexpr size_t kNearAsked = LMP_NET_NEAR_MAX + 1;

// The same device: by its id, or by its name for one that did not say an id.
bool samePeer(const hal::NetPeer& a, const hal::NetPeer& b) {
  return a.id[0] || b.id[0] ? strcmp(a.id, b.id) == 0 : strcmp(a.host, b.host) == 0;
}
int peerOrder(const hal::NetPeer& a, const hal::NetPeer& b) {
  const int byId = strcmp(a.id, b.id);
  return byId ? byId : strcmp(a.host, b.host);
}
// Nothing of what is told of it changed.
bool samePeerData(const hal::NetPeer& a, const hal::NetPeer& b) {
  return samePeer(a, b) && a.ip == b.ip && a.port == b.port && a.api == b.api &&
         strcmp(a.host, b.host) == 0 && strcmp(a.name, b.name) == 0 &&
         strcmp(a.code, b.code) == 0 && strcmp(a.modules, b.modules) == 0 &&
         strcmp(a.title, b.title) == 0 && strcmp(a.icon, b.icon) == 0 &&
         strcmp(a.fw, b.fw) == 0 && strcmp(a.board, b.board) == 0 &&
         strcmp(a.edition, b.edition) == 0;
}
#endif

constexpr ArgSpec kJoinArgs[] = {
    Arg::str("ssid", 1, 32),
    Arg::secret("psw", 0, 63).optional(),
};

constexpr ArgSpec kApArgs[] = {
    Arg::str("ssid", 1, 32).optional(),
    Arg::secret("psw", 8, 63).optional(),
};

constexpr ArgSpec kModeArgs[] = {
    Arg::str("mode", 2, 3),
};

constexpr uint32_t bit(hal::NetEvent e) { return 1u << static_cast<uint8_t>(e); }

// Milliseconds from since to now, read with their sign: a request that arrives during a
// round of the system carries a time later than the one of the round.
int32_t elapsed(uint32_t nowMs, uint32_t sinceMs) { return static_cast<int32_t>(nowMs - sinceMs); }
bool reached(uint32_t nowMs, uint32_t atMs) { return elapsed(nowMs, atMs) >= 0; }

// A protected network has a password of 8 to 63 characters. None means an open network.
bool validPassword(const char* password) {
  const size_t n = strlen(password);
  return n == 0 || (n >= 8 && n <= 63);
}

bool sameNet(const hal::Ip4& a, const hal::Ip4& b, const hal::Ip4& mask) {
  for (size_t i = 0; i < 4; ++i) {
    if ((a.b[i] & mask.b[i]) != (b.b[i] & mask.b[i])) return false;
  }
  return true;
}

void writeIp(JsonWriter& w, const char* key, const hal::Ip4& ip) {
  char text[16];
  ip.toString(text, sizeof(text));
  w.str(key, text);
}

// The access point of a device of the system, by the name it is born with.
bool systemAp(const char* ssid) {
  return strncmp(ssid, kApNamePrefix, sizeof(kApNamePrefix) - 1) == 0;
}

// What stays when the networks found are more than the list holds: the access points of the
// devices of the system before the networks of anybody else, then the stronger signal. New
// devices are found in this list, also the ones far from here.
int scanRank(const hal::WifiNetwork& n) { return (systemAp(n.ssid) ? 1000 : 0) + n.rssi; }

const char* scanStateName(hal::ScanState state) {
  switch (state) {
    case hal::ScanState::Running: return "running";
    case hal::ScanState::Done:    return "done";
    case hal::ScanState::Failed:  return "failed";
    case hal::ScanState::Idle:    break;
  }
  return "idle";
}

}  // namespace

const char* netStateName(NetState state) {
  switch (state) {
    case NetState::ApOnly:     return "ap";
    case NetState::Connecting: return "connecting";
    case NetState::Connected:  return "connected";
    case NetState::Retrying:   return "retrying";
    case NetState::Fallback:   return "fallback";
    case NetState::Off:        break;
  }
  return "off";
}

const char* joinPhaseName(JoinPhase phase) {
  switch (phase) {
    case JoinPhase::Trying:    return "trying";
    case JoinPhase::Reverting: return "reverting";
    case JoinPhase::None:      break;
  }
  return "none";
}

const char* joinResultName(JoinResult result) {
  switch (result) {
    case JoinResult::Joined:   return "joined";
    case JoinResult::Tested:   return "tested";
    case JoinResult::NotFound: return "not_found";
    case JoinResult::Refused:  return "refused";
    case JoinResult::TimedOut: return "timeout";
    case JoinResult::None:     break;
  }
  return "none";
}

const ServiceInfo& NetService::info() const {
  static const ServiceInfo kInfo{ServiceId::Net, "net", "\xF0\x9F\x9B\x9C", Mode::Any,
                                 RestartPolicy::Retry, 100,
                                 DepList(ServiceId::Config, ServiceId::Log)};
  return kInfo;
}

hal::Ip4 NetService::apAddress() const { return kApAddress; }

hal::Ip4 NetService::address() const {
  if (!net_) return hal::Ip4();
  if (state_ == NetState::Connected) {
    hal::StaInfo sta;
    net_->staInfo(sta);
    return sta.ip;
  }
  return apUp_ ? kApAddress : hal::Ip4();
}

bool NetService::fromAccessPoint(const hal::Ip4& client) const {
  if (!net_ || !apUp_) return false;
  if (client == kApAddress || !sameNet(client, kApAddress, kApMask)) return false;
  // A network with the same addresses of the access point: nobody can tell who comes from where.
  hal::StaInfo sta;
  net_->staInfo(sta);
  const bool onNetwork = sta.state == hal::StaState::Connected && sta.ip.isSet();
  return !(onNetwork && sameNet(sta.ip, kApAddress, kApMask));
}

// ---------------------------------------------------------------- life cycle

Status NetService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  net_ = kernel.board().net;
  if (!net_) {
    fail("no network on this board");
    return Status::Unsupported;
  }
  if (!eventsMutex_ && kernel.board().rtos) eventsMutex_ = kernel.board().rtos->createMutex();

  state_ = NetState::Off;
  apUp_ = captiveUp_ = namesUp_ = false;
  attempting_ = retryArmed_ = apDownArmed_ = false;
  failures_ = 0;
  join_ = JoinPhase::None;
  scanState_ = hal::ScanState::Idle;
  scanCount_ = 0;
  scanRef_ = 0;
#if LMP_FEATURE_MDNS
  nearCount_ = 0;
  nearState_ = hal::ScanState::Idle;
  nearRef_ = 0;
  nearArmed_ = false;
#endif
  internet_ = internetKnown_ = false;
  probeArmed_ = false;
  takeEvents();

  const Status up = net_->init(kernel.config().sys.hostname.c_str());
  if (up != Status::Ok) {
    fail("the radio does not start");
    return up;
  }
  net_->setEventHandler([this](hal::NetEvent e) { noteEvent(e); });
  nowMs_ = kernel.now();
  applyMode(nowMs_);
  return Status::Ok;
}

void NetService::stop() {
  if (!net_) return;
  stopNames();
  if (apUp_) stopAp();
  dropClient();
  net_->setEventHandler(hal::NetEventHandler());
  state_ = NetState::Off;
  join_ = JoinPhase::None;
}

void NetService::applyMode(uint32_t nowMs) {
  Config& config = kernel_->config();
  if (kernel_->mode() == Mode::Lan && config.net.staSsid.empty()) {
    // Nothing to connect to: the device would be unreachable.
    LMP_LOGW(src_, "mode LAN without a network: back to the access point");
    config.sys.mode = 1;
    config.touch(Config::kSys);
    kernel_->setMode(Mode::Ap);
  }

  if (kernel_->mode() != Mode::Lan) {
    if (state_ == NetState::ApOnly && apUp_) return;
    dropClient();
    failures_ = 0;
    apDownArmed_ = false;
    state_ = NetState::ApOnly;
    if (!apUp_) startAp();
    return;
  }

  // The client is already where the settings say, or on its way there: the mode is
  // announced more than once at startup, and after a change of network just accepted.
  const bool onTrack = state_ != NetState::Off && state_ != NetState::ApOnly;
  if (onTrack && ssid_ == config.net.staSsid && password_ == config.net.staPsw) return;

  dropClient();
  failures_ = 0;
  if (apUp_) stopAp();
  state_ = NetState::Connecting;
  connect(config.net.staSsid, config.net.staPsw, nowMs);
}

// ---------------------------------------------------------------- access point and names

Status NetService::startAp() {
  const Config& config = kernel_->config();
  // Devices close to each other spread over the three channels that do not overlap.
  hal::Mac mac;
  net_->staMac(mac);
  const uint8_t channel =
      LMP_NET_AP_CHANNEL ? LMP_NET_AP_CHANNEL : static_cast<uint8_t>(1 + 5 * (mac.b[5] % 3));

  const Status s =
      net_->apStart(config.net.apSsid.c_str(), config.net.apPsw.c_str(), kApAddress, channel);
  if (s != Status::Ok) {
    LMP_LOGE(src_, "access point not started: %s", statusName(s));
    degrade("access point not started");
    return s;
  }
  recover();
  apUp_ = true;
  apDownArmed_ = false;
  captiveUp_ = config.net.captive && net_->captiveStart(kApAddress) == Status::Ok;
  startNames();
  LMP_LOGI(src_, "access point '%s' on %u.%u.%u.%u, channel %u", config.net.apSsid.c_str(),
           kApAddress.b[0], kApAddress.b[1], kApAddress.b[2], kApAddress.b[3],
           static_cast<unsigned>(channel));
  kernel_->events().publish(Event(EventId::NetApUp));
  return Status::Ok;
}

void NetService::stopAp() {
  if (captiveUp_) net_->captiveStop();
  captiveUp_ = false;
  net_->apStop();
  apUp_ = false;
  apDownArmed_ = false;
  LMP_LOGI(src_, "access point down");
  kernel_->events().publish(Event(EventId::NetApDown));
}

void NetService::startNames() {
#if !LMP_FEATURE_MDNS
  return;
#endif
  const Config& config = kernel_->config();
  if (namesUp_ && namesHost_ == config.sys.hostname) return;
  if (namesUp_) net_->mdnsStop();
  namesHost_ = config.sys.hostname;
  namesUp_ = net_->mdnsStart(namesHost_.c_str(), LMP_HTTP_PORT) == Status::Ok;
  if (namesUp_) LMP_LOGD(src_, "name %s.local", namesHost_.c_str());
  announce();
}

void NetService::stopNames() {
  if (namesUp_) net_->mdnsStop();
  namesUp_ = false;
}

// ---------------------------------------------------------------- client

void NetService::connect(const Ssid& ssid, const Password& password, uint32_t nowMs) {
  ssid_ = ssid;
  password_ = password;
  retryArmed_ = false;
  attempting_ = true;
  attemptStartMs_ = nowMs;
  LMP_LOGI(src_, "connecting to '%s'", ssid_.c_str());
  const Status s = net_->staConnect(ssid_.c_str(), password_.c_str());
  if (s != Status::Ok) onAttemptFailed(nowMs, statusName(s), hal::StaFail::Other);
}

void NetService::dropClient() {
  const bool was = state_ == NetState::Connected;
  if (was || attempting_) net_->staDisconnect();
  attempting_ = false;
  retryArmed_ = false;
  if (was) {
    kernel_->events().publish(Event(EventId::NetStaLost));
    internetIs(false);
  }
  probeArmed_ = false;
  internetKnown_ = false;
}

void NetService::scheduleRetry(uint32_t nowMs) {
  uint32_t delay;
  if (state_ == NetState::Fallback) {
    delay = static_cast<uint32_t>(LMP_NET_FALLBACK_RETRY_SEC) * 1000u;
  } else {
    delay = kRetryFirstMs;
    for (uint8_t i = 1; i < failures_ && delay < kRetryMaxMs; ++i) delay *= 2;
    if (delay > kRetryMaxMs) delay = kRetryMaxMs;
  }
  retryArmed_ = true;
  retryAtMs_ = nowMs + delay;
}

void NetService::enterFallback(uint32_t nowMs) {
  state_ = NetState::Fallback;
  if (join_ == JoinPhase::Reverting) join_ = JoinPhase::None;
  LMP_LOGW(src_, "'%s' not reachable: access point up as a fallback", ssid_.c_str());
  if (!apUp_) startAp();
  kernel_->events().publish(Event(EventId::NetFallback));
  (void)nowMs;
}

void NetService::onConnected(uint32_t nowMs) {
  attempting_ = false;
  retryArmed_ = false;
  failures_ = 0;
  state_ = NetState::Connected;

  hal::StaInfo sta;
  net_->staInfo(sta);
  LMP_LOGI(src_, "connected to '%s', address %u.%u.%u.%u, signal %d dBm", ssid_.c_str(),
           sta.ip.b[0], sta.ip.b[1], sta.ip.b[2], sta.ip.b[3], static_cast<int>(sta.rssi));

  if (join_ == JoinPhase::Trying) {
    joinAccepted(nowMs);
    if (state_ != NetState::Connected) return;  // it was a test: the client has left
  } else if (join_ == JoinPhase::Reverting) {
    join_ = JoinPhase::None;
    LMP_LOGI(src_, "back on '%s'", ssid_.c_str());
  }

  recover();
  startNames();
#if LMP_FEATURE_MDNS
  // A look around a little later, when the others have had the time to see this device too.
  nearArmed_ = true;
  nearDueMs_ = nowMs + kNearFirstMs;
#endif
  if (apUp_) {
    apDownArmed_ = true;
    apDownAtMs_ = nowMs + kApGraceMs;
  }
  kernel_->events().publish(Event(EventId::NetStaConnected));
  armProbe(nowMs + kProbeFirstMs);
}

void NetService::onAttemptFailed(uint32_t nowMs, const char* why, hal::StaFail fail) {
  attempting_ = false;
  if (state_ == NetState::Connected) {
    LMP_LOGW(src_, "connection to '%s' lost", ssid_.c_str());
    kernel_->events().publish(Event(EventId::NetStaLost));
    state_ = NetState::Retrying;
    probeArmed_ = false;
    internetKnown_ = false;
    internetIs(false);
  }

  if (join_ == JoinPhase::Trying) {
    LMP_LOGI(src_, "'%s': %s, %s", ssid_.c_str(),
             fail == hal::StaFail::NotFound
                 ? "not found"
                 : (fail == hal::StaFail::WrongPassword ? "not accepted" : "no connection"),
             why ? why : "?");
    if (++joinFailures_ >= kJoinAttempts) {
      joinFailed(nowMs, fail == hal::StaFail::NotFound ? JoinResult::NotFound
                                                       : JoinResult::Refused);
    } else {
      retryArmed_ = true;
      retryAtMs_ = nowMs + kRetryFirstMs;
    }
    return;
  }

  if (failures_ < 255) ++failures_;
  LMP_LOGW(src_, "'%s' not reached: %s, attempt %u", ssid_.c_str(), why ? why : "?",
           static_cast<unsigned>(failures_));
  if (state_ != NetState::Fallback) {
    if (failures_ >= LMP_NET_FALLBACK_AFTER) {
      enterFallback(nowMs);
    } else {
      state_ = NetState::Retrying;
    }
  }
  scheduleRetry(nowMs);
}

// ---------------------------------------------------------------- events of the driver

void NetService::noteEvent(hal::NetEvent event) {
  hal::LockGuard guard(eventsMutex_);
  pending_ |= bit(event);
}

uint32_t NetService::takeEvents() {
  hal::LockGuard guard(eventsMutex_);
  const uint32_t events = pending_;
  pending_ = 0;
  return events;
}

void NetService::handleEvents(uint32_t events, uint32_t nowMs) {
  if (events & bit(hal::NetEvent::ScanDone)) collectScan(nowMs);

  if (events & (bit(hal::NetEvent::ApClientJoined) | bit(hal::NetEvent::ApClientLeft))) {
    hal::ApInfo ap;
    net_->apInfo(ap);
    LMP_LOGD(src_, "access point: %u connected", static_cast<unsigned>(ap.clients));
  }

  const bool lost = (events & (bit(hal::NetEvent::StaLost) | bit(hal::NetEvent::StaFailed))) != 0;
  const bool gotAddress = (events & bit(hal::NetEvent::StaGotIp)) != 0;
  if (!lost && !gotAddress) return;

  // Several events can arrive between two rounds, in an order that is not known here:
  // what counts is where the client is now.
  hal::StaInfo sta;
  net_->staInfo(sta);
  const bool up = sta.state == hal::StaState::Connected && sta.ip.isSet();

  if (gotAddress && up) {
    if (attempting_ || state_ != NetState::Connected) onConnected(nowMs);
    return;
  }
  if (lost && !up && (attempting_ || state_ == NetState::Connected)) {
    char why[24];
    snprintf(why, sizeof(why), "reason %u", static_cast<unsigned>(sta.failReason));
    onAttemptFailed(nowMs, why, sta.fail);
  }
}

void NetService::tick(uint32_t nowMs) {
  if (!net_) return;
  nowMs_ = nowMs;
  net_->poll();

  const uint32_t events = takeEvents();
  if (events) handleEvents(events, nowMs);

  if (attempting_ && elapsed(nowMs, attemptStartMs_) > static_cast<int32_t>(kAttemptTimeoutMs)) {
    net_->staDisconnect();
    onAttemptFailed(nowMs, "no answer", hal::StaFail::Other);
  }

  if (join_ == JoinPhase::Trying && reached(nowMs, joinDeadlineMs_)) {
    joinFailed(nowMs, JoinResult::TimedOut);
  }

  if (scanState_ == hal::ScanState::Running &&
      elapsed(nowMs, scanAtMs_) > static_cast<int32_t>(kScanTimeoutMs)) {
    scanState_ = hal::ScanState::Failed;
    net_->scanClear();
    LMP_LOGW(src_, "scan: no answer");
    if (scanRef_) kernel_->events().publish(commandDone(scanRef_, Status::Timeout));
    scanRef_ = 0;
  }

  if (probeArmed_ && !probing_ && state_ == NetState::Connected && reached(nowMs, probeAtMs_)) {
    probe(nowMs);
  }

  if (retryArmed_ && !attempting_ && scanState_ != hal::ScanState::Running &&
      reached(nowMs, retryAtMs_)) {
    connect(ssid_, password_, nowMs);
  }

  if (apDownArmed_ && reached(nowMs, apDownAtMs_)) {
    apDownArmed_ = false;
    if (apUp_ && state_ == NetState::Connected) stopAp();
  }

#if LMP_FEATURE_MDNS
  if (nearBack_) collectNear(nowMs);
  if (nearArmed_ && state_ == NetState::Connected && nearState_ != hal::ScanState::Running &&
      reached(nowMs, nearDueMs_)) {
    nearArmed_ = false;
    searchNear(nowMs);
  }
#endif

  guardNetwork(nowMs);
}

void NetService::guardNetwork(uint32_t nowMs) {
  if (LMP_NET_GUARD_MIN == 0) return;
  // Lost: on the network by choice, a network configured, and not on it or without Internet
  // (a radio that stopped answering can look connected). A change of network is not a loss.
  const bool wanted = kernel_->mode() == Mode::Lan && !ssid_.empty() && join_ == JoinPhase::None;
  const bool lost = wanted && (state_ != NetState::Connected || (internetKnown_ && !internet_));
  if (!lost) {
    lostArmed_ = false;
    return;
  }
  // Who is on the access point is using the device: a restart would throw them out. The time
  // counts from when the last one leaves.
  if (apUp_) {
    hal::ApInfo ap;
    net_->apInfo(ap);
    if (ap.clients > 0) lostArmed_ = false;
  }
  if (!lostArmed_) {
    lostArmed_ = true;
    lostSinceMs_ = nowMs;
    return;
  }
  // After a restart for this same reason the loss is likely out of the device, the router or
  // the line: it waits longer, not to restart every quarter of an hour.
  static constexpr const char* kWhy = "network lost";
  const char* previous = kernel_->previousWhy();
  const size_t prefix = strlen(Kernel::kGuardPrefix);
  const bool again = strncmp(previous, Kernel::kGuardPrefix, prefix) == 0 &&
                     strcmp(previous + prefix, kWhy) == 0;
  const uint32_t minutes = again ? LMP_NET_GUARD_AGAIN_MIN : LMP_NET_GUARD_MIN;
  if (elapsed(nowMs, lostSinceMs_) < static_cast<int32_t>(minutes * 60000u)) return;
  LMP_LOGE(src_, "network lost for %lu min", static_cast<unsigned long>(minutes));
  lostArmed_ = false;
  kernel_->guardRestart(kWhy);
}

void NetService::onEvent(const Event& event) {
  if (!net_) return;
  if (event.id == EventId::ModeChanged) {
    // During a change of network the mode follows the outcome: nothing to do here.
    if (join_ == JoinPhase::None) applyMode(kernel_->now());
  } else if (event.id == EventId::ConfigChanged && event.a == Config::kSys) {
    if (namesUp_) {
      startNames();  // the host name may be new
      announce();    // and the name of the device with it
    }
  } else if (event.id == EventId::ServiceState &&
             event.a == static_cast<uint8_t>(ServiceId::Modules) &&
             event.b == static_cast<uint8_t>(ServiceState::Ready)) {
    announce();      // the modules are known now
  }
}

// ---------------------------------------------------------------- change of network

Status NetService::beginJoin(const char* ssid, const char* password, bool test, CmdContext& ctx) {
  if (!validPassword(password)) {
    ctx.errorField = "psw";
    return Status::Invalid;
  }
  if (join_ != JoinPhase::None || scanState_ == hal::ScanState::Running) return Status::Busy;
  // One radio, one client: a test takes the client away from what it is doing. It is allowed
  // only while the access point is what keeps the device reachable.
  if (test && state_ != NetState::ApOnly && state_ != NetState::Fallback) {
    ctx.errorField = "ssid";
    return Status::Denied;
  }

  const Config& config = kernel_->config();
  const uint32_t nowMs = kernel_->now();
  previousMode_ = kernel_->mode();
  previousState_ = state_;
  if (previousMode_ == Mode::Lan) {
    previousSsid_ = config.net.staSsid;
    previousPassword_ = config.net.staPsw;
  } else {
    previousSsid_.clear();
    previousPassword_.clear();
  }

  dropClient();
  if (state_ != NetState::ApOnly && state_ != NetState::Fallback) state_ = NetState::Connecting;
  failures_ = 0;
  joinIsTest_ = test;
  joinFailures_ = 0;
  joinRef_ = ctx.ref;
  joinSsid_.assign(ssid);
  lastJoin_ = JoinResult::None;
  join_ = JoinPhase::Trying;
  const uint32_t limitMs =
      test ? kTestTimeoutMs : static_cast<uint32_t>(config.net.rollbackSec) * 1000u;
  joinDeadlineMs_ = nowMs + limitMs;

  LMP_LOGI(src_, "%s '%s', %lu s to get an address", test ? "test of" : "change to", ssid,
           static_cast<unsigned long>(limitMs / 1000));
  Password trial;
  trial.assign(password);
  connect(joinSsid_, trial, nowMs);

  if (ctx.result) {
    ctx.result->str("ssid", ssid);
    ctx.result->u32("timeoutSec", limitMs / 1000);
  }
  return Status::Pending;
}

void NetService::joinAccepted(uint32_t nowMs) {
  const uint8_t kind = joinIsTest_ ? 1 : 0;
  if (joinIsTest_) {
    lastJoin_ = JoinResult::Tested;
    join_ = JoinPhase::None;
    LMP_LOGI(src_, "test of '%s': accepted", joinSsid_.c_str());
    kernel_->events().publish(Event(EventId::NetJoinResult, kind, 1, joinRef_));
    if (joinRef_) kernel_->events().publish(commandDone(joinRef_, Status::Ok));

    // Nothing is kept: the client leaves and the device is where it was.
    net_->staDisconnect();
    ssid_ = previousSsid_;
    password_ = previousPassword_;
    state_ = previousState_ == NetState::Fallback ? NetState::Fallback : NetState::ApOnly;
    if (state_ == NetState::Fallback) scheduleRetry(nowMs);
    return;
  }

  Config& config = kernel_->config();
  config.net.prevSsid = previousSsid_;
  config.net.prevPsw = previousPassword_;
  config.net.staSsid = ssid_;
  config.net.staPsw = password_;
  config.sys.mode = 2;
  config.touch(Config::kNet);
  config.touch(Config::kSys);
  // Written now: a power loss after this point must find the new network.
  const Status saved = config.save();
  if (saved != Status::Ok) LMP_LOGE(src_, "network not saved: %s", statusName(saved));

  lastJoin_ = JoinResult::Joined;
  join_ = JoinPhase::None;
  LMP_LOGI(src_, "change to '%s': accepted", joinSsid_.c_str());
  kernel_->events().publish(Event(EventId::NetJoinResult, kind, 1, joinRef_));
  if (joinRef_) kernel_->events().publish(commandDone(joinRef_, Status::Ok));
  kernel_->events().publish(Event(EventId::ConfigChanged, Config::kNet));
  if (kernel_->mode() != Mode::Lan) kernel_->setMode(Mode::Lan);
}

void NetService::joinFailed(uint32_t nowMs, JoinResult why) {
  lastJoin_ = why;
  LMP_LOGW(src_, "%s '%s': %s", joinIsTest_ ? "test of" : "change to", joinSsid_.c_str(),
           joinResultName(why));
  kernel_->events().publish(Event(EventId::NetJoinResult, joinIsTest_ ? 1 : 0, 0, joinRef_));
  if (joinRef_) {
    const Status outcome = why == JoinResult::NotFound
                               ? Status::NotFound
                               : (why == JoinResult::TimedOut ? Status::Timeout : Status::Denied);
    kernel_->events().publish(commandDone(joinRef_, outcome));
  }

  net_->staDisconnect();
  attempting_ = false;
  retryArmed_ = false;
  failures_ = 0;
  ssid_ = previousSsid_;
  password_ = previousPassword_;

  const bool hadNetwork = previousMode_ == Mode::Lan && !previousSsid_.empty();
  if (!hadNetwork) {
    join_ = JoinPhase::None;
    state_ = NetState::ApOnly;
    if (!apUp_) startAp();
    return;
  }
  if (previousState_ == NetState::Fallback) {
    join_ = JoinPhase::None;
    state_ = NetState::Fallback;
    scheduleRetry(nowMs);
    return;
  }
  join_ = JoinPhase::Reverting;
  state_ = NetState::Connecting;
  LMP_LOGI(src_, "going back to '%s'", ssid_.c_str());
  connect(ssid_, password_, nowMs);
}

// ---------------------------------------------------------------- scan

void NetService::collectScan(uint32_t nowMs) {
  scanState_ = net_->scanState() == hal::ScanState::Failed ? hal::ScanState::Failed
                                                           : hal::ScanState::Done;
  scanCount_ = 0;
  const size_t found = net_->scanCount();
  for (size_t i = 0; i < found; ++i) {
    hal::WifiNetwork n;
    if (!net_->scanResult(i, n) || n.ssid[0] == '\0') continue;

    // A network with several access points is listed once, with its best signal.
    size_t same = scanCount_;
    for (size_t k = 0; k < scanCount_; ++k) {
      if (strcmp(scan_[k].ssid, n.ssid) == 0) same = k;
    }
    if (same < scanCount_) {
      if (n.rssi > scan_[same].rssi) scan_[same] = n;
      continue;
    }
    if (scanCount_ < LMP_NET_SCAN_MAX) {
      scan_[scanCount_++] = n;
      continue;
    }
    size_t weakest = 0;
    for (size_t k = 1; k < scanCount_; ++k) {
      if (scanRank(scan_[k]) < scanRank(scan_[weakest])) weakest = k;
    }
    if (scanRank(n) > scanRank(scan_[weakest])) scan_[weakest] = n;
  }

  // Strongest first.
  for (size_t i = 1; i < scanCount_; ++i) {
    const hal::WifiNetwork moving = scan_[i];
    size_t k = i;
    while (k > 0 && scan_[k - 1].rssi < moving.rssi) {
      scan_[k] = scan_[k - 1];
      --k;
    }
    scan_[k] = moving;
  }
  net_->scanClear();
  scanAtMs_ = nowMs;
  LMP_LOGI(src_, "scan: %u networks", static_cast<unsigned>(scanCount_));
  if (scanRef_) {
    kernel_->events().publish(commandDone(
        scanRef_, scanState_ == hal::ScanState::Done ? Status::Ok : Status::Io));
  }
  scanRef_ = 0;
}

// ---------------------------------------------------------------- the others of the system

// What the device says of itself to who looks for the devices of the system.
void NetService::announce() {
#if LMP_FEATURE_MDNS
  if (!namesUp_) return;
  const Config& config = kernel_->config();
  if (!selfId_[0] && kernel_->board().sys) {
    hal::Mac mac;
    kernel_->board().sys->mac(mac);
    mac.toString(selfId_, sizeof(selfId_));
  }
  char modules[sizeof(hal::NetPeer::modules)] = "";
  // What the device is, for who reads a list of devices: its first module, the one its
  // console opens on.
  const char* title = "";
  const char* icon = "";
#if LMP_FEATURE_MODULES
  if (IModulesService* hosted = kernel_->api<IModulesService>()) {
    size_t used = 0;
    const char* name = nullptr;
    for (size_t i = 0; (name = hosted->nameAt(i)) != nullptr; ++i) {
      const int n = snprintf(modules + used, sizeof(modules) - used, "%s%s", used ? "," : "", name);
      if (n < 0 || static_cast<size_t>(n) >= sizeof(modules) - used) {
        modules[used] = '\0';   // a name that does not fit is left out whole
        break;
      }
      used += static_cast<size_t>(n);
    }
    const char* first = hosted->nameAt(0);
    if (const sdk::Module* module = first ? hosted->find(first) : nullptr) {
      title = module->manifest().title ? module->manifest().title : "";
      icon = module->manifest().icon ? module->manifest().icon : "";
    }
  }
#endif
  char api[4];
  snprintf(api, sizeof(api), "%u", static_cast<unsigned>(LMP_API_VERSION));
  const char* code = LMP_DEVICE_CODE;
  // Nothing empty is announced: who reads takes what is missing as not said.
  hal::NetText texts[10];
  size_t n = 0;
  texts[n++] = {"api", api};
  texts[n++] = {"id", selfId_};
  if (!config.sys.deviceName.empty()) texts[n++] = {"name", config.sys.deviceName.c_str()};
  if (*code) texts[n++] = {"code", code};
  if (modules[0]) texts[n++] = {"mod", modules};
  if (*title) texts[n++] = {"title", title};
  if (*icon) texts[n++] = {"icon", icon};
  texts[n++] = {"fw", LMP_VERSION};
  texts[n++] = {"board", kernel_->board().name};
  texts[n++] = {"ed", LMP_LIGHT ? "light" : "full"};
  const Status s = net_->mdnsAnnounce(namesHost_.c_str(), LMP_HTTP_PORT, texts, n);
  if (s != Status::Ok && s != Status::Unsupported) {
    LMP_LOGW(src_, "the device is not announced on the network: %s", statusName(s));
  }
#endif
}

// Asks the network who else is of the system. The answers take a couple of seconds: the
// question waits for them in the worker, and collectNear() takes what it found.
Status NetService::searchNear(uint32_t nowMs) {
#if LMP_FEATURE_MDNS
  if (!namesUp_ || state_ != NetState::Connected) return Status::Denied;
  if (nearState_ == hal::ScanState::Running) return Status::Busy;
  if (!nearFound_) {
    nearFound_ = static_cast<hal::NetPeer*>(hal::allocLarge(sizeof(hal::NetPeer) * kNearAsked));
  }
  if (!nearFound_) return Status::NoMem;
  nearBack_ = false;
  const Status posted = kernel_->worker().post([this] {
    size_t found = 0;
    nearResult_ = net_->peersFind(kNearSearchMs, nearFound_, kNearAsked, found);
    nearFoundCount_ = found;
    nearBack_ = true;
  });
  if (posted != Status::Ok) return posted;
  nearState_ = hal::ScanState::Running;
  nearAtMs_ = nowMs;
  return Status::Ok;
#else
  (void)nowMs;
  return Status::Unsupported;
#endif
}

void NetService::collectNear(uint32_t nowMs) {
#if LMP_FEATURE_MDNS
  nearBack_ = false;
  const bool ok = nearResult_ == Status::Ok;
  bool changed = false;
  size_t kept = 0;
  if (ok) {
    // Out the device itself and who answered twice; then by id, so that two searches that
    // found the same devices give the same list.
    for (size_t i = 0; i < nearFoundCount_; ++i) {
      const hal::NetPeer peer = nearFound_[i];
      const bool self = peer.id[0] ? strcmp(peer.id, selfId_) == 0
                                   : strcmp(peer.host, namesHost_.c_str()) == 0;
      bool twice = false;
      for (size_t k = 0; k < kept; ++k) twice = twice || samePeer(nearFound_[k], peer);
      if (self || twice || kept >= LMP_NET_NEAR_MAX) continue;
      size_t at = kept++;
      while (at > 0 && peerOrder(peer, nearFound_[at - 1]) < 0) {
        nearFound_[at] = nearFound_[at - 1];
        --at;
      }
      nearFound_[at] = peer;
    }
    changed = kept != nearCount_;
    for (size_t i = 0; i < kept && !changed; ++i) changed = !samePeerData(nearFound_[i], near_[i]);
    for (size_t i = 0; i < kept; ++i) near_[i] = nearFound_[i];
    nearCount_ = kept;
  }
  hal::freeLarge(nearFound_);
  nearFound_ = nullptr;
  nearState_ = ok ? hal::ScanState::Done : hal::ScanState::Failed;
  nearAtMs_ = nowMs;
  nearArmed_ = true;
  nearDueMs_ = nowMs + static_cast<uint32_t>(LMP_NET_NEAR_EVERY_MIN) * 60000u;
  if (!ok) {
    LMP_LOGW(src_, "search of the other devices: %s", statusName(nearResult_));
  } else if (changed) {
    LMP_LOGI(src_, "%u other devices of the system on this network", static_cast<unsigned>(kept));
  }
  if (nearRef_) kernel_->events().publish(commandDone(nearRef_, ok ? Status::Ok : Status::Io));
  nearRef_ = 0;
  if (changed) {
    kernel_->events().publish(Event(EventId::NetNear, 0, static_cast<uint8_t>(kept)));
  }
#else
  (void)nowMs;
#endif
}

// ---------------------------------------------------------------- Internet

void NetService::armProbe(uint32_t atMs) {
  probeArmed_ = !kernel_->config().net.probeUrl.empty();
  probeAtMs_ = atMs;
}

void NetService::internetIs(bool up) {
  if (internet_ == up) return;
  internet_ = up;
  if (up) {
    LMP_LOGI(src_, "Internet reachable");
    kernel_->events().publish(Event(EventId::InternetUp));
  } else {
    LMP_LOGW(src_, "Internet not reachable");
    kernel_->events().publish(Event(EventId::InternetDown));
  }
}

#if LMP_FEATURE_HTTPCLIENT

void NetService::probe(uint32_t nowMs) {
  IHttpClientService* http = kernel_->api<IHttpClientService>();
  probeUrl_ = kernel_->config().net.probeUrl;
  HttpFetch what;
  what.url = probeUrl_.c_str();
  what.timeoutMs = kProbeTimeoutMs;
  const Status asked =
      http ? http->fetch(what, [this](const HttpAnswer& answer) { onProbe(answer); })
           : Status::Unsupported;
  if (asked == Status::Ok) {
    probing_ = true;
    probeArmed_ = false;
  } else if (asked == Status::Unsupported || asked == Status::Invalid) {
    // No service for the requests, or an address that cannot be asked: nothing to wait for.
    probeArmed_ = false;
  } else {
    probeAtMs_ = nowMs + kProbeWaitMs;   // the service is starting, or its queue is full
  }
}

void NetService::onProbe(const HttpAnswer& answer) {
  probing_ = false;
  if (state_ != NetState::Connected) return;   // the network went away in the meantime
  // Any answer of a server on Internet counts, also a redirect.
  const bool up = answer.result == Status::Ok && answer.status >= 200 && answer.status < 400;
  // The first check after a connection says it also when the answer is no.
  if (!internetKnown_ && !up) LMP_LOGW(src_, "Internet not reachable");
  internetKnown_ = true;
  internetIs(up);
  armProbe(kernel_->now() +
           1000u * static_cast<uint32_t>(up ? LMP_NET_PROBE_SEC : LMP_NET_PROBE_RETRY_SEC));
}

#else

void NetService::probe(uint32_t) { probeArmed_ = false; }
void NetService::onProbe(const HttpAnswer&) {}

#endif

// ---------------------------------------------------------------- documents and commands

bool NetService::usingDefaultApPassword() const {
  const Config& config = kernel_->config();
  for (const FieldDesc& field : config.fields(Config::kNet)) {
    if (strcmp(field.key, "apPsw") == 0) {
      return field.spec.defStr && config.net.apPsw == field.spec.defStr;
    }
  }
  return false;
}

void NetService::describe(JsonWriter& w) const {
  if (!kernel_ || !net_) return;
  const Config& config = kernel_->config();
  const uint32_t nowMs = kernel_->now();
  hal::StaInfo sta;
  hal::ApInfo ap;
  hal::Mac mac;
  net_->staInfo(sta);
  net_->apInfo(ap);
  net_->staMac(mac);
  char text[24];

  w.str("mode", modeName(kernel_->mode()));
  w.str("state", netStateName(state_));

  w.beginObject("sta");
  w.boolean("connected", state_ == NetState::Connected);
  w.str("ssid", state_ == NetState::ApOnly ? "" : ssid_.c_str());
  if (state_ == NetState::Connected) {
    writeIp(w, "ip", sta.ip);
    writeIp(w, "gateway", sta.gateway);
    writeIp(w, "mask", sta.mask);
    writeIp(w, "dns", sta.dns);
    w.i32("rssi", sta.rssi);
    w.u32("channel", sta.channel);
  }
  mac.toString(text, sizeof(text));
  w.str("mac", text);
  w.u32("failures", failures_);
  w.endObject();

  w.beginObject("ap");
  w.boolean("up", apUp_);
  w.str("ssid", config.net.apSsid.c_str());
  writeIp(w, "ip", kApAddress);
  w.u32("clients", apUp_ ? ap.clients : 0);
  if (apUp_) w.u32("channel", ap.channel);
  w.boolean("captive", captiveUp_);
  // The console warns while the password is the one every device is born with.
  w.boolean("pswDefault", usingDefaultApPassword());
  w.endObject();

#if LMP_FEATURE_HTTPCLIENT
  w.boolean("internet", internet_);
  w.boolean("internetChecked", internetKnown_);
#endif
  w.boolean("mdns", namesUp_ && net_->mdnsActive());
  FixedString<40> host;
  host.format("%s.local", config.sys.hostname.c_str());
  w.str("host", host.c_str());

  w.beginObject("join");
  w.str("phase", joinPhaseName(join_));
  w.str("last", joinResultName(lastJoin_));
  if (join_ != JoinPhase::None || lastJoin_ != JoinResult::None) {
    w.str("ssid", joinSsid_.c_str());
    w.boolean("test", joinIsTest_);
  }
  if (join_ == JoinPhase::Trying) {
    const int32_t left = -elapsed(nowMs, joinDeadlineMs_);
    w.u32("leftSec", left > 0 ? static_cast<uint32_t>(left) / 1000 : 0);
  }
  w.endObject();

  w.beginObject("scan");
  w.str("state", scanStateName(scanState_));
  if (scanState_ == hal::ScanState::Done) {
    const int32_t age = elapsed(nowMs, scanAtMs_);
    w.u32("ageSec", age > 0 ? static_cast<uint32_t>(age) / 1000 : 0);
  }
  w.beginArray("networks");
  for (size_t i = 0; i < scanCount_; ++i) {
    w.beginObject();
    w.str("ssid", scan_[i].ssid);
    w.i32("rssi", scan_[i].rssi);
    w.u32("channel", scan_[i].channel);
    w.boolean("secured", scan_[i].secured);
    // The access point of another device of the system: one that is not in a network.
    if (systemAp(scan_[i].ssid)) w.boolean("device", true);
    w.endObject();
  }
  w.endArray();
  w.endObject();

#if LMP_FEATURE_MDNS
  // The other devices of the system on this network, as the last search found them.
  w.beginObject("near");
  w.str("state", scanStateName(nearState_));
  if (nearState_ == hal::ScanState::Done) {
    const int32_t age = elapsed(nowMs, nearAtMs_);
    w.u32("ageSec", age > 0 ? static_cast<uint32_t>(age) / 1000 : 0);
  }
  w.beginArray("devices");
  for (size_t i = 0; i < nearCount_; ++i) {
    const hal::NetPeer& peer = near_[i];
    w.beginObject();
    w.str("id", peer.id);
    w.str("name", peer.name);
    w.str("host", peer.host);
    peer.ip.toString(text, sizeof(text));
    w.str("ip", text);
    if (peer.port != LMP_HTTP_PORT) w.u32("port", peer.port);
    w.u32("api", peer.api);
    w.str("code", peer.code);
    w.str("mod", peer.modules);
    if (peer.title[0]) w.str("title", peer.title);
    if (peer.icon[0]) w.str("icon", peer.icon);
    w.str("fw", peer.fw);
    w.str("board", peer.board);
    w.str("ed", peer.edition);
    w.endObject();
  }
  w.endArray();
  w.endObject();
#endif
}

void NetService::commands(CommandSet& set) {
  set.add("scan",
          LMP_HELP("Looks for the networks around. The list is in the state of the service"),
          [this](const Args&, CmdContext& ctx) -> Status {
            if (!net_) return Status::Unsupported;
            if (scanState_ == hal::ScanState::Running) {
              // Who asks again gets the reference of the scan in progress.
              ctx.ref = scanRef_;
              return Status::Pending;
            }
            if (attempting_ || join_ != JoinPhase::None) return Status::Busy;
            const Status s = net_->scanStart();
            if (s != Status::Ok) return s;
            scanState_ = hal::ScanState::Running;
            scanAtMs_ = kernel_->now();
            scanRef_ = ctx.ref;
            return Status::Pending;
          },
          kCmdAsync);

#if LMP_FEATURE_MDNS
  set.add("near",
          LMP_HELP("Looks for the other LoopMaxPro devices on this network. The list is in the "
                   "state of the service"),
          [this](const Args&, CmdContext& ctx) -> Status {
            if (!net_) return Status::Unsupported;
            if (nearState_ == hal::ScanState::Running) {
              // Who asks again gets the reference of the search in progress.
              if (!nearRef_) nearRef_ = ctx.ref;
              ctx.ref = nearRef_;
              return Status::Pending;
            }
            const Status s = searchNear(kernel_->now());
            if (s != Status::Ok) return s;
            nearRef_ = ctx.ref;
            return Status::Pending;
          },
          kCmdAsync);
#endif

  set.add("test", LMP_HELP("Tries the credentials of a network and keeps nothing"), kJoinArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!net_) return Status::Unsupported;
            return beginJoin(a.str("ssid"), a.str("psw"), true, ctx);
          },
          kCmdAsync);

  set.add("join",
          LMP_HELP("Moves the device to a network. Goes back by itself when it does not work"),
          kJoinArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!net_) return Status::Unsupported;
            return beginJoin(a.str("ssid"), a.str("psw"), false, ctx);
          },
          kCmdConfirm | kCmdAsync);

  set.add("setAp",
          LMP_HELP("Name and password of the access point, from the next restart"), kApArgs,
          [this](const Args& a, CmdContext&) -> Status {
            if (a.count() == 0) return Status::Invalid;
            Config& config = kernel_->config();
            if (a.has("ssid")) config.net.apSsid.assign(a.str("ssid"));
            if (a.has("psw")) config.net.apPsw.assign(a.str("psw"));
            config.touch(Config::kNet);
            kernel_->events().publish(Event(EventId::ConfigChanged, Config::kNet));
            return Status::Ok;
          });

  set.add("setMode", LMP_HELP("AP: access point. LAN: client of the configured network"), kModeArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (join_ != JoinPhase::None) return Status::Busy;
            const char* name = a.str("mode");
            Mode mode;
            if (strcmp(name, modeName(Mode::Ap)) == 0) {
              mode = Mode::Ap;
            } else if (strcmp(name, modeName(Mode::Lan)) == 0) {
              mode = Mode::Lan;
            } else {
              ctx.errorField = "mode";
              return Status::Invalid;
            }
            Config& config = kernel_->config();
            if (mode == Mode::Lan && config.net.staSsid.empty()) {
              // No network was ever accepted: join gives one.
              ctx.errorField = "mode";
              return Status::Denied;
            }
            config.sys.mode = mode == Mode::Lan ? 2 : 1;
            config.touch(Config::kSys);
            kernel_->setMode(mode);
            return Status::Ok;
          },
          kCmdConfirm);
}

}  // namespace lmp

#endif  // LMP_FEATURE_NET
