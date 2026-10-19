// LoopMaxPro HAL - ESP32 family: the cable, a W5500 on SPI (LMP_NET_LINKS with LMP_NET_LINK_ETH).
// ESP-IDF builds only, from version 6: the chip comes with the driver of the chip maker, a
// component of its own since that version.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_NET && ((LMP_NET_LINKS) & LMP_NET_LINK_ETH)

#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "hal/api/ilink.h"

namespace lmp::hal::esp32 {

class Eth : public ILink {
 public:
  LinkKind kind() const override { return LinkKind::Ethernet; }
  Status init(const char* hostname) override;
  void setEventHandler(NetEventHandler handler) override { handler_ = handler; }
  void poll() override {}
  Status start() override;
  Status stop() override;
  void info(LinkInfo& out) const override;
  void mac(Mac& out) const override { out = mac_; }

 private:
  static void onEvent(void* self, esp_event_base_t base, int32_t id, void* data);
  void handle(esp_event_base_t base, int32_t id, void* data);
  void emit(NetEvent event) const;
  // Back to nothing after a chip that did not answer.
  void release();

  NetEventHandler handler_;
  SemaphoreHandle_t lock_ = nullptr;
  esp_eth_handle_t eth_ = nullptr;
  esp_eth_mac_t* macDriver_ = nullptr;
  esp_eth_phy_t* phy_ = nullptr;
  esp_netif_t* netif_ = nullptr;
  esp_eth_netif_glue_handle_t glue_ = nullptr;
  bool bus_ = false;
  bool ready_ = false;
  bool missing_ = false;
  bool running_ = false;
  LinkInfo info_;
  Mac mac_;
};

}  // namespace lmp::hal::esp32

#endif
