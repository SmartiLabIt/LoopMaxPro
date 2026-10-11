#include "hal/native/native_net.h"

#include <stdio.h>
#include <string.h>

namespace lmp::hal::native {

void Net::reset() {
  handler_ = NetEventHandler();
  around_.clear();
  found_.clear();
  sta_ = StaInfo();
  ap_ = ApInfo();
  staAddress_ = Ip4::of(192, 168, 1, 50);
  scan_ = ScanState::Idle;
  wantedSsid_.clear();
  wantedPassword_.clear();
  apPassword_.clear();
  hostname_.clear();
  mdns_.clear();
  instance_.clear();
  texts_.clear();
  peers_.clear();
  announces_ = searches_ = 0;
  captive_ = false;
  started_ = false;
  connectDelay_ = scanDelay_ = 3;
  connectLeft_ = scanLeft_ = 0;
  connects_ = 0;
  initResult_ = apResult_ = Status::Ok;
}

void Net::addNetwork(const char* ssid, const char* password, int8_t rssi, uint8_t channel) {
  removeNetwork(ssid);
  around_.push_back(Known{ssid, password ? password : "", rssi, channel});
}

void Net::removeNetwork(const char* ssid) {
  for (size_t i = 0; i < around_.size(); ++i) {
    if (around_[i].ssid == ssid) {
      around_.erase(around_.begin() + static_cast<long>(i));
      return;
    }
  }
}

void Net::setDelays(unsigned connect, unsigned scan) {
  connectDelay_ = connect;
  scanDelay_ = scan;
}

void Net::failNext(Status init, Status ap) {
  initResult_ = init;
  apResult_ = ap;
}

const Net::Known* Net::find(const std::string& ssid) const {
  for (const Known& k : around_) {
    if (k.ssid == ssid) return &k;
  }
  return nullptr;
}

void Net::emit(NetEvent event) {
  if (handler_) handler_(event);
}

Status Net::init(const char* hostname) {
  if (initResult_ != Status::Ok) return initResult_;
  hostname_ = hostname ? hostname : "";
  started_ = true;
  return Status::Ok;
}

void Net::staMac(Mac& out) const {
  static const uint8_t fixed[6] = {0x02, 0x4C, 0x4D, 0x50, 0x00, 0x01};
  memcpy(out.b, fixed, sizeof(fixed));
}

// ---------------------------------------------------------------- client

Status Net::staConnect(const char* ssid, const char* password) {
  if (!started_) return Status::Denied;
  if (!ssid || !*ssid) return Status::Invalid;
  ++connects_;
  wantedSsid_ = ssid;
  wantedPassword_ = password ? password : "";
  sta_ = StaInfo();
  sta_.state = StaState::Connecting;
  snprintf(sta_.ssid, sizeof(sta_.ssid), "%s", ssid);
  connectLeft_ = connectDelay_ ? connectDelay_ : 1;
  return Status::Ok;
}

Status Net::staDisconnect() {
  // Leaving on request raises no event: it is not a loss.
  connectLeft_ = 0;
  sta_ = StaInfo();
  return Status::Ok;
}

void Net::dropSta() {
  if (sta_.state != StaState::Connected) return;
  sta_.state = StaState::Lost;
  sta_.ip = Ip4();
  sta_.failReason = 200;  // beacon timeout
  sta_.fail = StaFail::Other;
  emit(NetEvent::StaLost);
}

// ---------------------------------------------------------------- access point

Status Net::apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) {
  if (!started_) return Status::Denied;
  if (apResult_ != Status::Ok) return apResult_;
  if (!ssid || !*ssid) return Status::Invalid;
  ap_ = ApInfo();
  ap_.up = true;
  snprintf(ap_.ssid, sizeof(ap_.ssid), "%s", ssid);
  ap_.ip = ip;
  ap_.channel = channel;
  apPassword_ = password ? password : "";
  emit(NetEvent::ApStarted);
  return Status::Ok;
}

Status Net::apStop() {
  if (ap_.up) {
    ap_ = ApInfo();
    captive_ = false;
    emit(NetEvent::ApStopped);
  }
  return Status::Ok;
}

void Net::setApClients(uint8_t clients) {
  if (!ap_.up || clients == ap_.clients) return;
  const bool more = clients > ap_.clients;
  ap_.clients = clients;
  emit(more ? NetEvent::ApClientJoined : NetEvent::ApClientLeft);
}

// ---------------------------------------------------------------- scan and names

Status Net::scanStart() {
  if (!started_) return Status::Denied;
  if (scan_ == ScanState::Running) return Status::Busy;
  found_.clear();
  scan_ = ScanState::Running;
  scanLeft_ = scanDelay_ ? scanDelay_ : 1;
  return Status::Ok;
}

bool Net::scanResult(size_t index, WifiNetwork& out) const {
  if (index >= found_.size()) return false;
  out = found_[index];
  return true;
}

void Net::scanClear() {
  found_.clear();
  scanLeft_ = 0;
  scan_ = ScanState::Idle;
}

Status Net::mdnsStart(const char* hostname, uint16_t) {
  if (!started_ || !hostname || !*hostname) return Status::Invalid;
  mdns_ = hostname;
  return Status::Ok;
}

Status Net::mdnsAnnounce(const char* instance, uint16_t, const NetText* texts, size_t count) {
  if (mdns_.empty() || !instance || !*instance) return Status::Denied;
  instance_ = instance;
  texts_.clear();
  for (size_t i = 0; i < count; ++i) {
    if (texts[i].key && texts[i].value) texts_.emplace_back(texts[i].key, texts[i].value);
  }
  ++announces_;
  return Status::Ok;
}

std::string Net::announced(const char* key) const {
  for (const auto& text : texts_) {
    if (text.first == key) return text.second;
  }
  return std::string();
}

// The devices the test put on the network, at once: no waiting on the PC.
Status Net::peersFind(uint32_t, NetPeer* out, size_t cap, size_t& count) {
  count = 0;
  if (mdns_.empty() || !out) return Status::Denied;
  ++searches_;
  for (const NetPeer& peer : peers_) {
    if (count < cap) out[count++] = peer;
  }
  return Status::Ok;
}

Status Net::captiveStart(Ip4) {
  if (!ap_.up) return Status::Denied;
  captive_ = true;
  return Status::Ok;
}

// ---------------------------------------------------------------- time passes

void Net::poll() {
  if (scanLeft_ && --scanLeft_ == 0) {
    for (const Known& k : around_) {
      WifiNetwork n;
      snprintf(n.ssid, sizeof(n.ssid), "%s", k.ssid.c_str());
      n.rssi = k.rssi;
      n.channel = k.channel;
      n.secured = !k.password.empty();
      found_.push_back(n);
    }
    scan_ = ScanState::Done;
    emit(NetEvent::ScanDone);
  }

  if (connectLeft_ && --connectLeft_ == 0) {
    const Known* k = find(wantedSsid_);
    if (k && k->password == wantedPassword_) {
      sta_.state = StaState::Connected;
      sta_.ip = staAddress_;
      sta_.gateway = Ip4::of(staAddress_.b[0], staAddress_.b[1], staAddress_.b[2], 1);
      sta_.mask = Ip4::of(255, 255, 255, 0);
      sta_.dns = sta_.gateway;
      sta_.rssi = k->rssi;
      sta_.channel = k->channel;
      // With one radio the access point follows the channel of the client.
      if (ap_.up) ap_.channel = k->channel;
      emit(NetEvent::StaConnected);
      emit(NetEvent::StaGotIp);
    } else {
      sta_.state = StaState::Failed;
      sta_.failReason = k ? kWrongPassword : kNotFound;
      sta_.fail = k ? StaFail::WrongPassword : StaFail::NotFound;
      emit(NetEvent::StaFailed);
    }
  }
}

}  // namespace lmp::hal::native
