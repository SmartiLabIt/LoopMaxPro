// LoopMaxPro HAL - ESP32 family: Wi-Fi.
// Written with the drivers of the chip maker, which are part of both the Arduino and the
// ESP-IDF builds: the two implementations of the HAL share this file.
// The credentials are never given to the driver to keep: they belong to the settings.
#pragma once
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "hal/api/inet.h"
#include "lmp_config.h"

// 1 lets the radio sleep between the beacons of the network: less current, slower answers.
#ifndef LMP_NET_POWER_SAVE
#define LMP_NET_POWER_SAVE 0
#endif

// Wi-Fi Easy Connect where the driver was built with it: the ESP-IDF builds, which turn it on in
// sdkconfig.defaults. The libraries of the Arduino core are built without it.
#if defined(CONFIG_ESP_WIFI_DPP_SUPPORT) && CONFIG_ESP_WIFI_DPP_SUPPORT
#define LMP_NET_DPP 1
#else
#define LMP_NET_DPP 0
#endif

namespace lmp::hal::esp32 {

class Net : public INet {
 public:
  Status init(const char* hostname) override;
  void setEventHandler(NetEventHandler handler) override { handler_ = handler; }
  void poll() override;

  Status staConnect(const char* ssid, const char* password) override;
  Status staDisconnect() override;
  void staInfo(StaInfo& out) const override;
  void staMac(Mac& out) const override;

  Status apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) override;
  Status apStop() override;
  void apInfo(ApInfo& out) const override;
  Status apMarks(const WifiMark* marks, size_t count) override;

  Status scanStart() override;
  ScanState scanState() const override { return scan_; }
  size_t scanCount() const override { return found_; }
  bool scanResult(size_t index, WifiNetwork& out) const override;
  void scanClear() override;
  void scanWatch(const char* tag) override;

#if LMP_NET_DPP
  bool dppAvailable() const override { return true; }
  Status dppListen(uint8_t channel) override;
  void dppStop() override;
  DppState dppState() const override { return dpp_; }
  bool dppUri(char* out, size_t cap) const override;
  bool dppTake(char* ssid, size_t ssidCap, char* password, size_t passwordCap) override;
#endif

  Status mdnsStart(const char* hostname, uint16_t httpPort) override;
  void mdnsStop() override;
  bool mdnsActive() const override { return mdns_; }
  Status mdnsAnnounce(const char* instance, uint16_t port, const NetText* texts,
                      size_t count) override;
  Status peersFind(uint32_t timeoutMs, NetPeer* out, size_t cap, size_t& count) override;
  Status captiveStart(Ip4 address) override;
  void captiveStop() override { captive_ = false; }
  bool captiveActive() const override { return captive_; }

 private:
  // Networks read from the driver after a scan, before the service picks the best ones.
  static constexpr size_t kRecords = LMP_NET_SCAN_MAX + 8;

  // An element of the maker as the driver takes it: id, length, oui, type, data.
  static constexpr size_t kMarkBytes = 6 + kWifiMarkMax;

  static void onEvent(void* self, esp_event_base_t base, int32_t id, void* data);
  static void onVendorElement(void* self, wifi_vendor_ie_type_t type, const uint8_t sa[6],
                              const vendor_ie_data_t* element, int rssi);
  static void dnsTask(void* self);
  void handle(esp_event_base_t base, int32_t id, void* data);
  void emit(NetEvent event) const;
  // Station, access point or both, as they are wanted now.
  Status applyMode();
  // Gives the driver the marks kept here.
  void putMarks();
  void answerDns();

  NetEventHandler handler_;
  SemaphoreHandle_t lock_ = nullptr;
  esp_netif_t* staNetif_ = nullptr;
  esp_netif_t* apNetif_ = nullptr;
  bool ready_ = false;
  bool running_ = false;
  bool wantSta_ = false;
  bool wantAp_ = false;
  volatile bool scanning_ = false;
  volatile bool modeStale_ = false;

  StaInfo sta_;
  ApInfo ap_;
  volatile ScanState scan_ = ScanState::Idle;
  WifiNetwork records_[kRecords];
  volatile size_t found_ = 0;

  // The marks of the access point, ready for the driver.
  uint8_t marks_[2][kMarkBytes];
  size_t markCount_ = 0;
  // What marks the networks of the system in a scan, and who was heard with it during the scan.
  char watch_[5] = "";
  uint8_t heard_[kRecords][6];
  volatile size_t heardCount_ = 0;

#if LMP_NET_DPP
  bool wantDpp_ = false;            // the station runs for Easy Connect
  bool dppReady_ = false;           // the supplicant is initialized
  bool dppKeyed_ = false;           // and has its key: the address is there
  bool dppListenLater_ = false;     // asked before the key or the station were there
  uint8_t dppChannel_ = 0;          // the channel of the key: a new channel, a new key
  volatile DppState dpp_ = DppState::Off;
  char dppUri_[200] = "";
  char dppSsid_[33] = "";
  char dppPassword_[65] = "";
  bool dppHave_ = false;
#endif

  bool mdns_ = false;
  bool announced_ = false;
  volatile bool captive_ = false;
  Ip4 captiveAddress_;
  TaskHandle_t dnsTask_ = nullptr;
};

}  // namespace lmp::hal::esp32
