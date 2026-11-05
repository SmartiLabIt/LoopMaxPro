// LoopMaxPro HAL for the PC - a cable that exists only in memory. The tests plug it into the
// board (Board::link) and decide when the cable is in and when the network gives the address.
#pragma once
#include <string>

#include "hal/api/ilink.h"

namespace lmp::hal::native {

class Link : public ILink {
 public:
  LinkKind kind() const override { return LinkKind::Ethernet; }
  Status init(const char* hostname) override;
  void setEventHandler(NetEventHandler handler) override { handler_ = handler; }
  void poll() override;
  Status start() override;
  Status stop() override;
  void info(LinkInfo& out) const override { out = info_; }
  void mac(Mac& out) const override { out = Mac{{0x02, 0x4C, 0x4D, 0x45, 0x00, 0x01}}; }

  // ---- hooks for the tests
  // The cable goes in or out. Out with an address: LinkLost.
  void plug(bool in);
  // The chip does not answer: init says NotFound.
  void setMissing(bool missing) { missing_ = missing; }
  // Calls to poll() between the cable in and the address.
  void setDelay(unsigned polls) { delay_ = polls; }
  void setAddress(Ip4 address) { address_ = address; }
  const std::string& hostname() const { return hostname_; }
  unsigned starts() const { return starts_; }

 private:
  void emit(NetEvent event);
  void settle();

  NetEventHandler handler_;
  LinkInfo info_;
  Ip4 address_ = Ip4::of(10, 0, 0, 20);
  std::string hostname_;
  bool missing_ = false;
  bool ready_ = false;
  bool running_ = false;
  bool plugged_ = true;
  unsigned delay_ = 3;
  unsigned left_ = 0;
  unsigned starts_ = 0;
};

}  // namespace lmp::hal::native
