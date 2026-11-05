#include "services/bus/bus_service.h"

#if LMP_FEATURE_BUS

#include <stdio.h>
#include <string.h>

#include "services/gpio/gpio_service.h"

// Pins of the first bus when the board has chips on it, such as a display. -1: none.
#ifndef LMP_I2C0_SDA
#define LMP_I2C0_SDA -1
#endif
#ifndef LMP_I2C0_SCL
#define LMP_I2C0_SCL -1
#endif
#ifndef LMP_I2C0_HZ
#define LMP_I2C0_HZ 100000
#endif

namespace lmp {
namespace {

// Addresses asked for each round of the system.
constexpr uint8_t kScanStep = 8;
constexpr const char* kConsole = "console";
constexpr const char* kBoard = "board";
constexpr const char* const kPinOwners[] = {"i2c0", "i2c1", "i2c2", "i2c3"};
constexpr const char* kSpiOwner = "spi";

constexpr ArgSpec kOpenArgs[] = {
    Arg::u8("bus", 0, LMP_HAL_I2C_MAX - 1).optional(),
    Arg::u8("sda", 0, 63),
    Arg::u8("scl", 0, 63),
    Arg::u32("hz", 10000, 1000000).optional(),
};

constexpr ArgSpec kBusArgs[] = {
    Arg::u8("bus", 0, LMP_HAL_I2C_MAX - 1).optional(),
};

}  // namespace

const ServiceInfo& BusService::info() const {
  static const ServiceInfo kInfo{ServiceId::Bus, "bus", "\xF0\x9F\x94\x8C", Mode::Any,
                                 RestartPolicy::Retry, 20,
                                 DepList(ServiceId::Gpio, ServiceId::Log)};
  return kInfo;
}

Status BusService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  pins_ = kernel.api<IGpio>();
  const hal::Board& board = kernel.board();

  count_ = 0;
  for (size_t i = 0; i < LMP_HAL_I2C_MAX; ++i) {
    buses_[i] = Bus();
    buses_[i].hal = board.i2c[i];
    buses_[i].pinOwner = kPinOwners[i < 4 ? i : 3];
    if (board.i2c[i]) count_ = i + 1;
  }
  scan_ = Scan::Idle;
  foundCount_ = 0;
  spi_ = board.spi;
  spiOpen_ = false;
  spiUserCount_ = 0;

  if (LMP_I2C0_SDA >= 0 && LMP_I2C0_SCL >= 0) {
    const Status s = openI2c(0, static_cast<uint8_t>(LMP_I2C0_SDA),
                             static_cast<uint8_t>(LMP_I2C0_SCL), LMP_I2C0_HZ, kBoard);
    if (s != Status::Ok) degrade("the bus of the board does not open");
  }
  return Status::Ok;
}

void BusService::stop() {
  if (scan_ == Scan::Running) endScan(Status::Denied);
  for (size_t i = 0; i < LMP_HAL_I2C_MAX; ++i) {
    Bus& b = buses_[i];
    if (!b.open) continue;
    b.hal->end();
    if (pins_) {
      pins_->release(b.sda, b.pinOwner);
      pins_->release(b.scl, b.pinOwner);
    }
    b.open = false;
    b.userCount = 0;
  }
  if (spiOpen_) {
    spi_->end();
    releaseSpiPins();
    spiOpen_ = false;
    spiUserCount_ = 0;
  }
}

// ---------------------------------------------------------------- I2C

hal::II2c* BusService::i2c(uint8_t bus) {
  return bus < LMP_HAL_I2C_MAX && buses_[bus].open ? buses_[bus].hal : nullptr;
}

Status BusService::openI2c(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t hz,
                           const char* owner) {
  if (bus >= LMP_HAL_I2C_MAX || !owner || !*owner || sda == scl || hz == 0) {
    return Status::Invalid;
  }
  Bus& b = buses_[bus];
  if (!b.hal) return Status::Unsupported;

  if (b.open) {
    if (b.sda != sda || b.scl != scl) return Status::Busy;
    for (uint8_t i = 0; i < b.userCount; ++i) {
      if (strcmp(b.users[i], owner) == 0) return Status::Ok;   // already in
    }
    if (b.userCount >= kUsers) return Status::NoMem;
    b.users[b.userCount++] = owner;
    return Status::Ok;
  }

  if (pins_) {
    LMP_TRY(pins_->claim(sda, hal::PinMode::OutputOpenDrain, b.pinOwner, "SDA"));
    const Status second = pins_->claim(scl, hal::PinMode::OutputOpenDrain, b.pinOwner, "SCL");
    if (second != Status::Ok) {
      pins_->release(sda, b.pinOwner);
      return second;
    }
  }
  const Status s = b.hal->begin(sda, scl, hz);
  if (s != Status::Ok) {
    if (pins_) {
      pins_->release(sda, b.pinOwner);
      pins_->release(scl, b.pinOwner);
    }
    LMP_LOGE(src_, "I2C %u on pins %u and %u not opened: %s", static_cast<unsigned>(bus),
             static_cast<unsigned>(sda), static_cast<unsigned>(scl), statusName(s));
    return s;
  }
  b.open = true;
  b.sda = sda;
  b.scl = scl;
  b.hz = hz;
  b.users[0] = owner;
  b.userCount = 1;
  LMP_LOGI(src_, "I2C %u on pins %u and %u, %lu Hz, for %s", static_cast<unsigned>(bus),
           static_cast<unsigned>(sda), static_cast<unsigned>(scl),
           static_cast<unsigned long>(hz), owner);
  return Status::Ok;
}

Status BusService::closeI2c(uint8_t bus, const char* owner) {
  if (bus >= LMP_HAL_I2C_MAX || !owner) return Status::Invalid;
  Bus& b = buses_[bus];
  if (!b.open) return Status::NotFound;
  uint8_t at = b.userCount;
  for (uint8_t i = 0; i < b.userCount; ++i) {
    if (strcmp(b.users[i], owner) == 0) at = i;
  }
  if (at == b.userCount) return Status::Denied;   // it never opened this bus
  for (uint8_t i = at; i + 1 < b.userCount; ++i) b.users[i] = b.users[i + 1];
  --b.userCount;
  if (b.userCount) return Status::Ok;

  if (scan_ == Scan::Running && scanBus_ == bus) endScan(Status::Denied);
  b.hal->end();
  if (pins_) {
    pins_->release(b.sda, b.pinOwner);
    pins_->release(b.scl, b.pinOwner);
  }
  b.open = false;
  LMP_LOGI(src_, "I2C %u closed", static_cast<unsigned>(bus));
  return Status::Ok;
}

// ---------------------------------------------------------------- SPI

void BusService::releaseSpiPins() {
  if (!pins_) return;
  pins_->release(spiSck_, kSpiOwner);
  pins_->release(spiMosi_, kSpiOwner);
  if (spiMiso_ >= 0) pins_->release(static_cast<uint8_t>(spiMiso_), kSpiOwner);
}

Status BusService::openSpi(uint8_t sck, int16_t miso, uint8_t mosi, const char* owner) {
  if (!owner || !*owner || sck == mosi || miso == sck || miso == mosi) return Status::Invalid;
  if (!spi_) return Status::Unsupported;

  if (spiOpen_) {
    if (spiSck_ != sck || spiMosi_ != mosi || spiMiso_ != miso) return Status::Busy;
    for (uint8_t i = 0; i < spiUserCount_; ++i) {
      if (strcmp(spiUsers_[i], owner) == 0) return Status::Ok;   // already in
    }
    if (spiUserCount_ >= kUsers) return Status::NoMem;
    spiUsers_[spiUserCount_++] = owner;
    return Status::Ok;
  }

  spiSck_ = sck;
  spiMosi_ = mosi;
  spiMiso_ = miso;
  if (pins_) {
    Status s = pins_->claim(sck, hal::PinMode::Output, kSpiOwner, "SCK");
    if (s == Status::Ok) {
      s = pins_->claim(mosi, hal::PinMode::Output, kSpiOwner, "MOSI");
      if (s != Status::Ok) pins_->release(sck, kSpiOwner);
    }
    if (s == Status::Ok && miso >= 0) {
      s = pins_->claim(static_cast<uint8_t>(miso), hal::PinMode::Input, kSpiOwner, "MISO");
      if (s != Status::Ok) {
        pins_->release(sck, kSpiOwner);
        pins_->release(mosi, kSpiOwner);
      }
    }
    if (s != Status::Ok) return s;
  }
  const Status s = spi_->begin(sck, miso < 0 ? hal::kSpiNoPin : static_cast<uint8_t>(miso), mosi);
  if (s != Status::Ok) {
    releaseSpiPins();
    LMP_LOGE(src_, "SPI on pins %u and %u not opened: %s", static_cast<unsigned>(sck),
             static_cast<unsigned>(mosi), statusName(s));
    return s;
  }
  spiOpen_ = true;
  spiUsers_[0] = owner;
  spiUserCount_ = 1;
  LMP_LOGI(src_, "SPI on pins %u (clock) and %u (data), for %s", static_cast<unsigned>(sck),
           static_cast<unsigned>(mosi), owner);
  return Status::Ok;
}

Status BusService::closeSpi(const char* owner) {
  if (!owner) return Status::Invalid;
  if (!spiOpen_) return Status::NotFound;
  uint8_t at = spiUserCount_;
  for (uint8_t i = 0; i < spiUserCount_; ++i) {
    if (strcmp(spiUsers_[i], owner) == 0) at = i;
  }
  if (at == spiUserCount_) return Status::Denied;   // it never opened the bus
  for (uint8_t i = at; i + 1 < spiUserCount_; ++i) spiUsers_[i] = spiUsers_[i + 1];
  --spiUserCount_;
  if (spiUserCount_) return Status::Ok;
  spi_->end();
  releaseSpiPins();
  spiOpen_ = false;
  LMP_LOGI(src_, "SPI closed");
  return Status::Ok;
}

void BusService::endScan(Status outcome) {
  scan_ = outcome == Status::Ok ? Scan::Done : Scan::Idle;
  if (outcome == Status::Ok) {
    LMP_LOGI(src_, "I2C %u: %u chips found", static_cast<unsigned>(scanBus_),
             static_cast<unsigned>(foundCount_));
  }
  if (scanRef_) kernel_->events().publish(commandDone(scanRef_, outcome));
  scanRef_ = 0;
}

void BusService::tick(uint32_t) {
  if (scan_ != Scan::Running) return;
  hal::II2c* bus = i2c(scanBus_);
  if (!bus) {
    endScan(Status::Denied);
    return;
  }
  for (uint8_t i = 0; i < kScanStep && scanNext_ <= kI2cLast; ++i, ++scanNext_) {
    if (!bus->probe(scanNext_)) continue;
    if (foundCount_ < sizeof(found_)) {
      found_[foundCount_++] = scanNext_;
    } else {
      foundMore_ = true;
    }
  }
  if (scanNext_ > kI2cLast) endScan(Status::Ok);
}

// ---------------------------------------------------------------- documents and commands

void BusService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const hal::Board& board = kernel_->board();

  w.beginArray("i2c");
  for (size_t i = 0; i < count_; ++i) {
    const Bus& b = buses_[i];
    if (!b.hal) continue;
    w.beginObject();
    w.u32("bus", static_cast<uint32_t>(i));
    w.boolean("open", b.open);
    if (b.open) {
      w.u32("sda", b.sda);
      w.u32("scl", b.scl);
      w.u32("hz", b.hz);
      w.beginArray("users");
      for (uint8_t u = 0; u < b.userCount; ++u) w.str(StringView(b.users[u]));
      w.endArray();
    }
    w.endObject();
  }
  w.endArray();

  w.beginObject("scan");
  const char* state = "idle";
  if (scan_ == Scan::Running) state = "running";
  if (scan_ == Scan::Done) state = "done";
  w.str("state", state);
  if (scan_ != Scan::Idle) {
    w.u32("bus", scanBus_);
    // Addresses as the data sheets write them: "0x3C".
    w.beginArray("found");
    for (uint8_t i = 0; i < foundCount_; ++i) {
      char text[8];
      snprintf(text, sizeof(text), "0x%02X", static_cast<unsigned>(found_[i]));
      w.str(StringView(text));
    }
    w.endArray();
    if (foundMore_) w.boolean("more", true);
  }
  w.endObject();

  size_t uarts = 0;
  for (const hal::IUart* u : board.uart) {
    if (u) ++uarts;
  }
  w.boolean("spi", board.spi != nullptr);
  if (spiOpen_) {
    w.beginObject("spiBus");
    w.u32("sck", spiSck_);
    w.u32("mosi", spiMosi_);
    if (spiMiso_ >= 0) w.u32("miso", static_cast<uint32_t>(spiMiso_));
    w.beginArray("users");
    for (uint8_t u = 0; u < spiUserCount_; ++u) w.str(StringView(spiUsers_[u]));
    w.endArray();
    w.endObject();
  }
  w.u32("uart", static_cast<uint32_t>(uarts));
  w.boolean("oneWire", board.oneWire != nullptr);
}

void BusService::commands(CommandSet& set) {
  set.add("i2cOpen", LMP_HELP("Opens an I2C bus on two pins"), kOpenArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Status s = openI2c(a.u8("bus"), a.u8("sda"), a.u8("scl"),
                                     a.u32("hz", 100000), kConsole);
            if (s == Status::Invalid) ctx.errorField = "scl";
            if (s == Status::Unsupported) ctx.errorField = "bus";
            return s;
          });

  set.add("i2cClose", LMP_HELP("Closes the bus opened with i2cOpen"), kBusArgs,
          [this](const Args& a, CmdContext&) -> Status {
            return closeI2c(a.u8("bus"), kConsole);
          });

  set.add("i2cScan",
          LMP_HELP("Looks for the chips on an open bus. The list is in the state"), kBusArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (scan_ == Scan::Running) return Status::Busy;
            const uint8_t bus = a.u8("bus");
            if (!i2c(bus)) {
              ctx.errorField = "bus";
              return Status::Denied;   // not open
            }
            scan_ = Scan::Running;
            scanBus_ = bus;
            scanNext_ = kI2cFirst;
            scanRef_ = ctx.ref;
            foundCount_ = 0;
            foundMore_ = false;
            return Status::Pending;
          },
          kCmdAsync);
}

}  // namespace lmp

#endif  // LMP_FEATURE_BUS
