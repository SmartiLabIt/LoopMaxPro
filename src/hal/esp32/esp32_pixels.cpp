// LoopMaxPro HAL - ESP32 family: addressable LEDs (WS2812, "NeoPixel") on the RMT.
// The RMT draws the bits in hardware, from its own memory: what is in the memory goes out with
// the right timing whatever Wi-Fi and interrupts do. The memory of a channel is small (64 bits
// on the ESP32, 48 on the others): a longer frame is loaded half a memory at a time by an
// interrupt while the line is going, and when that comes late the line sends again what was
// there, and the LEDs show other colours. So a line takes the memory its frame needs, when the
// chip has it (see kBlocksMost); who draws sends the same frame again every few seconds, which
// heals what a late interrupt, a spike or a dip of the supply left on the LEDs.
// ESP-IDF 5 and 6 have a new driver with encoders; ESP-IDF 4.4, inside the Arduino core 2.0.x,
// has the old one, fed with the pulses of each bit, one block of memory a line.
//
// WS2812: a bit takes 1.25 us. 0 is 0.4 us high and 0.85 us low, 1 is 0.8 us high and 0.45 us
// low; the colours go green, red, blue, the high bit first. More than 50 us low ends a frame.
#include "hal/esp32/esp32_board.h"

#include <stdlib.h>
#include <string.h>

#include <soc/soc_caps.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#else
#include <driver/rmt.h>
#endif

#include <driver/gpio.h>

namespace lmp::hal::esp32 {

Pixels::Line* Pixels::find(uint8_t pin) {
  for (Line& l : lines_) {
    if (l.pin == pin) return &l;
  }
  return nullptr;
}

bool Pixels::supports(uint8_t pin) const { return GPIO_IS_VALID_OUTPUT_GPIO(pin); }

#if ESP_IDF_VERSION_MAJOR >= 5

namespace {
constexpr uint32_t kTicksPerSecond = 10000000;   // 0.1 us a tick

// A channel can take the memory of the channels after it, which then cannot be used. The
// channels that transmit are 8 on the ESP32, 4 on the S2 and S3, 2 on the others: a line takes
// as many blocks as its frame needs, and leaves one channel that transmits to the other line.
// On the ESP32 a ring of 12 LEDs (289 bits with the one that ends the frame) fits in 5 blocks
// and goes out without any interrupt; a frame of 18 LEDs is the longest that does.
#if CONFIG_IDF_TARGET_ESP32
constexpr size_t kTxChannels = 8;
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
constexpr size_t kTxChannels = 4;
#else
constexpr size_t kTxChannels = 2;
#endif
constexpr size_t kBlocksMost = kTxChannels - (Pixels::kLines - 1);
}  // namespace

Status Pixels::begin(uint8_t pin, uint16_t count) {
  if (!supports(pin) || count == 0) return Status::Invalid;
  if (find(pin)) end(pin);
  Line* line = find(0xFF);
  if (!line) return Status::NoMem;

  rmt_tx_channel_config_t cfg = {};
  cfg.gpio_num = static_cast<gpio_num_t>(pin);
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = kTicksPerSecond;
  cfg.trans_queue_depth = 1;
  // One symbol a bit, and the one that ends the frame. With less memory than that the line
  // works as before, half a memory at a time: the driver is asked for fewer blocks until it
  // gives them.
  const size_t symbols = static_cast<size_t>(count) * 24 + 1;
  size_t blocks = (symbols + SOC_RMT_MEM_WORDS_PER_CHANNEL - 1) / SOC_RMT_MEM_WORDS_PER_CHANNEL;
  if (blocks > kBlocksMost) blocks = kBlocksMost;
  rmt_channel_handle_t channel = nullptr;
  for (; blocks >= 1 && !channel; --blocks) {
    cfg.mem_block_symbols = blocks * SOC_RMT_MEM_WORDS_PER_CHANNEL;
    if (rmt_new_tx_channel(&cfg, &channel) != ESP_OK) channel = nullptr;
  }
  if (!channel) return Status::NoMem;

  rmt_bytes_encoder_config_t enc = {};
  enc.bit0.level0 = 1;
  enc.bit0.duration0 = 4;    // 0.4 us
  enc.bit0.level1 = 0;
  enc.bit0.duration1 = 9;    // 0.9 us
  enc.bit1.level0 = 1;
  enc.bit1.duration0 = 8;
  enc.bit1.level1 = 0;
  enc.bit1.duration1 = 5;
  enc.flags.msb_first = 1;
  rmt_encoder_handle_t encoder = nullptr;
  if (rmt_new_bytes_encoder(&enc, &encoder) != ESP_OK || rmt_enable(channel) != ESP_OK) {
    if (encoder) rmt_del_encoder(encoder);
    rmt_del_channel(channel);
    return Status::Io;
  }
  line->pin = pin;
  line->count = count;
  line->channel = channel;
  line->encoder = encoder;
  return Status::Ok;
}

Status Pixels::show(uint8_t pin, const uint8_t* grb, size_t len) {
  Line* line = find(pin);
  if (!line || !grb) return Status::Invalid;
  if (len > line->count * 3u) len = line->count * 3u;
  rmt_transmit_config_t t = {};
  auto* channel = static_cast<rmt_channel_handle_t>(line->channel);
  if (rmt_transmit(channel, static_cast<rmt_encoder_handle_t>(line->encoder), grb, len, &t) != ESP_OK) {
    return Status::Io;
  }
  return rmt_tx_wait_all_done(channel, 50) == ESP_OK ? Status::Ok : Status::Timeout;
}

Status Pixels::end(uint8_t pin) {
  Line* line = find(pin);
  if (!line) return Status::NotFound;
  auto* channel = static_cast<rmt_channel_handle_t>(line->channel);
  rmt_disable(channel);
  rmt_del_channel(channel);
  rmt_del_encoder(static_cast<rmt_encoder_handle_t>(line->encoder));
  *line = Line();
  return Status::Ok;
}

#else  // ESP-IDF 4.4

namespace {
// 40 MHz: 25 ns a tick.
constexpr uint8_t kClockDiv = 2;
constexpr uint16_t kT0H = 16, kT0L = 34, kT1H = 32, kT1L = 18;
}  // namespace

Status Pixels::begin(uint8_t pin, uint16_t count) {
  if (!supports(pin) || count == 0) return Status::Invalid;
  if (find(pin)) end(pin);
  Line* line = find(0xFF);
  if (!line) return Status::NoMem;
  const rmt_channel_t channel = static_cast<rmt_channel_t>(line - lines_);

  // Written out: the macro of the driver leaves members without a value.
  rmt_config_t cfg = {};
  cfg.rmt_mode = RMT_MODE_TX;
  cfg.channel = channel;
  cfg.gpio_num = static_cast<gpio_num_t>(pin);
  cfg.clk_div = kClockDiv;
  cfg.mem_block_num = 1;
  cfg.tx_config.carrier_en = false;
  cfg.tx_config.loop_en = false;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  if (rmt_config(&cfg) != ESP_OK) return Status::Io;
  if (rmt_driver_install(channel, 0, 0) != ESP_OK) return Status::NoMem;
  void* items = calloc(static_cast<size_t>(count) * 24, sizeof(rmt_item32_t));
  if (!items) {
    rmt_driver_uninstall(channel);
    return Status::NoMem;
  }
  line->pin = pin;
  line->count = count;
  line->items = items;
  return Status::Ok;
}

Status Pixels::show(uint8_t pin, const uint8_t* grb, size_t len) {
  Line* line = find(pin);
  if (!line || !grb) return Status::Invalid;
  if (len > line->count * 3u) len = line->count * 3u;
  auto* items = static_cast<rmt_item32_t*>(line->items);
  size_t n = 0;
  for (size_t i = 0; i < len; ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      const bool one = (grb[i] >> bit) & 1;
      items[n].level0 = 1;
      items[n].duration0 = one ? kT1H : kT0H;
      items[n].level1 = 0;
      items[n].duration1 = one ? kT1L : kT0L;
      ++n;
    }
  }
  const rmt_channel_t channel = static_cast<rmt_channel_t>(line - lines_);
  return rmt_write_items(channel, items, static_cast<int>(n), true) == ESP_OK ? Status::Ok
                                                                              : Status::Io;
}

Status Pixels::end(uint8_t pin) {
  Line* line = find(pin);
  if (!line) return Status::NotFound;
  rmt_driver_uninstall(static_cast<rmt_channel_t>(line - lines_));
  free(line->items);
  *line = Line();
  return Status::Ok;
}

#endif

}  // namespace lmp::hal::esp32
