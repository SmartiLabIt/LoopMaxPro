#include "hal/esp32/esp32_spi.h"

#include <freertos/FreeRTOS.h>
#include <string.h>

namespace lmp::hal::esp32 {
namespace {

constexpr spi_host_device_t kHost = SPI2_HOST;
// Without DMA the driver moves this many bytes for each transaction.
constexpr size_t kChunk = 64;

}  // namespace

Status SpiBus::begin(uint8_t sck, uint8_t miso, uint8_t mosi) {
  if (ready_) return Status::Ok;
  spi_bus_config_t bus = {};
  bus.sclk_io_num = sck;
  bus.miso_io_num = miso == kSpiNoPin ? -1 : miso;
  bus.mosi_io_num = mosi;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  if (spi_bus_initialize(kHost, &bus, SPI_DMA_DISABLED) != ESP_OK) return Status::Io;
  ready_ = true;
  return Status::Ok;
}

void SpiBus::end() {
  if (!ready_) return;
  for (Profile& p : profiles_) {
    if (p.handle) spi_bus_remove_device(p.handle);
    p = Profile();
  }
  spi_bus_free(kHost);
  ready_ = false;
}

spi_device_handle_t SpiBus::profileFor(const SpiConfig& cfg) {
  Profile* free = nullptr;
  for (Profile& p : profiles_) {
    if (!p.handle) {
      if (!free) free = &p;
      continue;
    }
    if (p.frequencyHz == cfg.frequencyHz && p.mode == cfg.mode && p.msbFirst == cfg.msbFirst) {
      return p.handle;
    }
  }
  if (!free) return nullptr;

  spi_device_interface_config_t dev = {};
  dev.mode = cfg.mode;
  dev.clock_speed_hz = static_cast<int>(cfg.frequencyHz);
  dev.spics_io_num = -1;  // the chip select is driven here, around the whole transfer
  dev.queue_size = 1;
  dev.flags = cfg.msbFirst ? 0 : SPI_DEVICE_BIT_LSBFIRST;
  if (spi_bus_add_device(kHost, &dev, &free->handle) != ESP_OK) {
    free->handle = nullptr;
    return nullptr;
  }
  free->frequencyHz = cfg.frequencyHz;
  free->mode = cfg.mode;
  free->msbFirst = cfg.msbFirst;
  return free->handle;
}

Status SpiBus::transfer(uint8_t cs, const SpiConfig& cfg, const uint8_t* tx, uint8_t* rx,
                        size_t len) {
  if (!ready_) return Status::Denied;
  if (len == 0 || (!tx && !rx) || cfg.mode > 3 || cfg.frequencyHz == 0) return Status::Invalid;
  const spi_device_handle_t device = profileFor(cfg);
  if (!device) return Status::NoMem;

  if (spi_device_acquire_bus(device, portMAX_DELAY) != ESP_OK) return Status::Busy;
  if (cs != kSpiNoCs) gpio_.write(cs, PinLevel::Low);

  static const uint8_t kZeros[kChunk] = {0};
  Status result = Status::Ok;
  for (size_t done = 0; done < len;) {
    const size_t n = len - done < kChunk ? len - done : kChunk;
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = tx ? tx + done : kZeros;
    t.rx_buffer = rx ? rx + done : nullptr;
    if (spi_device_polling_transmit(device, &t) != ESP_OK) {
      result = Status::Io;
      break;
    }
    done += n;
  }

  if (cs != kSpiNoCs) gpio_.write(cs, PinLevel::High);
  spi_device_release_bus(device);
  return result;
}

}  // namespace lmp::hal::esp32
