// Tests of the LoRa service on the PC, with the simulated radio.
#include <string.h>
#include <unity.h>

#include <string>

#include "../support/test_support.h"
#include "app/loopmax.h"
#include "drivers/lora/lora_airtime.h"
#include "services/lora/lora_band.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

struct System {
  TestBoard tb;
  LoopMax app{tb.board};
  unsigned received = 0;
  unsigned sentEvents = 0;
  Status lastSent = Status::Ok;

  System() {
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    app.kernel().events().subscribe([this](const Event& e) {
      if (e.id == EventId::LoraReceived) ++received;
      if (e.id == EventId::LoraSent) {
        ++sentEvents;
        lastSent = static_cast<Status>(e.b);
      }
    });
    run(app.kernel(), tb.clock, 200);
    tb.output();
  }

  Kernel& kernel() { return app.kernel(); }
  LoraService& lora() { return app.lora(); }
  FakeLoraRadio& radio() { return fakeLoraRadio(); }
  void pass(uint32_t ms) { run(app.kernel(), tb.clock, ms); }

  Status command(const char* name, const char* json, CmdContext* out = nullptr) {
    CmdContext ctx;
    ctx.source = CmdSource::Local;
    const Status s = kernel().commands().execute("sys.lora", name, JsonValue::parse(json), ctx);
    if (out) *out = ctx;
    return s;
  }
};

LoraParams params(uint8_t sf, uint32_t bw = 125000) {
  LoraParams p;
  p.spreadingFactor = sf;
  p.bandwidthHz = bw;
  return p;
}

}  // namespace

// ---------------------------------------------------------------- time on air

void test_airtime_matches_the_known_values() {
  // Values of the calculators of the chip maker: preamble 8, coding rate 4/5, CRC on.
  TEST_ASSERT_EQUAL_UINT32(41216, loraAirtimeUs(params(7), 10));
  TEST_ASSERT_EQUAL_UINT32(328704, loraAirtimeUs(params(9), 51));
  TEST_ASSERT_EQUAL_UINT32(991232, loraAirtimeUs(params(12), 10));
  TEST_ASSERT_EQUAL_UINT32(2465792, loraAirtimeUs(params(12), 51));
}

void test_airtime_follows_the_parameters() {
  TEST_ASSERT_EQUAL_UINT32(1024, loraSymbolUs(params(7)));
  TEST_ASSERT_EQUAL_UINT32(32768, loraSymbolUs(params(12)));
  TEST_ASSERT_FALSE(loraLowDataRate(params(10)));
  TEST_ASSERT_TRUE(loraLowDataRate(params(11)));
  TEST_ASSERT_FALSE(loraLowDataRate(params(12, 500000)));

  // Twice the bandwidth, half the time.
  TEST_ASSERT_EQUAL_UINT32(20608, loraAirtimeUs(params(7, 250000), 10));
  // More protection and no CRC change the number of symbols.
  LoraParams p = params(7);
  p.codingRate = 8;
  TEST_ASSERT_TRUE(loraAirtimeUs(p, 10) > loraAirtimeUs(params(7), 10));
  p = params(7);
  p.crc = false;
  TEST_ASSERT_TRUE(loraAirtimeUs(p, 10) < loraAirtimeUs(params(7), 10));
  // Values the formula does not cover.
  TEST_ASSERT_EQUAL_UINT32(0, loraAirtimeUs(params(6), 10));
  TEST_ASSERT_EQUAL_UINT32(0, loraAirtimeUs(params(7, 0), 10));
}

// ---------------------------------------------------------------- band rules

void test_band_868_parts() {
  TEST_ASSERT_EQUAL_UINT(6, loraSubBands(868).size());
  int i = loraSubBandOf(868, 868100000, 125000);
  TEST_ASSERT_EQUAL_INT(2, i);
  TEST_ASSERT_EQUAL_UINT(10, loraSubBands(868)[i].dutyPermille);
  TEST_ASSERT_EQUAL_INT(14, loraSubBands(868)[i].maxPowerDbm);

  i = loraSubBandOf(868, 869525000, 125000);
  TEST_ASSERT_EQUAL_INT(4, i);
  TEST_ASSERT_EQUAL_UINT(100, loraSubBands(868)[i].dutyPermille);

  i = loraSubBandOf(868, 868950000, 125000);
  TEST_ASSERT_EQUAL_INT(3, i);
  TEST_ASSERT_EQUAL_UINT(1, loraSubBands(868)[i].dutyPermille);
}

void test_band_refuses_what_is_outside() {
  // Between two parts, outside the band, across an edge, too wide for the part.
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(868, 868650000, 125000));
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(868, 915000000, 125000));
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(868, 868050000, 125000));
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(868, 869525000, 500000));
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(868, 0, 125000));

  TEST_ASSERT_EQUAL_INT(0, loraSubBandOf(433, 433175000, 125000));
  TEST_ASSERT_EQUAL_INT(-1, loraSubBandOf(433, 868100000, 125000));
  TEST_ASSERT_TRUE(loraSubBands(915).empty());
  TEST_ASSERT_EQUAL_UINT32(0, loraDefaultFrequency(915));

  TEST_ASSERT_TRUE(loraBandwidthAllowed(125000));
  TEST_ASSERT_TRUE(loraBandwidthAllowed(500000));
  TEST_ASSERT_FALSE(loraBandwidthAllowed(100000));
}

void test_budget_fills_up_with_time() {
  AirtimeBudget b;
  b.start(10, 1000);                       // 1 %: 36 seconds in one hour
  TEST_ASSERT_EQUAL_UINT32(36000, b.capacityMs());
  TEST_ASSERT_EQUAL_UINT32(36000, b.leftMs(1000));

  b.spend(1000000, 1000);
  TEST_ASSERT_EQUAL_UINT32(35000, b.leftMs(1000));
  // One second of time on air comes back in one hundred seconds.
  TEST_ASSERT_EQUAL_UINT32(35500, b.leftMs(51000));
  TEST_ASSERT_EQUAL_UINT32(36000, b.leftMs(101000));
  TEST_ASSERT_EQUAL_UINT32(36000, b.leftMs(900000));

  TEST_ASSERT_TRUE(b.allows(36000000, 900000));
  TEST_ASSERT_FALSE(b.allows(36000001, 900000));
  b.spend(36000000, 900000);
  TEST_ASSERT_EQUAL_UINT32(0, b.leftMs(900000));
  TEST_ASSERT_FALSE(b.allows(1000, 900000));
  TEST_ASSERT_TRUE(b.allows(1000, 900100));
}

void test_budget_survives_the_wrap_of_the_clock() {
  AirtimeBudget b;
  b.start(10, 0xFFFFFF00u);
  b.spend(2000000, 0xFFFFFF00u);
  // 512 milliseconds later the counter of the clock has started again from zero.
  TEST_ASSERT_EQUAL_UINT32(34005, b.leftMs(0x00000100u));
}

// ---------------------------------------------------------------- service

void test_service_starts_and_listens() {
  System sys;
  Service* s = sys.kernel().services().find(ServiceId::Lora);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_TRUE(s->state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Listening);
  TEST_ASSERT_TRUE(sys.lora().available());

  // The chip got the defaults of the band of the board.
  TEST_ASSERT_TRUE(sys.radio().started());
  TEST_ASSERT_TRUE(sys.radio().listening());
  TEST_ASSERT_EQUAL_UINT32(868100000, sys.radio().params().frequencyHz);
  TEST_ASSERT_EQUAL_UINT32(125000, sys.radio().params().bandwidthHz);
  TEST_ASSERT_EQUAL_UINT(7, sys.radio().params().spreadingFactor);
  TEST_ASSERT_EQUAL_INT(10, sys.radio().params().powerDbm);
  TEST_ASSERT_TRUE(sys.radio().params().crc);

  TEST_ASSERT_NOT_NULL(sys.kernel().api<ILora>());
}

void test_radio_pins_belong_to_the_service() {
  System sys;
  IGpio* gpio = sys.kernel().api<IGpio>();
  const hal::LoraWiring& w = *sys.tb.board.lora;
  const uint8_t pins[] = {w.cs, w.sck, w.mosi, w.miso, w.rst, w.busy, w.dio1};
  for (uint8_t pin : pins) TEST_ASSERT_EQUAL_STRING("lora", gpio->ownerOf(pin));
  TEST_ASSERT_TRUE(gpio->claim(w.cs, hal::PinMode::Output, "zappy2") == Status::Busy);
}

void test_send_text() {
  System sys;
  CmdContext ctx;
  Json<256> result;
  ctx.result = &result.w;
  result.w.beginObject();
  ctx.source = CmdSource::Local;
  TEST_ASSERT_TRUE(sys.kernel().commands().execute("sys.lora", "send",
                                                   JsonValue::parse("{\"text\":\"ping\"}"),
                                                   ctx) == Status::Ok);
  result.w.endObject();
  const JsonValue r = result.parse();
  TEST_ASSERT_EQUAL_INT64(1, r["seq"].asInt());
  TEST_ASSERT_EQUAL_INT64(4, r["bytes"].asInt());
  TEST_ASSERT_EQUAL_INT64(31, r["airtimeMs"].asInt());   // 30.976 ms, rounded up

  TEST_ASSERT_EQUAL_UINT(1, sys.radio().sent().size());
  TEST_ASSERT_EQUAL_STRING("ping", sys.radio().sent()[0].c_str());
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Sending);
  TEST_ASSERT_EQUAL_UINT(0, sys.sentEvents);

  sys.pass(50);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Listening);
  TEST_ASSERT_TRUE(sys.radio().listening());
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().sent);
  TEST_ASSERT_EQUAL_UINT32(31, sys.lora().stats().airtimeMs);
  TEST_ASSERT_EQUAL_UINT(1, sys.sentEvents);
  TEST_ASSERT_TRUE(sys.lastSent == Status::Ok);
}

void test_send_bytes() {
  System sys;
  TEST_ASSERT_TRUE(sys.command("send", "{\"hex\":\"01ff10\"}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().sent().size());
  const std::string& p = sys.radio().sent()[0];
  TEST_ASSERT_EQUAL_UINT(3, p.size());
  TEST_ASSERT_EQUAL_UINT8(0x01, static_cast<uint8_t>(p[0]));
  TEST_ASSERT_EQUAL_UINT8(0xFF, static_cast<uint8_t>(p[1]));
  TEST_ASSERT_EQUAL_UINT8(0x10, static_cast<uint8_t>(p[2]));
}

void test_send_rejects_bad_requests() {
  System sys;
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.command("send", "{}", &ctx) == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"a\",\"hex\":\"01\"}", &ctx) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("send", "{\"hex\":\"012\"}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("hex", ctx.errorField);
  TEST_ASSERT_TRUE(sys.command("send", "{\"hex\":\"zz\"}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_UINT(0, sys.radio().sent().size());

  // One packet at a time.
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"one\"}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"two\"}") == Status::Busy);
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().sent().size());
  sys.pass(50);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"two\"}") == Status::Ok);

  // Through the interface used by the modules.
  ILora* lora = sys.kernel().api<ILora>();
  sys.pass(50);
  TEST_ASSERT_TRUE(lora->send(nullptr, 4) == Status::Invalid);
  const uint8_t one = 1;
  TEST_ASSERT_TRUE(lora->send(&one, 0) == Status::Invalid);
  TEST_ASSERT_TRUE(lora->send(&one, 1) == Status::Ok);
}

void test_receive() {
  System sys;
  unsigned heard = 0;
  std::string lastText;
  TEST_ASSERT_TRUE(sys.lora().subscribe([&](const LoraPacket& p) {
    ++heard;
    lastText.assign(reinterpret_cast<const char*>(p.data), p.len);
  }));

  sys.radio().inject("temp=21.5", -92.5f, 7.25f);
  sys.pass(50);

  TEST_ASSERT_EQUAL_UINT(1, heard);
  TEST_ASSERT_EQUAL_STRING("temp=21.5", lastText.c_str());
  TEST_ASSERT_EQUAL_UINT(1, sys.received);
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().received);
  TEST_ASSERT_EQUAL_UINT(1, sys.lora().inboxCount());
  TEST_ASSERT_TRUE(sys.radio().listening());

  Json<4096> doc;
  doc.w.beginObject();
  TEST_ASSERT_TRUE(sys.kernel().commands().describe("sys.lora", doc.w) == Status::Ok);
  doc.w.endObject();
  const JsonValue v = doc.parse();
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_TRUE(v["chip"].stringEquals("SX1262"));
  TEST_ASSERT_EQUAL_INT64(868, v["band"].asInt());
  TEST_ASSERT_TRUE(v["state"].stringEquals("listening"));
  TEST_ASSERT_EQUAL_INT64(868100000, v["freq"].asInt());
  TEST_ASSERT_EQUAL_DOUBLE(1.0, v["limits"]["dutyPercent"].asDouble());
  TEST_ASSERT_EQUAL_INT64(14, v["limits"]["maxPower"].asInt());
  TEST_ASSERT_EQUAL_INT64(36000, v["limits"]["airtimeLeftMs"].asInt());
  TEST_ASSERT_EQUAL_DOUBLE(-92.5, v["last"]["rssi"].asDouble());
  TEST_ASSERT_EQUAL_UINT(1, v["inbox"].size());
  const JsonValue p = v["inbox"].at(0);
  TEST_ASSERT_EQUAL_INT64(1, p["seq"].asInt());
  TEST_ASSERT_EQUAL_INT64(9, p["bytes"].asInt());
  TEST_ASSERT_TRUE(p["text"].stringEquals("temp=21.5"));
  TEST_ASSERT_TRUE(p["hex"].stringEquals("74656d703d32312e35"));
  TEST_ASSERT_EQUAL_DOUBLE(7.2, p["snr"].asDouble());
}

void test_receive_bytes_and_damaged_packets() {
  System sys;
  sys.radio().inject(std::string("\x00\x01\xfe", 3));
  sys.radio().injectDamaged();
  sys.pass(50);
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().received);
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().damaged);
  TEST_ASSERT_EQUAL_UINT(1, sys.received);

  Json<2048> doc;
  doc.w.beginObject();
  sys.kernel().commands().describe("sys.lora", doc.w);
  doc.w.endObject();
  const JsonValue p = doc.parse()["inbox"].at(0);
  TEST_ASSERT_TRUE(p["hex"].stringEquals("0001fe"));
  // What is not text is given as bytes only.
  TEST_ASSERT_FALSE(p["text"].valid());
}

void test_inbox_keeps_the_last_packets() {
  System sys;
  const unsigned total = LMP_LORA_INBOX + 3;
  for (unsigned i = 1; i <= total; ++i) {
    sys.radio().inject("p" + std::to_string(i));
    sys.pass(30);
  }
  TEST_ASSERT_EQUAL_UINT32(total, sys.lora().stats().received);
  TEST_ASSERT_EQUAL_UINT(LMP_LORA_INBOX, sys.lora().inboxCount());
  TEST_ASSERT_EQUAL_UINT32(4, sys.lora().inboxAt(0).seq);
  TEST_ASSERT_EQUAL_UINT32(total, sys.lora().inboxAt(LMP_LORA_INBOX - 1).seq);

  // The document with a full inbox of long packets is still valid JSON.
  TEST_ASSERT_TRUE(sys.command("clear", "{}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(0, sys.lora().inboxCount());
  TEST_ASSERT_EQUAL_UINT32(0, sys.lora().stats().received);
  for (unsigned i = 0; i < LMP_LORA_INBOX; ++i) {
    sys.radio().inject(std::string(LMP_LORA_MAX_PAYLOAD, '\x07'));
    sys.pass(30);
  }
  static Json<8192> doc;
  doc.sink.clear();
  JsonWriter w(doc.sink);
  w.beginObject();
  sys.kernel().commands().describe("sys.lora", w);
  w.endObject();
  TEST_ASSERT_TRUE(w.complete());
  TEST_ASSERT_TRUE(JsonValue::parse(doc.sink.view()).valid());
}

void test_nothing_is_heard_when_not_listening() {
  System sys;
  TEST_ASSERT_TRUE(sys.command("listen", "{\"on\":false}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Idle);
  TEST_ASSERT_FALSE(sys.radio().listening());
  sys.radio().inject("lost");
  sys.pass(50);
  TEST_ASSERT_EQUAL_UINT32(0, sys.lora().stats().received);

  // Sending still works, and the radio goes back to rest.
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"x\"}") == Status::Ok);
  sys.pass(50);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Idle);

  TEST_ASSERT_TRUE(sys.command("listen", "{\"on\":true}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.radio().listening());
}

void test_settings_are_checked() {
  System sys;
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.command("set", "{}", &ctx) == Status::Invalid);

  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":868650000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("freq", ctx.errorField);
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":915000000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("freq", ctx.errorField);
  TEST_ASSERT_TRUE(sys.command("set", "{\"bw\":100000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("bw", ctx.errorField);
  TEST_ASSERT_TRUE(sys.command("set", "{\"sf\":6}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("sf", ctx.errorField);
  TEST_ASSERT_TRUE(sys.command("set", "{\"cr\":9}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("cr", ctx.errorField);
  // 14 dBm is the most in this part of the band.
  TEST_ASSERT_TRUE(sys.command("set", "{\"power\":15}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("power", ctx.errorField);
  // A signal 500 kHz wide would cross the edge of this part of the band.
  TEST_ASSERT_TRUE(sys.command("set", "{\"bw\":500000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("freq", ctx.errorField);

  // Nothing of what was refused reached the chip.
  TEST_ASSERT_EQUAL_UINT32(868100000, sys.radio().params().frequencyHz);
  TEST_ASSERT_EQUAL_UINT32(125000, sys.radio().params().bandwidthHz);
  TEST_ASSERT_EQUAL_UINT(7, sys.radio().params().spreadingFactor);
  TEST_ASSERT_EQUAL_INT(10, sys.radio().params().powerDbm);

  // Moved to where it fits, the wide signal is accepted.
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":868300000,\"bw\":500000}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":868400000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("freq", ctx.errorField);
  TEST_ASSERT_EQUAL_UINT32(868300000, sys.radio().params().frequencyHz);
}

void test_settings_reach_the_chip() {
  System sys;
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":869525000,\"sf\":9,\"cr\":8,\"power\":20,"
                                      "\"sync\":52}") == Status::Ok);
  const LoraParams& p = sys.radio().params();
  TEST_ASSERT_EQUAL_UINT32(869525000, p.frequencyHz);
  TEST_ASSERT_EQUAL_UINT(9, p.spreadingFactor);
  TEST_ASSERT_EQUAL_UINT(8, p.codingRate);
  TEST_ASSERT_EQUAL_INT(20, p.powerDbm);
  TEST_ASSERT_EQUAL_UINT(0x34, p.syncWord);
  // The radio listens again with the new values.
  TEST_ASSERT_TRUE(sys.radio().listening());
  // The other part of the band has its own share of time: 10 % of one hour.
  TEST_ASSERT_EQUAL_UINT32(360000, sys.lora().airtimeLeftMs());

  // Going back, the power allowed there is lower: the change is refused as a whole.
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":868100000}", &ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("power", ctx.errorField);
  TEST_ASSERT_EQUAL_UINT32(869525000, sys.radio().params().frequencyHz);
  TEST_ASSERT_TRUE(sys.command("set", "{\"freq\":868100000,\"power\":14}") == Status::Ok);
}

void test_settings_survive_a_restart() {
  TestBoard tb;
  {
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    app.begin();
    run(app.kernel(), tb.clock, 200);
    CmdContext ctx;
    TEST_ASSERT_TRUE(app.kernel().commands().execute(
        "sys.lora", "set", JsonValue::parse("{\"freq\":867500000,\"sf\":10,\"power\":2}"),
        ctx) == Status::Ok);
    TEST_ASSERT_TRUE(app.kernel().commands().execute(
        "sys.lora", "listen", JsonValue::parse("{\"on\":false}"), ctx) == Status::Ok);
    Logger::setInstance(nullptr);
  }

  tb.native().fs.unmount();
  fakeLoraRadio().reset();
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  app.begin();
  run(app.kernel(), tb.clock, 200);
  const LoraConfig& c = app.lora().settings();
  TEST_ASSERT_EQUAL_UINT32(867500000, c.frequencyHz);
  TEST_ASSERT_EQUAL_UINT(10, c.spreadingFactor);
  TEST_ASSERT_EQUAL_INT(2, c.powerDbm);
  TEST_ASSERT_FALSE(c.listen);
  TEST_ASSERT_EQUAL_UINT32(867500000, fakeLoraRadio().params().frequencyHz);
  TEST_ASSERT_FALSE(fakeLoraRadio().listening());
}

void test_factory_reset_clears_the_settings() {
  TestBoard tb;
  {
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    app.begin();
    run(app.kernel(), tb.clock, 200);
    CmdContext ctx;
    app.kernel().commands().execute("sys.lora", "set", JsonValue::parse("{\"sf\":11}"), ctx);
    TEST_ASSERT_EQUAL_UINT(11, app.lora().settings().spreadingFactor);
    TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.system", "factoryReset",
                                                     JsonValue::parse("{}"), ctx) == Status::Ok);
    run(app.kernel(), tb.clock, 1500);
    Logger::setInstance(nullptr);
  }

  tb.native().fs.unmount();
  fakeLoraRadio().reset();
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  app.begin();
  run(app.kernel(), tb.clock, 200);
  TEST_ASSERT_EQUAL_UINT(7, app.lora().settings().spreadingFactor);
  TEST_ASSERT_EQUAL_UINT32(868100000, app.lora().settings().frequencyHz);
}

void test_time_on_air_is_limited() {
  System sys;
  // The slowest setting: a packet of 51 bytes stays on air for 2.47 seconds.
  TEST_ASSERT_TRUE(sys.command("set", "{\"sf\":12}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(2466, sys.lora().airtimeMs(51));
  const std::string packet(51, 'x');
  const std::string json = "{\"text\":\"" + packet + "\"}";

  // 36 seconds in one hour: 14 packets fit, the next does not.
  unsigned accepted = 0;
  CmdContext ctx;
  for (unsigned i = 0; i < 20; ++i) {
    const Status s = sys.command("send", json.c_str(), &ctx);
    if (s != Status::Ok) {
      TEST_ASSERT_TRUE(s == Status::Denied);
      TEST_ASSERT_EQUAL_STRING("duty", ctx.errorField);
      break;
    }
    ++accepted;
    sys.pass(30);
  }
  TEST_ASSERT_EQUAL_UINT(14, accepted);
  TEST_ASSERT_EQUAL_UINT(14, sys.radio().sent().size());
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().refused);
  TEST_ASSERT_TRUE(sys.lora().airtimeLeftMs() < 2466);

  // A short packet still fits in what is left.
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"ok\"}") == Status::Ok);
  sys.pass(30);

  // Five minutes later three seconds came back: enough for one long packet, not for two.
  sys.pass(300000);
  TEST_ASSERT_TRUE(sys.command("send", json.c_str()) == Status::Ok);
  sys.pass(30);
  TEST_ASSERT_TRUE(sys.command("send", json.c_str(), &ctx) == Status::Denied);
}

void test_radio_that_does_not_answer() {
  TestBoard tb;
  fakeLoraRadio().setPresent(false);
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 300);

  Service* s = app.kernel().services().find(ServiceId::Lora);
  TEST_ASSERT_TRUE(s->state() == ServiceState::Error);
  TEST_ASSERT_EQUAL_STRING("radio does not answer", s->reason());
  TEST_ASSERT_FALSE(app.lora().available());
  // The rest of the system is not held back, and the pins are free again.
  TEST_ASSERT_TRUE(app.kernel().log().bootDoneMs() > 0);
  TEST_ASSERT_TRUE(app.kernel().services().find(ServiceId::Config)->state() ==
                   ServiceState::Ready);
  TEST_ASSERT_NULL(app.kernel().api<IGpio>()->ownerOf(tb.board.lora->cs));

  const uint8_t one = 1;
  TEST_ASSERT_TRUE(app.lora().send(&one, 1) == Status::Denied);

  // The service tries again by itself: the radio came back.
  fakeLoraRadio().setPresent(true);
  run(app.kernel(), tb.clock, ServiceRegistry::kRetryFirstMs + 500);
  TEST_ASSERT_TRUE(s->state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(app.lora().radioState() == LoraState::Listening);
  TEST_ASSERT_TRUE(fakeLoraRadio().begins() >= 2);
}

void test_transmission_that_never_ends() {
  System sys;
  sys.radio().setTxStuck(true);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"ping\"}") == Status::Ok);
  sys.pass(1000);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Sending);
  sys.pass(1000);

  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Listening);
  TEST_ASSERT_EQUAL_UINT32(0, sys.lora().stats().sent);
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().failed);
  TEST_ASSERT_EQUAL_UINT(1, sys.sentEvents);
  TEST_ASSERT_TRUE(sys.lastSent == Status::Timeout);

  sys.radio().setTxStuck(false);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"ping\"}") == Status::Ok);
  sys.pass(50);
  TEST_ASSERT_EQUAL_UINT32(1, sys.lora().stats().sent);
}

void test_send_asked_during_a_round_of_the_system() {
  // On a board the clock moves while the services run: a request that arrives from the
  // command line starts after the time the round was given.
  struct MovingClock : FakeClock {
    uint32_t millis() const override { return const_cast<MovingClock*>(this)->now++; }
  };
  TestBoard tb;
  MovingClock clock;
  hal::Board board = tb.board;
  board.clock = &clock;

  LoopMax app(board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  for (int i = 0; i < 50; ++i) app.kernel().tick();
  TEST_ASSERT_TRUE(app.lora().radioState() == LoraState::Listening);

  fakeLoraRadio().setTxStuck(true);
  tb.native().serial.feedInput("cmd sys.lora send text=ping\n");
  // Enough rounds for the serial service to read the line and for the radio service to
  // look at the transmission, not enough for its time to run out.
  for (int i = 0; i < 40; ++i) app.kernel().tick();
  TEST_ASSERT_EQUAL_UINT(1, fakeLoraRadio().sent().size());
  TEST_ASSERT_TRUE(app.lora().radioState() == LoraState::Sending);
  TEST_ASSERT_EQUAL_UINT32(0, app.lora().stats().failed);

  fakeLoraRadio().setTxStuck(false);
  for (int i = 0; i < 40; ++i) app.kernel().tick();
  TEST_ASSERT_EQUAL_UINT32(1, app.lora().stats().sent);
  TEST_ASSERT_TRUE(app.lora().radioState() == LoraState::Listening);
}

void test_radio_off_and_on() {
  System sys;
  TEST_ASSERT_TRUE(sys.command("enable", "{\"on\":false}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Off);
  TEST_ASSERT_FALSE(sys.radio().started());
  TEST_ASSERT_FALSE(sys.lora().available());

  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"ping\"}", &ctx) == Status::Denied);
  TEST_ASSERT_EQUAL_STRING("enabled", ctx.errorField);
  // The settings can change while the radio is off.
  TEST_ASSERT_TRUE(sys.command("set", "{\"sf\":8}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.kernel().services().find(ServiceId::Lora)->state() ==
                   ServiceState::Ready);

  TEST_ASSERT_TRUE(sys.command("enable", "{\"on\":true}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.lora().radioState() == LoraState::Listening);
  TEST_ASSERT_EQUAL_UINT(8, sys.radio().params().spreadingFactor);
  TEST_ASSERT_TRUE(sys.command("send", "{\"text\":\"ping\"}") == Status::Ok);
}

void test_command_line() {
  System sys;
  sys.tb.output();
  sys.app.serial().execute("cmd sys.lora send text=ping");
  std::string out = sys.tb.output();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"airtimeMs\":31"));
  sys.pass(50);

  sys.app.serial().execute("cmd sys.lora set freq=869525000 power=20");
  out = sys.tb.output();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"ok\":true"));
  TEST_ASSERT_EQUAL_UINT32(869525000, sys.radio().params().frequencyHz);

  sys.app.serial().execute("cmd sys.lora set freq=868650000");
  out = sys.tb.output();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"field\":\"freq\""));

  sys.app.serial().execute("man sys.lora");
  out = sys.tb.output();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"name\":\"send\""));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"name\":\"enable\""));
}

// A packet for a receiver that sleeps: a long preamble, then the settings again.
void test_a_long_preamble_for_one_packet() {
  System sys;
  const uint8_t data[] = {1, 2, 3, 4};
  const uint32_t shortMs = sys.lora().airtimeMs(sizeof(data));
  const uint32_t leftBefore = sys.lora().airtimeLeftMs();
  TEST_ASSERT_TRUE(sys.lora().send(data, sizeof(data), 1100) == Status::Ok);
  sys.pass(1300);
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().sentPreambles().size());
  TEST_ASSERT_EQUAL_UINT16(1100, sys.radio().sentPreambles()[0]);
  TEST_ASSERT_EQUAL_UINT16(8, sys.radio().params().preamble);   // back to the settings
  TEST_ASSERT_TRUE(sys.radio().listening());
  // Its time on air is the long one: more than a second at SF7.
  const uint32_t spent = leftBefore - sys.lora().airtimeLeftMs();
  TEST_ASSERT_TRUE(spent > 1000 && spent > shortMs * 10);
  TEST_ASSERT_TRUE(sys.lora().send(data, sizeof(data)) == Status::Ok);
  sys.pass(200);
  TEST_ASSERT_EQUAL_UINT16(8, sys.radio().sentPreambles()[1]);
}

// A sync word for now: used by the radio, not saved, and given back with 0.
void test_a_sync_word_for_now() {
  System sys;
  TEST_ASSERT_TRUE(sys.lora().useSyncWord(0x5B) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(0x5B, sys.radio().params().syncWord);
  TEST_ASSERT_TRUE(sys.radio().listening());
  const uint8_t data[] = {9};
  sys.lora().send(data, 1);
  sys.pass(200);
  TEST_ASSERT_EQUAL_UINT8(0x5B, sys.radio().sentSyncs().back());
  TEST_ASSERT_EQUAL_UINT8(0x12, sys.lora().settings().syncWord);   // the settings did not change
  TEST_ASSERT_TRUE(sys.lora().useSyncWord(0) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(0x12, sys.radio().params().syncWord);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_long_preamble_for_one_packet);
  RUN_TEST(test_a_sync_word_for_now);
  RUN_TEST(test_airtime_matches_the_known_values);
  RUN_TEST(test_airtime_follows_the_parameters);
  RUN_TEST(test_band_868_parts);
  RUN_TEST(test_band_refuses_what_is_outside);
  RUN_TEST(test_budget_fills_up_with_time);
  RUN_TEST(test_budget_survives_the_wrap_of_the_clock);
  RUN_TEST(test_service_starts_and_listens);
  RUN_TEST(test_radio_pins_belong_to_the_service);
  RUN_TEST(test_send_text);
  RUN_TEST(test_send_bytes);
  RUN_TEST(test_send_rejects_bad_requests);
  RUN_TEST(test_receive);
  RUN_TEST(test_receive_bytes_and_damaged_packets);
  RUN_TEST(test_inbox_keeps_the_last_packets);
  RUN_TEST(test_nothing_is_heard_when_not_listening);
  RUN_TEST(test_settings_are_checked);
  RUN_TEST(test_settings_reach_the_chip);
  RUN_TEST(test_settings_survive_a_restart);
  RUN_TEST(test_factory_reset_clears_the_settings);
  RUN_TEST(test_time_on_air_is_limited);
  RUN_TEST(test_radio_that_does_not_answer);
  RUN_TEST(test_transmission_that_never_ends);
  RUN_TEST(test_send_asked_during_a_round_of_the_system);
  RUN_TEST(test_radio_off_and_on);
  RUN_TEST(test_command_line);
  return UNITY_END();
}
