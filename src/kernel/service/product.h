// LoopMaxPro - the service of the product: one more service, written outside the system, that
// a firmware can bring with it. The system does not know what it does. It knows four things it
// can ask it: whether the modules of the device must stop and why, what it adds to the report
// of the device, and whether a request of a server is its own.
//
// A firmware names the function that gives its service with -DLMP_PRODUCT_SERVICE=<function>;
// the program hands it to the application before the start (LoopMax::setProduct). Without one,
// nothing of this is in the firmware.
#pragma once
#include "base/status.h"
#include "kernel/service/service.h"

namespace lmp {

class JsonValue;
class JsonWriter;

class IProduct {
 public:
  static constexpr ServiceId kId = ServiceId::Product;
  virtual ~IProduct() = default;

  // True when the modules of the device must not work now. A module of the kind "bridge" is
  // never stopped: what ends the block arrives through it. A module can keep a reduced form
  // (sdk/module.h, reducedWhenBlocked).
  virtual bool blocked() const = 0;
  // Why, in one word of its own. It is the reason of a module stopped by the block, the field
  // of a command refused for it, and the name of the event that tells a change.
  virtual const char* reason() const = 0;
  // Members of its own in the part "device" of the report.
  virtual void describeDevice(JsonWriter& w) const { (void)w; }
  // A request of the server of a type the cloud service does not know. Unsupported: not of
  // this service. Ok: taken. Anything else: of this service, and refused.
  virtual Status onServerRequest(const char* type, const JsonValue& body) {
    (void)type;
    (void)body;
    return Status::Unsupported;
  }
};

#ifdef LMP_PRODUCT_SERVICE
// Given by who wrote the service of this firmware.
Service& LMP_PRODUCT_SERVICE();
#endif

}  // namespace lmp
