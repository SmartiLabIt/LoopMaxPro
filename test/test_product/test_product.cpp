// Tests on the PC of the service of the product (kernel/service/product.h): one more service
// that a firmware can bring with it, which can stop the modules of the device. Here it is a
// service of the tests, a guard that blocks when the test says so.
#include <string.h>

#include <string>
#include <vector>

#include "../support/online_system.h"
#include "kernel/service/product.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

class Guard : public Service, public IProduct {
 public:
  const ServiceInfo& info() const override {
    static const ServiceInfo kInfo{ServiceId::Product, "guard", "", Mode::Any, RestartPolicy::Retry,
                                   1000, DepList(ServiceId::Config, ServiceId::Log)};
    return kInfo;
  }
  Status start(Kernel& kernel) override {
    kernel_ = &kernel;
    return Status::Ok;
  }
  void stop() override { kernel_ = nullptr; }
  void* api() override { return static_cast<IProduct*>(this); }

  bool blocked() const override { return closed_; }
  const char* reason() const override { return "guard"; }
  void describeDevice(JsonWriter& w) const override { w.u32("guard", 7); }
  Status onServerRequest(const char* type, const JsonValue& body) override {
    if (strcmp(type, "guard") != 0) return Status::Unsupported;
    if (!body["closed"].isBool()) return Status::Invalid;
    close(body["closed"].asBool());
    return Status::Ok;
  }

  // Before the start it only remembers; after, it tells the system.
  void close(bool closed) {
    closed_ = closed;
    if (kernel_) kernel_->events().publish(Event(EventId::ProductChanged, 0, closed ? 1 : 0));
  }

 private:
  Kernel* kernel_ = nullptr;
  bool closed_ = false;
};

// A module that keeps a reduced form while the device is blocked.
class Lamp final : public sdk::Module {
  LMP_MODULE("lamp", "Lamp of the test", "", "1.0.0", sdk::ModuleKind::Hardware, 100)

 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }
  bool reducedWhenBlocked() const override { return true; }
  void onBlocked(bool blocked) override { told.push_back(blocked); }

  std::vector<bool> told;
};

// A way to a server, as a bridge gives it.
class Bridge : public ICloudBridge {
 public:
  std::vector<std::string> sent;
  const char* name() const override { return "fake"; }
  void poll(uint32_t) override {}
  bool connected() const override { return true; }
  Status send(StringView envelope) override {
    sent.emplace_back(envelope.data, envelope.size);
    return Status::Ok;
  }
  void onReceive(CloudReceiveFn fn) override { deliver_ = fn; }
  void receive(const std::string& text) { deliver_(StringView(text.data(), text.size())); }

 private:
  CloudReceiveFn deliver_;
};

bool works(sdk::Module& m) {
  return m.state() == sdk::ModuleState::Ready || m.state() == sdk::ModuleState::Degraded;
}

}  // namespace

// A firmware without a service of the product: nobody can block it, and it says nothing more.
void test_without_the_service_nothing_is_there() {
  OnlineSystem sys;
  TEST_ASSERT_NULL(sys.kernel().api<IProduct>());
  TEST_ASSERT_TRUE(works(*sys.app.modules().find("demo")));
  Json<4096> report;
  sys.kernel().report(report.w, "device", true);
  TEST_ASSERT_FALSE(report.parse()["device"]["guard"].valid());
}

void test_a_blocked_device_stops_its_modules_and_starts_them_again() {
  Guard guard;
  OnlineSystem sys{true, nullptr, 1500, {}, &guard};
  sdk::Module& demo = *sys.app.modules().find("demo");
  TEST_ASSERT_TRUE(works(demo));
  TEST_ASSERT_TRUE(sys.command("mod.demo", "config.reset", "{}") == Status::Ok);

  // Blocked: the module stops, with the word of the service as its reason, and answers no more.
  guard.close(true);
  sys.advance(1100);
  TEST_ASSERT_TRUE(demo.state() == sdk::ModuleState::Stopped);
  TEST_ASSERT_EQUAL_STRING("guard", demo.reason());
  TEST_ASSERT_TRUE(sys.command("mod.demo", "config.reset", "{}") != Status::Ok);
  TEST_ASSERT_TRUE(sys.logHas("the guard is over"));
  // Nobody starts it again by hand.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.modules", "restart", "{\"module\":\"demo\"}", &field) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_STRING("guard", field);
  sys.advance(1500);
  TEST_ASSERT_TRUE(demo.state() == sdk::ModuleState::Stopped);

  // The block ends: at once, without a restart.
  guard.close(false);
  sys.advance(1500);
  TEST_ASSERT_TRUE(works(demo));
  TEST_ASSERT_TRUE(sys.command("mod.demo", "config.reset", "{}") == Status::Ok);
}

// Blocked when it is switched on: its module takes no pin and moves no output.
void test_a_device_that_starts_blocked_starts_no_module() {
  Guard guard;
  guard.close(true);
  OnlineSystem sys{true, nullptr, 1500, {}, &guard};
  sdk::Module& demo = *sys.app.modules().find("demo");
  TEST_ASSERT_TRUE(demo.state() == sdk::ModuleState::Stopped);
  TEST_ASSERT_EQUAL_STRING("guard", demo.reason());
  // The pins of the module are free: nobody took them.
  IGpio* gpio = sys.kernel().api<IGpio>();
  TEST_ASSERT_TRUE(gpio->claim(40, hal::PinMode::Output, "test", "Test") == Status::Ok);
  gpio->release(40, "test");

  guard.close(false);
  sys.advance(1500);
  TEST_ASSERT_TRUE(demo.state() == sdk::ModuleState::Ready);
}

// A module can stay on in a reduced form: it is told, and its state says why.
void test_a_module_that_keeps_a_reduced_form_is_told() {
  static Lamp lamp;
  lamp.told.clear();
  Guard guard;
  OnlineSystem sys{true, nullptr, 1500, {&lamp}, &guard};
  TEST_ASSERT_TRUE(lamp.state() == sdk::ModuleState::Ready);
  TEST_ASSERT_EQUAL_UINT(0, lamp.told.size());

  guard.close(true);
  sys.advance(1100);
  TEST_ASSERT_TRUE(lamp.state() == sdk::ModuleState::Degraded);
  TEST_ASSERT_EQUAL_STRING("guard", lamp.reason());
  TEST_ASSERT_EQUAL_UINT(1, lamp.told.size());
  TEST_ASSERT_TRUE(lamp.told[0]);

  guard.close(false);
  sys.advance(1100);
  TEST_ASSERT_TRUE(lamp.state() == sdk::ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("", lamp.reason());
  TEST_ASSERT_EQUAL_UINT(2, lamp.told.size());
  TEST_ASSERT_FALSE(lamp.told[1]);
}

// What the service adds to the report of the device, and the name of the event that tells its
// changes: both are its own.
void test_the_report_and_the_event_are_of_the_service() {
  Guard guard;
  OnlineSystem sys{true, nullptr, 1500, {}, &guard};
  Json<4096> report;
  sys.kernel().report(report.w, "device", true);
  TEST_ASSERT_EQUAL_INT64(7, report.parse()["device"]["guard"].asInt(-1));
  TEST_ASSERT_EQUAL_STRING("guard", eventName(EventId::ProductChanged));

  guard.close(true);
  sys.advance(500);
  Event told;
  TEST_ASSERT_TRUE(sys.count(EventId::ProductChanged, &told) >= 1);
  TEST_ASSERT_EQUAL_UINT8(1, told.b);
}

// A request of the server of a type the system does not know is offered to the service.
void test_a_request_of_the_server_can_be_of_the_service() {
  Guard guard;
  OnlineSystem sys{true, nullptr, 1500, {}, &guard};
  Bridge bridge;
  TEST_ASSERT_TRUE(sys.app.cloud().attach(bridge) == Status::Ok);
  sys.advance(3000);
  sdk::Module& demo = *sys.app.modules().find("demo");

  // Its own, and well made: taken. No answer is due.
  bridge.sent.clear();
  bridge.receive("{\"type\":\"guard\",\"body\":{\"closed\":true}}");
  sys.advance(1500);
  TEST_ASSERT_TRUE(guard.blocked());
  TEST_ASSERT_TRUE(demo.state() == sdk::ModuleState::Stopped);
  for (const std::string& e : bridge.sent) {
    TEST_ASSERT_FALSE(JsonValue::parse(StringView(e.data(), e.size()))["type"].stringEquals("error"));
  }
  {
    Doc cloud(sys.kernel(), "sys.cloud");
    TEST_ASSERT_EQUAL_INT64(0, cloud.v["refused"].asInt(-1));
  }

  // Its own, and badly made: refused, counted, nothing changes.
  bridge.receive("{\"type\":\"guard\",\"body\":{}}");
  sys.advance(1500);
  TEST_ASSERT_TRUE(guard.blocked());
  {
    Doc cloud(sys.kernel(), "sys.cloud");
    TEST_ASSERT_EQUAL_INT64(1, cloud.v["refused"].asInt(-1));
  }

  // Of nobody: the server is told.
  bridge.sent.clear();
  bridge.receive("{\"type\":\"nobody\",\"ref\":\"r1\",\"body\":{}}");
  sys.advance(1500);
  bool told = false;
  for (const std::string& e : bridge.sent) {
    const JsonValue env = JsonValue::parse(StringView(e.data(), e.size()));
    if (env["type"].stringEquals("error") && env["body"]["error"].stringEquals("unknown_type")) {
      told = true;
    }
  }
  TEST_ASSERT_TRUE(told);
  sys.app.cloud().detach(bridge);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_the_service_nothing_is_there);
  RUN_TEST(test_a_blocked_device_stops_its_modules_and_starts_them_again);
  RUN_TEST(test_a_device_that_starts_blocked_starts_no_module);
  RUN_TEST(test_a_module_that_keeps_a_reduced_form_is_told);
  RUN_TEST(test_the_report_and_the_event_are_of_the_service);
  RUN_TEST(test_a_request_of_the_server_can_be_of_the_service);
  return UNITY_END();
}
