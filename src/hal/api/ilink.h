// LoopMaxPro HAL - the link of the device to the network when it is not its Wi-Fi: a cable
// (Ethernet) or a modem (cellular). A build has one at most (LMP_NET_LINK); the Wi-Fi radio
// stays for the access point. No policy here: what to do while the link has no address belongs
// to the net service.
#pragma once
#include "hal/api/inet.h"

namespace lmp::hal {

enum class LinkKind : uint8_t { Wifi, Ethernet, Cellular };

inline const char* linkKindName(LinkKind kind) {
  switch (kind) {
    case LinkKind::Ethernet: return "eth";
    case LinkKind::Cellular: return "cell";
    case LinkKind::Wifi:     break;
  }
  return "wifi";
}

enum class LinkState : uint8_t {
  Off,        // not started
  Missing,    // the chip does not answer: not mounted, or wired wrong
  Down,       // started, no cable or no network
  Up,         // on the network, waiting for an address
  Connected   // with an address
};

inline const char* linkStateName(LinkState state) {
  switch (state) {
    case LinkState::Missing:   return "missing";
    case LinkState::Down:      return "down";
    case LinkState::Up:        return "up";
    case LinkState::Connected: return "connected";
    case LinkState::Off:       break;
  }
  return "off";
}

struct LinkInfo {
  LinkState state = LinkState::Off;
  Ip4 ip;
  Ip4 gateway;
  Ip4 mask;
  Ip4 dns;
  uint16_t speedMbps = 0;   // 0 when not known
  bool fullDuplex = false;
};

class ILink {
 public:
  virtual ~ILink() = default;

  virtual LinkKind kind() const = 0;
  // Sets the chip up, once. NotFound when it does not answer: info() then says Missing.
  virtual Status init(const char* hostname) = 0;
  // LinkGotIp and LinkLost, from the context of the driver.
  virtual void setEventHandler(NetEventHandler handler) = 0;
  virtual void poll() = 0;
  // Takes part in the network, or leaves it. The address comes from the network (DHCP) and
  // arrives as LinkGotIp; leaving on request raises no event.
  virtual Status start() = 0;
  virtual Status stop() = 0;
  virtual void info(LinkInfo& out) const = 0;
  virtual void mac(Mac& out) const = 0;
};

}  // namespace lmp::hal
