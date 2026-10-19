#include "hal/native/native_link.h"

namespace lmp::hal::native {

Status Link::init(const char* hostname) {
  hostname_ = hostname ? hostname : "";
  if (missing_) {
    info_ = LinkInfo();
    info_.state = LinkState::Missing;
    return Status::NotFound;
  }
  ready_ = true;
  return Status::Ok;
}

Status Link::start() {
  if (missing_) return Status::NotFound;
  if (!ready_) return Status::Denied;
  if (running_) return Status::Ok;
  running_ = true;
  ++starts_;
  settle();
  return Status::Ok;
}

Status Link::stop() {
  running_ = false;
  left_ = 0;
  info_ = LinkInfo();
  return Status::Ok;
}

void Link::plug(bool in) {
  plugged_ = in;
  if (!running_) return;
  const bool had = info_.state == LinkState::Connected;
  settle();
  if (had && !in) emit(NetEvent::LinkLost);
}

// Where the link is after the cable went in or out: the address comes some polls later.
void Link::settle() {
  info_ = LinkInfo();
  if (!plugged_) {
    info_.state = LinkState::Down;
    left_ = 0;
    return;
  }
  info_.state = LinkState::Up;
  info_.speedMbps = 100;
  info_.fullDuplex = true;
  left_ = delay_ ? delay_ : 1;
}

void Link::poll() {
  if (!running_ || !left_ || --left_) return;
  info_.state = LinkState::Connected;
  info_.ip = address_;
  info_.mask = Ip4::of(255, 255, 255, 0);
  info_.gateway = Ip4::of(address_.b[0], address_.b[1], address_.b[2], 1);
  info_.dns = info_.gateway;
  emit(NetEvent::LinkGotIp);
}

void Link::emit(NetEvent event) {
  if (handler_) handler_(event);
}

}  // namespace lmp::hal::native
