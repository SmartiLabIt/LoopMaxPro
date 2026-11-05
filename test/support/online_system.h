// A whole system on the PC, with the network and the servers decided by the test.
// The long operations run when the test lets the time pass: nothing happens on its own.
#pragma once
#include <string.h>
#include <unity.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "app/loopmax.h"
#include "test_support.h"

namespace lmp::test {

constexpr const char* kHome = "Home";
constexpr const char* kHomePassword = "home-password";
constexpr const char* kProbeUrl = "http://probe.test/generate_204";
constexpr int64_t kSomeTime = 1790000000;   // 21 September 2026

// State of a target, as the console reads it.
struct Doc {
  Json<8192> out;
  JsonValue v;

  Doc(Kernel& kernel, const char* target) {
    out.w.beginObject();
    TEST_ASSERT_TRUE_MESSAGE(kernel.commands().describe(target, out.w) == Status::Ok, target);
    out.w.endObject();
    TEST_ASSERT_TRUE_MESSAGE(out.w.complete(), target);
    v = out.parse();
    TEST_ASSERT_TRUE_MESSAGE(v.valid(), target);
  }
  const char* text() const { return out.text(); }
};

struct OnlineSystem {
  // Changes the settings the system starts with.
  using Setup = void (*)(Config& config);

  TestBoard tb;
  LoopMax app{tb.board};
  std::vector<Event> events;

  // lan: the device has a network in its settings and the network is around.
  // The check of Internet and the question of the position are off unless the setup gives
  // them an address.
  // modules: given by hand, they take the place of the ones of the build.
  // product: the service of the product of this device, when the test wants one.
  explicit OnlineSystem(bool lan = true, Setup setup = nullptr, uint32_t runMs = 1500,
                        std::initializer_list<sdk::Module*> modules = {},
                        Service* product = nullptr) {
    for (sdk::Module* m : modules) TEST_ASSERT_TRUE(app.modules().add(*m) == Status::Ok);
    if (product) app.setProduct(*product);
    tb.board.kv->begin();
    {
      Config config(tb.board.kv, tb.board.rng, tb.board.sys);
      config.load();
      if (lan) {
        tb.native().net.addNetwork(kHome, kHomePassword);
        config.net.staSsid.assign(kHome);
        config.net.staPsw.assign(kHomePassword);
        config.sys.mode = 2;
        config.touch(Config::kSys);
      }
      config.net.probeUrl.clear();
      config.touch(Config::kNet);
#if LMP_FEATURE_GEO
      config.geo.lookupUrl.clear();
      config.touch(Config::kGeo);
#endif
      if (setup) {
        setup(config);
        for (uint8_t i = 0; i < Config::kSectionCount; ++i) {
          config.touch(static_cast<Config::Section>(i));
        }
      }
      TEST_ASSERT_TRUE(config.save() == Status::Ok);
    }
    Logger::setInstance(&app.kernel().log());
    app.kernel().worker().setInline(true);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    app.kernel().events().subscribe([this](const Event& e) { events.push_back(e); });
    advance(runMs);
    TEST_ASSERT_TRUE(lan == app.net().connected());
  }

  Kernel& kernel() { return app.kernel(); }
  hal::native::Objects& pc() { return tb.native(); }
  void advance(uint32_t ms) { run(app.kernel(), tb.clock, ms); }

  Status command(const char* target, const char* name, const char* json,
                 const char** field = nullptr, uint32_t* ref = nullptr) {
    CmdContext ctx;
    ctx.source = CmdSource::Local;
    const Status s = kernel().commands().execute(target, name, JsonValue::parse(json), ctx);
    if (field) *field = ctx.errorField;
    if (ref) *ref = ctx.ref;
    return s;
  }

  // How many events of a kind arrived, and the last one.
  int count(EventId id, Event* last = nullptr) const {
    int n = 0;
    for (const Event& e : events) {
      if (e.id != id) continue;
      ++n;
      if (last) *last = e;
    }
    return n;
  }

  // The event that closes the command with this reference, or false.
  bool done(uint32_t ref, Status& outcome) const {
    for (const Event& e : events) {
      if (e.id == EventId::CommandDone && e.value == ref) {
        outcome = static_cast<Status>(e.b);
        return true;
      }
    }
    return false;
  }

  ServiceState stateOf(ServiceId id) {
    Service* s = kernel().services().find(id);
    return s ? s->state() : ServiceState::Stopped;
  }

  bool logHas(const char* text) {
    LogEntry entries[LMP_LOG_RING];
    const size_t n = kernel().log().read(0, entries, LMP_LOG_RING);
    for (size_t i = 0; i < n; ++i) {
      if (strstr(entries[i].msg, text)) return true;
    }
    return false;
  }
};

}  // namespace lmp::test
