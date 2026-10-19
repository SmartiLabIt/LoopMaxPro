#include "hal/esp32/esp32_eth.h"

#if LMP_FEATURE_NET && ((LMP_NET_LINKS) & LMP_NET_LINK_ETH)

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_eth_mac_w5500.h>
#include <esp_eth_phy_w5500.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <soc/soc_caps.h>
#include <string.h>

#if ESP_IDF_VERSION_MAJOR < 6
#error "the cable (LMP_NET_LINK_ETH) needs an ESP-IDF build, version 6 or later"
#endif
#if LMP_ETH_PIN_SCK < 0 || LMP_ETH_PIN_MOSI < 0 || LMP_ETH_PIN_MISO < 0 || LMP_ETH_PIN_CS < 0
#error "LMP_NET_LINK_ETH needs LMP_ETH_PIN_SCK, LMP_ETH_PIN_MOSI, LMP_ETH_PIN_MISO and LMP_ETH_PIN_CS"
#endif
#if SOC_SPI_PERIPH_NUM < 3
#error "the cable needs a second SPI bus for itself: this chip has one only"
#endif

namespace lmp::hal::esp32 {
namespace {

// The first bus is the one of the system (the LoRa radio among the others): the cable has the
// second one, and the two never wait for each other.
constexpr spi_host_device_t kHost = SPI3_HOST;

Ip4 fromEsp(const esp_ip4_addr_t& a) {
  return Ip4::of(esp_ip4_addr1_16(&a), esp_ip4_addr2_16(&a), esp_ip4_addr3_16(&a),
                 esp_ip4_addr4_16(&a));
}

// Taken, or already there: both are fine.
bool fine(esp_err_t e) { return e == ESP_OK || e == ESP_ERR_INVALID_STATE; }

}  // namespace

Status Eth::init(const char* hostname) {
  if (missing_) return Status::NotFound;
  if (!ready_) {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!lock_) return Status::NoMem;
    if (!fine(esp_netif_init()) || !fine(esp_event_loop_create_default())) return Status::Io;
    if (LMP_ETH_PIN_INT >= 0 && !fine(gpio_install_isr_service(0))) return Status::Io;

    spi_bus_config_t bus = {};
    bus.mosi_io_num = LMP_ETH_PIN_MOSI;
    bus.miso_io_num = LMP_ETH_PIN_MISO;
    bus.sclk_io_num = LMP_ETH_PIN_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    if (spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return Status::Io;
    bus_ = true;

    spi_device_interface_config_t device = {};
    device.mode = 0;
    device.clock_speed_hz = LMP_ETH_SPI_MHZ * 1000 * 1000;
    device.queue_size = 20;
    device.spics_io_num = LMP_ETH_PIN_CS;
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(kHost, &device);
    w5500.int_gpio_num = LMP_ETH_PIN_INT;
    if (LMP_ETH_PIN_INT < 0) w5500.poll_period_ms = LMP_ETH_POLL_MS;

    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    macConfig.rx_task_stack_size = 4096;
    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    // The default is a pin of its own: here only the one of the build, or none.
    phyConfig.reset_gpio_num = LMP_ETH_PIN_RST;
    macDriver_ = esp_eth_mac_new_w5500(&w5500, &macConfig);
    phy_ = esp_eth_phy_new_w5500(&phyConfig);
    if (!macDriver_ || !phy_) {
      release();
      return Status::NoMem;
    }
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(macDriver_, phy_);
    // The driver resets the chip and reads its version: a chip that does not answer stops here.
    if (esp_eth_driver_install(&config, &eth_) != ESP_OK) {
      release();
      missing_ = true;
      info_.state = LinkState::Missing;
      return Status::NotFound;
    }

    // The chip has no address of its own: the one the ESP32 keeps for a cable.
    uint8_t address[6];
    if (esp_read_mac(address, ESP_MAC_ETH) == ESP_OK) {
      esp_eth_ioctl(eth_, ETH_CMD_S_MAC_ADDR, address);
      memcpy(mac_.b, address, sizeof(address));
    }

    esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
    netif_ = esp_netif_new(&netifConfig);
    glue_ = netif_ ? esp_eth_new_netif_glue(eth_) : nullptr;
    if (!netif_ || !glue_ || esp_netif_attach(netif_, glue_) != ESP_OK) return Status::NoMem;
    esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &Eth::onEvent, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &Eth::onEvent, this,
                                        nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &Eth::onEvent, this,
                                        nullptr);
    ready_ = true;
  }
  if (hostname && *hostname) esp_netif_set_hostname(netif_, hostname);
  return Status::Ok;
}

void Eth::release() {
  if (eth_) esp_eth_driver_uninstall(eth_);
  eth_ = nullptr;
  if (macDriver_) macDriver_->del(macDriver_);
  if (phy_) phy_->del(phy_);
  macDriver_ = nullptr;
  phy_ = nullptr;
  if (bus_) spi_bus_free(kHost);
  bus_ = false;
}

Status Eth::start() {
  if (missing_) return Status::NotFound;
  if (!ready_) return Status::Denied;
  if (running_) return Status::Ok;
  if (esp_eth_start(eth_) != ESP_OK) return Status::Io;
  running_ = true;
  xSemaphoreTake(lock_, portMAX_DELAY);
  info_ = LinkInfo();
  info_.state = LinkState::Down;
  xSemaphoreGive(lock_);
  return Status::Ok;
}

Status Eth::stop() {
  if (!ready_ || !running_) return Status::Ok;
  esp_eth_stop(eth_);
  running_ = false;
  xSemaphoreTake(lock_, portMAX_DELAY);
  info_ = LinkInfo();
  xSemaphoreGive(lock_);
  return Status::Ok;
}

void Eth::info(LinkInfo& out) const {
  if (!lock_) {
    out = info_;
    return;
  }
  xSemaphoreTake(lock_, portMAX_DELAY);
  out = info_;
  xSemaphoreGive(lock_);
}

void Eth::emit(NetEvent event) const {
  if (handler_) handler_(event);
}

void Eth::onEvent(void* self, esp_event_base_t base, int32_t id, void* data) {
  static_cast<Eth*>(self)->handle(base, id, data);
}

void Eth::handle(esp_event_base_t base, int32_t id, void* data) {
  if (base == ETH_EVENT) {
    if (id == ETHERNET_EVENT_CONNECTED) {
      eth_speed_t speed = ETH_SPEED_10M;
      eth_duplex_t duplex = ETH_DUPLEX_HALF;
      esp_eth_ioctl(eth_, ETH_CMD_G_SPEED, &speed);
      esp_eth_ioctl(eth_, ETH_CMD_G_DUPLEX_MODE, &duplex);
      xSemaphoreTake(lock_, portMAX_DELAY);
      if (info_.state != LinkState::Connected) info_.state = LinkState::Up;
      info_.speedMbps = speed == ETH_SPEED_100M ? 100 : 10;
      info_.fullDuplex = duplex == ETH_DUPLEX_FULL;
      xSemaphoreGive(lock_);
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
      xSemaphoreTake(lock_, portMAX_DELAY);
      const bool had = info_.state == LinkState::Connected;
      info_ = LinkInfo();
      info_.state = running_ ? LinkState::Down : LinkState::Off;
      xSemaphoreGive(lock_);
      if (had) emit(NetEvent::LinkLost);
    }
    return;
  }
  if (base != IP_EVENT) return;
  if (id == IP_EVENT_ETH_GOT_IP) {
    const ip_event_got_ip_t* got = static_cast<const ip_event_got_ip_t*>(data);
    esp_netif_dns_info_t dns = {};
    esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_MAIN, &dns);
    xSemaphoreTake(lock_, portMAX_DELAY);
    info_.state = LinkState::Connected;
    info_.ip = fromEsp(got->ip_info.ip);
    info_.mask = fromEsp(got->ip_info.netmask);
    info_.gateway = fromEsp(got->ip_info.gw);
    info_.dns = fromEsp(dns.ip.u_addr.ip4);
    xSemaphoreGive(lock_);
    emit(NetEvent::LinkGotIp);
  } else if (id == IP_EVENT_ETH_LOST_IP) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool had = info_.state == LinkState::Connected;
    if (had) info_.state = LinkState::Up;
    info_.ip = info_.gateway = info_.mask = info_.dns = Ip4();
    xSemaphoreGive(lock_);
    if (had) emit(NetEvent::LinkLost);
  }
}

}  // namespace lmp::hal::esp32

#endif
