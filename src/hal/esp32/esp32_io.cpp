// LoopMaxPro HAL - ESP32 family, both frameworks: console, pins, PWM, ADC.
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_adc/adc_cali_scheme.h>
#endif

#include "hal/esp32/esp32_board.h"
#include "lmp_config.h"

#if LMP_ESP32_CONSOLE == 1
#include <driver/usb_serial_jtag.h>
#endif

namespace lmp::hal::esp32 {

// ---------------------------------------------------------------- serial

namespace {

constexpr uart_port_t kConsole = UART_NUM_0;
constexpr int kRxBuffer = 1024;
constexpr int kTxBuffer = LMP_SERIAL_TX_BUFFER;
constexpr uint32_t kFlushMs = 200;

}  // namespace

Status Serial0::begin(uint32_t baud) {
  if (ready_) return Status::Ok;
  uart_config_t cfg = {};
  cfg.baud_rate = static_cast<int>(baud);
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#if ESP_IDF_VERSION_MAJOR >= 5
  cfg.source_clk = UART_SCLK_DEFAULT;
#else
  cfg.source_clk = UART_SCLK_APB;
#endif
  // The pins are the ones the chip already uses for its boot messages.
  if (uart_driver_install(kConsole, kRxBuffer, kTxBuffer, 0, nullptr, 0) != ESP_OK) {
    return Status::Io;
  }
  if (uart_param_config(kConsole, &cfg) != ESP_OK) {
    uart_driver_delete(kConsole);
    return Status::Io;
  }
  ready_ = true;
  return Status::Ok;
}

size_t Serial0::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data || len == 0) return 0;
  LockGuard guard(mutex_);
  const int n = uart_write_bytes(kConsole, data, len);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t Serial0::read(uint8_t* out, size_t cap) {
  if (!ready_ || !out || cap == 0) return 0;
  LockGuard guard(mutex_);
  size_t waiting = 0;
  if (uart_get_buffered_data_len(kConsole, &waiting) != ESP_OK || waiting == 0) return 0;
  const int n = uart_read_bytes(kConsole, out, waiting < cap ? waiting : cap, 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t Serial0::available() const {
  if (!ready_) return 0;
  size_t waiting = 0;
  return uart_get_buffered_data_len(kConsole, &waiting) == ESP_OK ? waiting : 0;
}

void Serial0::flush() {
  if (ready_) uart_wait_tx_done(kConsole, pdMS_TO_TICKS(kFlushMs));
}

// ---------------------------------------------------------------- USB-JTAG console

#if LMP_ESP32_CONSOLE == 1

Status JtagConsole::begin(uint32_t) {
  if (ready_) return Status::Ok;
  usb_serial_jtag_driver_config_t cfg = {};
  cfg.rx_buffer_size = kRxBuffer;
  cfg.tx_buffer_size = kTxBuffer;
  if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return Status::Io;
  ready_ = true;
  return Status::Ok;
}

size_t JtagConsole::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data || len == 0) return 0;
  LockGuard guard(mutex_);
  const int n = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(LMP_SERIAL_TX_TIMEOUT_MS));
  return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t JtagConsole::fill() const {
  if (heldCount_ < sizeof(held_)) {
    const int n = usb_serial_jtag_read_bytes(held_ + heldCount_, sizeof(held_) - heldCount_, 0);
    if (n > 0) heldCount_ += static_cast<size_t>(n);
  }
  return heldCount_;
}

size_t JtagConsole::read(uint8_t* out, size_t cap) {
  if (!ready_ || !out || cap == 0) return 0;
  LockGuard guard(mutex_);
  const size_t n = fill() < cap ? heldCount_ : cap;
  memcpy(out, held_, n);
  memmove(held_, held_ + n, heldCount_ - n);
  heldCount_ -= n;
  return n;
}

size_t JtagConsole::available() const { return ready_ ? fill() : 0; }

#endif
// ---------------------------------------------------------------- gpio

namespace {

PinHandler g_handlers[SOC_GPIO_PIN_COUNT];

void dispatch(void* arg) {
  const uint8_t pin = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
  if (pin < SOC_GPIO_PIN_COUNT && g_handlers[pin]) g_handlers[pin](pin);
}

gpio_num_t gpioOf(uint8_t pin) { return static_cast<gpio_num_t>(pin); }

}  // namespace

bool Gpio::isValid(uint8_t pin) const { return GPIO_IS_VALID_GPIO(pin); }
bool Gpio::canOutput(uint8_t pin) const { return GPIO_IS_VALID_OUTPUT_GPIO(pin); }

Status Gpio::setMode(uint8_t pin, PinMode mode) {
  if (!isValid(pin)) return Status::Invalid;
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.intr_type = GPIO_INTR_DISABLE;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  switch (mode) {
    case PinMode::Input:
      cfg.mode = GPIO_MODE_INPUT;
      break;
    case PinMode::InputPullUp:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_up_en = GPIO_PULLUP_ENABLE;
      break;
    case PinMode::InputPullDown:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
      break;
    // Outputs keep the input stage on: read() returns the level of the pin.
    case PinMode::Output:
      if (!canOutput(pin)) return Status::Unsupported;
      cfg.mode = GPIO_MODE_INPUT_OUTPUT;
      break;
    case PinMode::OutputOpenDrain:
      if (!canOutput(pin)) return Status::Unsupported;
      cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
      break;
    case PinMode::Analog:
      cfg.mode = GPIO_MODE_DISABLE;
      break;
    default:
      return Status::Unsupported;
  }
  return gpio_config(&cfg) == ESP_OK ? Status::Ok : Status::Io;
}

Status Gpio::write(uint8_t pin, PinLevel level) {
  if (!canOutput(pin)) return Status::Invalid;
  gpio_set_level(gpioOf(pin), level == PinLevel::High ? 1 : 0);
  return Status::Ok;
}

Status Gpio::read(uint8_t pin, PinLevel& level) const {
  if (!isValid(pin)) return Status::Invalid;
  level = gpio_get_level(gpioOf(pin)) ? PinLevel::High : PinLevel::Low;
  return Status::Ok;
}

Status Gpio::attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) {
  if (!isValid(pin) || !handler) return Status::Invalid;
  if (!isrReady_) {
    const esp_err_t err = gpio_install_isr_service(0);
    // Invalid state: somebody else installed the service, which is fine.
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return Status::Io;
    isrReady_ = true;
  }
  const gpio_int_type_t type = edge == Edge::Rising
                                   ? GPIO_INTR_POSEDGE
                                   : (edge == Edge::Falling ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
  g_handlers[pin] = handler;
  if (gpio_set_intr_type(gpioOf(pin), type) != ESP_OK ||
      gpio_isr_handler_add(gpioOf(pin), dispatch,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(pin))) != ESP_OK) {
    g_handlers[pin].reset();
    return Status::Io;
  }
  gpio_intr_enable(gpioOf(pin));
  return Status::Ok;
}

Status Gpio::detachInterrupt(uint8_t pin) {
  if (!isValid(pin)) return Status::Invalid;
  gpio_intr_disable(gpioOf(pin));
  if (isrReady_) gpio_isr_handler_remove(gpioOf(pin));
  gpio_set_intr_type(gpioOf(pin), GPIO_INTR_DISABLE);
  g_handlers[pin].reset();
  return Status::Ok;
}

// ---------------------------------------------------------------- PWM

namespace {

// The only speed mode that every chip of the family has.
constexpr ledc_mode_t kPwmMode = LEDC_LOW_SPEED_MODE;

}  // namespace

Pwm::Pwm() = default;

int Pwm::channelOf(uint8_t pin) const {
  for (uint8_t i = 0; i < kChannels; ++i) {
    if (channels_[i].pin == pin) return i;
  }
  return -1;
}

int Pwm::timerFor(uint32_t frequencyHz, uint8_t resolutionBits) const {
  int unused = -1;
  for (uint8_t i = 0; i < kTimers; ++i) {
    const Timer& t = timers_[i];
    if (t.users == 0) {
      if (unused < 0) unused = i;
    } else if (t.frequencyHz == frequencyHz && t.resolutionBits == resolutionBits) {
      return i;
    }
  }
  return unused;
}

bool Pwm::supports(uint8_t pin) const { return GPIO_IS_VALID_OUTPUT_GPIO(pin); }

Status Pwm::setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) {
#if defined(SOC_LEDC_TIMER_BIT_WIDTH)
  constexpr uint8_t kMaxBits = SOC_LEDC_TIMER_BIT_WIDTH;
#else
  constexpr uint8_t kMaxBits = SOC_LEDC_TIMER_BIT_WIDE_NUM;
#endif
  if (!supports(pin) || frequencyHz == 0 || resolutionBits == 0 || resolutionBits > kMaxBits) {
    return Status::Invalid;
  }
  int channel = channelOf(pin);
  const bool known = channel >= 0;
  if (!known) {
    channel = channelOf(kFree);
    if (channel < 0) return Status::NoMem;
  }
  // A pin that changes its settings leaves its timer before looking for the new one.
  if (known) --timers_[channels_[channel].timer].users;

  const int timer = timerFor(frequencyHz, resolutionBits);
  if (timer < 0) {
    if (known) ++timers_[channels_[channel].timer].users;
    return Status::NoMem;
  }

  if (timers_[timer].users == 0) {
    ledc_timer_config_t tc = {};
    tc.speed_mode = kPwmMode;
    tc.duty_resolution = static_cast<ledc_timer_bit_t>(resolutionBits);
    tc.timer_num = static_cast<ledc_timer_t>(timer);
    tc.freq_hz = frequencyHz;
    tc.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&tc) != ESP_OK) {
      if (known) ++timers_[channels_[channel].timer].users;
      return Status::Invalid;  // frequency and resolution cannot be combined
    }
    timers_[timer].frequencyHz = frequencyHz;
    timers_[timer].resolutionBits = resolutionBits;
  }

  ledc_channel_config_t cc = {};
  cc.gpio_num = pin;
  cc.speed_mode = kPwmMode;
  cc.channel = static_cast<ledc_channel_t>(channel);
  cc.timer_sel = static_cast<ledc_timer_t>(timer);
  cc.duty = 0;
  cc.hpoint = 0;
  if (ledc_channel_config(&cc) != ESP_OK) {
    if (known) ++timers_[channels_[channel].timer].users;
    return Status::Io;
  }

  ++timers_[timer].users;
  channels_[channel].pin = pin;
  channels_[channel].timer = static_cast<uint8_t>(timer);
  return Status::Ok;
}

Status Pwm::write(uint8_t pin, uint32_t duty) {
  const int channel = channelOf(pin);
  if (channel < 0) return Status::NotFound;
  const ledc_channel_t ch = static_cast<ledc_channel_t>(channel);
  if (ledc_set_duty(kPwmMode, ch, duty) != ESP_OK) return Status::Invalid;
  return ledc_update_duty(kPwmMode, ch) == ESP_OK ? Status::Ok : Status::Io;
}

Status Pwm::release(uint8_t pin) {
  const int channel = channelOf(pin);
  if (channel < 0) return Status::NotFound;
  ledc_stop(kPwmMode, static_cast<ledc_channel_t>(channel), 0);
  gpio_reset_pin(gpioOf(pin));
  --timers_[channels_[channel].timer].users;
  channels_[channel].pin = kFree;
  return Status::Ok;
}

// ---------------------------------------------------------------- ADC

#if ESP_IDF_VERSION_MAJOR >= 5

uint8_t Adc::resolutionBits() const { return SOC_ADC_RTC_MAX_BITWIDTH; }

namespace {

constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;  // full range of the pin

adc_cali_handle_t createCalibration(adc_channel_t channel) {
  adc_cali_handle_t handle = nullptr;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cfg = {};
  cfg.unit_id = ADC_UNIT_1;
  cfg.chan = channel;
  cfg.atten = kAttenuation;
  cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_curve_fitting(&cfg, &handle) != ESP_OK) handle = nullptr;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  (void)channel;
  adc_cali_line_fitting_config_t cfg = {};
  cfg.unit_id = ADC_UNIT_1;
  cfg.atten = kAttenuation;
  cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_line_fitting(&cfg, &handle) != ESP_OK) handle = nullptr;
#else
  (void)channel;
#endif
  return handle;
}

bool channelOfPin(uint8_t pin, adc_channel_t& channel) {
  adc_unit_t unit;
  return adc_oneshot_io_to_channel(pin, &unit, &channel) == ESP_OK && unit == ADC_UNIT_1;
}

}  // namespace

bool Adc::supports(uint8_t pin) const {
  adc_channel_t channel;
  return channelOfPin(pin, channel) && static_cast<uint8_t>(channel) < kChannels;
}

Status Adc::prepare(uint8_t pin, adc_channel_t& channel) {
  if (!supports(pin) || !channelOfPin(pin, channel)) return Status::Invalid;
  if (!unit_) {
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit, &unit_) != ESP_OK) {
      unit_ = nullptr;
      return Status::Io;
    }
  }
  Channel& c = channels_[channel];
  if (!c.ready) {
    adc_oneshot_chan_cfg_t cfg = {};
    cfg.atten = kAttenuation;
    cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(unit_, channel, &cfg) != ESP_OK) return Status::Io;
    c.calibration = createCalibration(channel);
    c.ready = true;
  }
  return Status::Ok;
}

Status Adc::readRaw(uint8_t pin, uint32_t& value) {
  adc_channel_t channel;
  LMP_TRY(prepare(pin, channel));
  int raw = 0;
  if (adc_oneshot_read(unit_, channel, &raw) != ESP_OK) return Status::Io;
  value = raw > 0 ? static_cast<uint32_t>(raw) : 0;
  return Status::Ok;
}

Status Adc::readMillivolts(uint8_t pin, uint32_t& millivolts) {
  adc_channel_t channel;
  LMP_TRY(prepare(pin, channel));
  // Without the factory calibration of the chip the voltage would be a guess.
  if (!channels_[channel].calibration) return Status::Unsupported;
  int raw = 0;
  int mv = 0;
  if (adc_oneshot_read(unit_, channel, &raw) != ESP_OK) return Status::Io;
  if (adc_cali_raw_to_voltage(channels_[channel].calibration, raw, &mv) != ESP_OK) {
    return Status::Io;
  }
  millivolts = mv > 0 ? static_cast<uint32_t>(mv) : 0;
  return Status::Ok;
}

#else  // ESP-IDF 4.4: the classic driver of the converter

namespace {

constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;   // full range of the pin
constexpr adc_bits_width_t kWidth = static_cast<adc_bits_width_t>(ADC_WIDTH_BIT_DEFAULT);

bool channelOfPin(uint8_t pin, int& channel) {
  for (int c = 0; c < ADC1_CHANNEL_MAX; ++c) {
    gpio_num_t io;
    if (adc1_pad_get_io_num(static_cast<adc1_channel_t>(c), &io) == ESP_OK && io == pin) {
      channel = c;
      return true;
    }
  }
  return false;
}

}  // namespace

uint8_t Adc::resolutionBits() const { return SOC_ADC_MAX_BITWIDTH; }

bool Adc::supports(uint8_t pin) const {
  int channel;
  return channelOfPin(pin, channel);
}

Status Adc::prepare(uint8_t pin, int& channel) {
  if (!channelOfPin(pin, channel)) return Status::Invalid;
  if (!widthSet_) {
    if (adc1_config_width(kWidth) != ESP_OK) return Status::Io;
    widthSet_ = true;
  }
  Channel& c = channels_[channel];
  if (!c.ready) {
    if (adc1_config_channel_atten(static_cast<adc1_channel_t>(channel), kAttenuation) != ESP_OK) {
      return Status::Io;
    }
    esp_adc_cal_characterize(ADC_UNIT_1, kAttenuation, kWidth, 1100,
                             &c.calibration);
    c.ready = true;
  }
  return Status::Ok;
}

Status Adc::readRaw(uint8_t pin, uint32_t& value) {
  int channel;
  LMP_TRY(prepare(pin, channel));
  const int raw = adc1_get_raw(static_cast<adc1_channel_t>(channel));
  if (raw < 0) return Status::Io;
  value = static_cast<uint32_t>(raw);
  return Status::Ok;
}

Status Adc::readMillivolts(uint8_t pin, uint32_t& millivolts) {
  int channel;
  LMP_TRY(prepare(pin, channel));
  const int raw = adc1_get_raw(static_cast<adc1_channel_t>(channel));
  if (raw < 0) return Status::Io;
  millivolts = esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(raw), &channels_[channel].calibration);
  return Status::Ok;
}

#endif

}  // namespace lmp::hal::esp32
