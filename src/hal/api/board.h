// LoopMaxPro HAL - the table that describes a board.
// One implementation folder is compiled for each build and defines lmp::hal::board().
#pragma once
#include "hal/api/ibus.h"
#include "hal/api/iclock.h"
#include "hal/api/icrypto.h"
#include "hal/api/igpio.h"
#include "hal/api/ihttp.h"
#include "hal/api/ilink.h"
#include "hal/api/inet.h"
#include "hal/api/iprobe.h"
#include "hal/api/iradio.h"
#include "hal/api/irtos.h"
#include "hal/api/iserial.h"
#include "hal/api/istorage.h"
#include "hal/api/isystem.h"
#include "hal/api/types.h"
#include "lmp_config.h"

namespace lmp::hal {

struct Board {
  const char* name = LMP_BOARD;
  HalCaps caps;

  // Always present.
  ISystem* sys = nullptr;
  IClock* clock = nullptr;
  ISerial* serial = nullptr;
  IRtos* rtos = nullptr;
  IRandom* rng = nullptr;
  IGpio* gpio = nullptr;

  // Required by the OS: storage and network.
  IFs* fs = nullptr;
  IKv* kv = nullptr;
  INet* net = nullptr;
  IHttpServer* http = nullptr;

  // Optional: null when the board or the build does not have them.
  IHash* hash = nullptr;
  IPwm* pwm = nullptr;
  IPixels* pixels = nullptr;
  IAdc* adc = nullptr;
  II2c* i2c[LMP_HAL_I2C_MAX] = {};
  ISpi* spi = nullptr;
  IUart* uart[LMP_HAL_UART_MAX] = {};
  IOneWire* oneWire = nullptr;
  IOta* ota = nullptr;
  INtp* ntp = nullptr;
  IHttpClient* httpClient = nullptr;
  IMqtt* mqtt = nullptr;
  // A second client, for the bridge of the cloud: the one above stays for the broker of the
  // user. Null where the build has no cloud.
  IMqtt* cloudMqtt = nullptr;
  // Pings, ports, ARP, mDNS and SSDP: what the tools of a network ask (iprobe.h).
  INetProbe* probe = nullptr;
  // The other ways to the network, besides the Wi-Fi (ilink.h): there when the build looks for
  // them (LMP_NET_LINKS), usable when their chip answers. The modem is not written yet.
  ILink* cable = nullptr;
  ILink* modem = nullptr;

  // Chips on the board. Null when the board does not have them.
  const LoraWiring* lora = nullptr;
};

// Early hardware setup. Call it once, first thing at startup.
void boot();

// The board this firmware runs on.
const Board& board();

}  // namespace lmp::hal
