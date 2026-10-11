// LoopMaxPro HAL for the PC - a network that exists only in memory.
// It behaves as a driver does: requests return at once, the outcome arrives later as an event,
// after a number of calls to poll(). The tests decide which networks are around.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "hal/api/inet.h"

namespace lmp::hal::native {

class Net : public INet {
 public:
  // Reasons of a failure, with the numbers the ESP32 drivers use.
  static constexpr uint8_t kWrongPassword = 15;
  static constexpr uint8_t kNotFound = 201;

  Status init(const char* hostname) override;
  void setEventHandler(NetEventHandler handler) override { handler_ = handler; }
  void poll() override;

  Status staConnect(const char* ssid, const char* password) override;
  Status staDisconnect() override;
  void staInfo(StaInfo& out) const override { out = sta_; }
  void staMac(Mac& out) const override;

  Status apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) override;
  Status apStop() override;
  void apInfo(ApInfo& out) const override { out = ap_; }

  Status scanStart() override;
  ScanState scanState() const override { return scan_; }
  size_t scanCount() const override { return found_.size(); }
  bool scanResult(size_t index, WifiNetwork& out) const override;
  void scanClear() override;

  Status mdnsStart(const char* hostname, uint16_t httpPort) override;
  void mdnsStop() override {
    mdns_.clear();
    instance_.clear();
    texts_.clear();
  }
  bool mdnsActive() const override { return !mdns_.empty(); }
  Status mdnsAnnounce(const char* instance, uint16_t port, const NetText* texts,
                      size_t count) override;
  Status peersFind(uint32_t timeoutMs, NetPeer* out, size_t cap, size_t& count) override;
  Status captiveStart(Ip4 address) override;
  void captiveStop() override { captive_ = false; }
  bool captiveActive() const override { return captive_; }

  // ---- hooks for the tests
  // Back to a radio just switched on, with no network around.
  void reset();
  void addNetwork(const char* ssid, const char* password, int8_t rssi = -60, uint8_t channel = 6);
  void removeNetwork(const char* ssid);
  // Calls to poll() between a request and its outcome.
  void setDelays(unsigned connect, unsigned scan);
  // The network the client is on goes away.
  void dropSta();
  void setApClients(uint8_t clients);
  void failNext(Status init, Status ap);
  // Address the networks around give to the client. The gateway is the first of its network.
  void setStaAddress(Ip4 address) { staAddress_ = address; }

  unsigned connects() const { return connects_; }
  const std::string& apPassword() const { return apPassword_; }
  const std::string& mdnsName() const { return mdns_; }
  // The service of the system as it was announced last: its instance, and one of its data
  // (empty when not there). How many times it was announced.
  const std::string& announcedAs() const { return instance_; }
  std::string announced(const char* key) const;
  unsigned announces() const { return announces_; }
  // The other devices of the system the network has: what a search finds.
  void addPeer(const NetPeer& peer) { peers_.push_back(peer); }
  void clearPeers() { peers_.clear(); }
  unsigned searches() const { return searches_; }
  const std::string& hostname() const { return hostname_; }

 private:
  struct Known {
    std::string ssid;
    std::string password;
    int8_t rssi;
    uint8_t channel;
  };

  const Known* find(const std::string& ssid) const;
  void emit(NetEvent event);

  NetEventHandler handler_;
  std::vector<Known> around_;
  std::vector<WifiNetwork> found_;
  StaInfo sta_;
  ApInfo ap_;
  Ip4 staAddress_ = Ip4::of(192, 168, 1, 50);
  ScanState scan_ = ScanState::Idle;
  std::string wantedSsid_;
  std::string wantedPassword_;
  std::string apPassword_;
  std::string hostname_;
  std::string mdns_;
  std::string instance_;
  std::vector<std::pair<std::string, std::string>> texts_;
  std::vector<NetPeer> peers_;
  unsigned announces_ = 0;
  unsigned searches_ = 0;
  bool captive_ = false;
  bool started_ = false;
  unsigned connectDelay_ = 3;
  unsigned scanDelay_ = 3;
  unsigned connectLeft_ = 0;
  unsigned scanLeft_ = 0;
  unsigned connects_ = 0;
  Status initResult_ = Status::Ok;
  Status apResult_ = Status::Ok;
};

}  // namespace lmp::hal::native
