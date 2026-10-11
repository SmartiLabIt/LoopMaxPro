#include "hal/esp32/esp32_update.h"

#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#include <string.h>

#include "hal/esp32/esp32_paths.h"

#if LMP_FEATURE_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif

#if LMP_FEATURE_BUS
// The driver of the older framework is the only one there; the newer framework has a new
// one and warns against the old.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#include <driver/i2c_master.h>
#define LMP_I2C_NEW_DRIVER 1
#else
#include <driver/i2c.h>
#define LMP_I2C_NEW_DRIVER 0
#endif
#endif

namespace lmp::hal::esp32 {

// ---------------------------------------------------------------- firmware update

#if LMP_FEATURE_OTA

namespace {

constexpr size_t kSector = 4096;

const esp_partition_t* partitionOf(const void* p) { return static_cast<const esp_partition_t*>(p); }

}  // namespace

Status Ota::begin(OtaTarget target, size_t totalSize) {
  if (inProgress_) return Status::Busy;

  const esp_partition_t* partition = nullptr;
  if (target == OtaTarget::App) {
    partition = esp_ota_get_next_update_partition(nullptr);
  } else {
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                         kFsPartition);
  }
  if (!partition) return Status::Unsupported;
  if (totalSize > partition->size) return Status::NoMem;

  if (target == OtaTarget::App) {
    // Erased while it is written: erasing the whole space first would hold who asks for
    // several seconds.
    esp_ota_handle_t handle = 0;
    const esp_err_t e = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (e != ESP_OK) return e == ESP_ERR_NO_MEM ? Status::NoMem : Status::Io;
    handle_ = handle;
  } else {
    // From here to the restart the files are not reachable.
    if (fs_) fs_->unmount();
    erased_ = 0;
  }
  partition_ = partition;
  target_ = target;
  written_ = 0;
  inProgress_ = true;
  return Status::Ok;
}

Status Ota::write(const uint8_t* data, size_t len) {
  if (!inProgress_) return Status::Denied;
  if (len == 0) return Status::Ok;
  if (!data) return Status::Invalid;
  const esp_partition_t* partition = partitionOf(partition_);
  if (written_ + len > partition->size) return Status::NoMem;

  if (target_ == OtaTarget::App) {
    if (esp_ota_write(handle_, data, len) != ESP_OK) return Status::Io;
  } else {
    while (erased_ < written_ + len) {
      if (esp_partition_erase_range(partition, erased_, kSector) != ESP_OK) return Status::Io;
      erased_ += kSector;
    }
    if (esp_partition_write(partition, written_, data, len) != ESP_OK) return Status::Io;
  }
  written_ += len;
  return Status::Ok;
}

Status Ota::end() {
  if (!inProgress_) return Status::Denied;
  inProgress_ = false;
  if (written_ == 0) {
    if (target_ == OtaTarget::App) esp_ota_abort(handle_);
    return Status::Invalid;
  }
  if (target_ == OtaTarget::Filesystem) return Status::Ok;

  // The framework checks that the image is a firmware for this chip, whole.
  const esp_err_t e = esp_ota_end(handle_);
  if (e != ESP_OK) return e == ESP_ERR_OTA_VALIDATE_FAILED ? Status::Invalid : Status::Io;
  if (esp_ota_set_boot_partition(partitionOf(partition_)) != ESP_OK) return Status::Io;
  return Status::Ok;
}

void Ota::abort() {
  if (!inProgress_) return;
  inProgress_ = false;
  if (target_ == OtaTarget::App) esp_ota_abort(handle_);
  // A filesystem written by halves stays as it is: the next start makes a new one.
}

bool Ota::pendingVerify() const {
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

Status Ota::markValid() {
  if (!pendingVerify()) return Status::Ok;
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? Status::Ok : Status::Io;
}

bool Ota::canRollback() const { return esp_ota_check_rollback_is_possible(); }

Status Ota::rollback() {
  if (!canRollback()) return Status::Denied;
  // The other copy is chosen for the next start: the restart is asked by who called.
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (!other) return Status::Unsupported;
  const esp_err_t e = esp_ota_set_boot_partition(other);
  if (e == ESP_OK) return Status::Ok;
  return e == ESP_ERR_OTA_VALIDATE_FAILED ? Status::Invalid : Status::Io;
}

#endif  // LMP_FEATURE_OTA

// ---------------------------------------------------------------- I2C

#if LMP_FEATURE_BUS

namespace {

constexpr int kTimeoutMs = 50;

Status statusOf(esp_err_t e) {
  switch (e) {
    case ESP_OK:                return Status::Ok;
    case ESP_ERR_TIMEOUT:       return Status::Timeout;
    case ESP_ERR_INVALID_ARG:   return Status::Invalid;
    case ESP_ERR_NO_MEM:        return Status::NoMem;
    case ESP_ERR_NOT_FOUND:     return Status::NotFound;
    default:                    break;
  }
  // No acknowledge: nobody at that address.
  return Status::NotFound;
}

}  // namespace

#if LMP_I2C_NEW_DRIVER

Status I2c::begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) {
  if (up_) end();
  i2c_master_bus_config_t config;
  memset(&config, 0, sizeof(config));
  config.i2c_port = static_cast<i2c_port_num_t>(port_);
  config.sda_io_num = static_cast<gpio_num_t>(sda);
  config.scl_io_num = static_cast<gpio_num_t>(scl);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  const esp_err_t e = i2c_new_master_bus(&config, &bus);
  if (e != ESP_OK) return e == ESP_ERR_INVALID_ARG ? Status::Invalid : Status::Io;
  bus_ = bus;
  frequency_ = frequencyHz;
  up_ = true;
  return Status::Ok;
}

void I2c::end() {
  if (!up_) return;
  for (size_t i = 0; i < kDevices; ++i) {
    if (devices_[i]) i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(devices_[i]));
    devices_[i] = nullptr;
    addresses_[i] = 0;
  }
  i2c_del_master_bus(static_cast<i2c_master_bus_handle_t>(bus_));
  bus_ = nullptr;
  up_ = false;
}

void* I2c::device(uint8_t address) {
  for (size_t i = 0; i < kDevices; ++i) {
    if (devices_[i] && addresses_[i] == address) return devices_[i];
  }
  // The place of the oldest one is taken.
  const size_t slot = next_;
  next_ = static_cast<uint8_t>((next_ + 1) % kDevices);
  if (devices_[slot]) {
    i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(devices_[slot]));
    devices_[slot] = nullptr;
  }
  i2c_device_config_t config;
  memset(&config, 0, sizeof(config));
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = frequency_;
  i2c_master_dev_handle_t handle = nullptr;
  if (i2c_master_bus_add_device(static_cast<i2c_master_bus_handle_t>(bus_), &config, &handle) !=
      ESP_OK) {
    return nullptr;
  }
  devices_[slot] = handle;
  addresses_[slot] = address;
  return handle;
}

bool I2c::probe(uint8_t address) {
  return up_ && i2c_master_probe(static_cast<i2c_master_bus_handle_t>(bus_), address,
                                 kTimeoutMs) == ESP_OK;
}

Status I2c::write(uint8_t address, const uint8_t* data, size_t len, bool) {
  if (!up_) return Status::Denied;
  if (len == 0) return probe(address) ? Status::Ok : Status::NotFound;
  if (!data) return Status::Invalid;
  void* dev = device(address);
  if (!dev) return Status::NoMem;
  return statusOf(
      i2c_master_transmit(static_cast<i2c_master_dev_handle_t>(dev), data, len, kTimeoutMs));
}

Status I2c::read(uint8_t address, uint8_t* out, size_t len) {
  if (!up_) return Status::Denied;
  if (!out || len == 0) return Status::Invalid;
  void* dev = device(address);
  if (!dev) return Status::NoMem;
  return statusOf(
      i2c_master_receive(static_cast<i2c_master_dev_handle_t>(dev), out, len, kTimeoutMs));
}

Status I2c::writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxLen) {
  if (!up_) return Status::Denied;
  if (!tx || txLen == 0 || !rx || rxLen == 0) return Status::Invalid;
  void* dev = device(address);
  if (!dev) return Status::NoMem;
  return statusOf(i2c_master_transmit_receive(static_cast<i2c_master_dev_handle_t>(dev), tx,
                                              txLen, rx, rxLen, kTimeoutMs));
}

#else  // the driver of the older framework

Status I2c::begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) {
  if (up_) end();
  if (port_ >= SOC_I2C_NUM) return Status::Unsupported;
  i2c_config_t config;
  memset(&config, 0, sizeof(config));
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = sda;
  config.scl_io_num = scl;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = frequencyHz;
  const i2c_port_t port = static_cast<i2c_port_t>(port_);
  if (i2c_param_config(port, &config) != ESP_OK) return Status::Invalid;
  if (i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return Status::Io;
  frequency_ = frequencyHz;
  up_ = true;
  return Status::Ok;
}

void I2c::end() {
  if (!up_) return;
  i2c_driver_delete(static_cast<i2c_port_t>(port_));
  up_ = false;
}

void* I2c::device(uint8_t) { return nullptr; }

bool I2c::probe(uint8_t address) {
  if (!up_) return false;
  // The address alone: a chip that is there acknowledges it.
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd) return false;
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, static_cast<uint8_t>(address << 1), true);
  i2c_master_stop(cmd);
  const esp_err_t e =
      i2c_master_cmd_begin(static_cast<i2c_port_t>(port_), cmd, pdMS_TO_TICKS(kTimeoutMs));
  i2c_cmd_link_delete(cmd);
  return e == ESP_OK;
}

Status I2c::write(uint8_t address, const uint8_t* data, size_t len, bool) {
  if (!up_) return Status::Denied;
  if (len == 0) return probe(address) ? Status::Ok : Status::NotFound;
  if (!data) return Status::Invalid;
  return statusOf(i2c_master_write_to_device(static_cast<i2c_port_t>(port_), address, data, len,
                                             pdMS_TO_TICKS(kTimeoutMs)));
}

Status I2c::read(uint8_t address, uint8_t* out, size_t len) {
  if (!up_) return Status::Denied;
  if (!out || len == 0) return Status::Invalid;
  return statusOf(i2c_master_read_from_device(static_cast<i2c_port_t>(port_), address, out, len,
                                              pdMS_TO_TICKS(kTimeoutMs)));
}

Status I2c::writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxLen) {
  if (!up_) return Status::Denied;
  if (!tx || txLen == 0 || !rx || rxLen == 0) return Status::Invalid;
  return statusOf(i2c_master_write_read_device(static_cast<i2c_port_t>(port_), address, tx, txLen,
                                               rx, rxLen, pdMS_TO_TICKS(kTimeoutMs)));
}

#endif  // LMP_I2C_NEW_DRIVER

#endif  // LMP_FEATURE_BUS

}  // namespace lmp::hal::esp32
