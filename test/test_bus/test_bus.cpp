// Tests on the PC of the buses and of the worker: what the hardware outside the board and
// the long operations need from the system.
#include <atomic>
#include <chrono>
#include <thread>

#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

// ---------------------------------------------------------------- I2C

void test_a_bus_is_opened_and_shared() {
  OnlineSystem sys(false);
  IBus* bus = sys.kernel().api<IBus>();
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_NOT_NULL(bus);
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Bus) == ServiceState::Ready);
  TEST_ASSERT_EQUAL_UINT(LMP_HAL_I2C_MAX, bus->i2cCount());
  TEST_ASSERT_NULL(bus->i2c(0));   // nothing is open until somebody asks

  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 400000, "display") == Status::Ok);
  TEST_ASSERT_NOT_NULL(bus->i2c(0));
  TEST_ASSERT_TRUE(sys.pc().i2c[0].up());
  TEST_ASSERT_EQUAL_UINT8(21, sys.pc().i2c[0].sda());
  TEST_ASSERT_EQUAL_UINT8(22, sys.pc().i2c[0].scl());
  TEST_ASSERT_EQUAL_UINT32(400000, sys.pc().i2c[0].frequency());
  // The pins belong to the bus: nobody else can take them.
  TEST_ASSERT_EQUAL_STRING("i2c0", pins->ownerOf(21));
  TEST_ASSERT_EQUAL_STRING("i2c0", pins->ownerOf(22));
  TEST_ASSERT_TRUE(pins->claim(21, hal::PinMode::Output, "relay") == Status::Busy);

  // A second chip on the same wires joins the bus. Other pins are refused.
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "sensor") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(400000, sys.pc().i2c[0].frequency());   // who came first decided
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "sensor") == Status::Ok);   // already in
  TEST_ASSERT_TRUE(bus->openI2c(0, 4, 5, 100000, "other") == Status::Busy);

  Doc doc(sys.kernel(), "sys.bus");
  TEST_ASSERT_EQUAL_UINT(LMP_HAL_I2C_MAX, doc.v["i2c"].size());
  TEST_ASSERT_TRUE(doc.v["i2c"].at(0)["open"].asBool());
  TEST_ASSERT_EQUAL_INT64(21, doc.v["i2c"].at(0)["sda"].asInt());
  TEST_ASSERT_EQUAL_UINT(2, doc.v["i2c"].at(0)["users"].size());
  TEST_ASSERT_TRUE(doc.v["i2c"].at(0)["users"].at(1).stringEquals("sensor"));
  TEST_ASSERT_FALSE(doc.v["i2c"].at(1)["open"].asBool(true));

  // The bus closes when the last one leaves.
  TEST_ASSERT_TRUE(bus->closeI2c(0, "stranger") == Status::Denied);
  TEST_ASSERT_TRUE(bus->closeI2c(0, "display") == Status::Ok);
  TEST_ASSERT_NOT_NULL(bus->i2c(0));
  TEST_ASSERT_TRUE(bus->closeI2c(0, "sensor") == Status::Ok);
  TEST_ASSERT_NULL(bus->i2c(0));
  TEST_ASSERT_FALSE(sys.pc().i2c[0].up());
  TEST_ASSERT_NULL(pins->ownerOf(21));
  TEST_ASSERT_NULL(pins->ownerOf(22));
  TEST_ASSERT_TRUE(bus->closeI2c(0, "sensor") == Status::NotFound);
}

void test_a_bus_that_cannot_be_opened() {
  OnlineSystem sys(false);
  IBus* bus = sys.kernel().api<IBus>();
  IGpio* pins = sys.kernel().api<IGpio>();

  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 21, 100000, "x") == Status::Invalid);
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 0, "x") == Status::Invalid);
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "") == Status::Invalid);
  TEST_ASSERT_TRUE(bus->openI2c(LMP_HAL_I2C_MAX, 21, 22, 100000, "x") == Status::Invalid);

  // One of the two pins belongs to somebody: the other is given back.
  TEST_ASSERT_TRUE(pins->claim(22, hal::PinMode::Output, "relay") == Status::Ok);
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "x") == Status::Busy);
  TEST_ASSERT_NULL(pins->ownerOf(21));
  TEST_ASSERT_EQUAL_STRING("relay", pins->ownerOf(22));
  TEST_ASSERT_TRUE(pins->release(22, "relay") == Status::Ok);

  // The driver says no: the pins are given back.
  sys.pc().i2c[0].failNext(Status::Io);
  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "x") == Status::Io);
  TEST_ASSERT_NULL(pins->ownerOf(21));
  TEST_ASSERT_NULL(pins->ownerOf(22));
  TEST_ASSERT_NULL(bus->i2c(0));
  TEST_ASSERT_TRUE(sys.logHas("not opened"));
}

void test_chips_are_found_on_a_bus() {
  OnlineSystem sys(false);
  hal::native::I2c& wires = sys.pc().i2c[0];
  wires.addDevice(0x3C);
  wires.addDevice(0x76);
  wires.addDevice(0x03);   // outside the addresses a chip can have: never asked

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cScan", "{}", &field) == Status::Denied);
  TEST_ASSERT_EQUAL_STRING("bus", field);

  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cOpen", "{\"sda\":21,\"scl\":22}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(100000, wires.frequency());
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cScan", "{\"bus\":0}", nullptr, &ref) ==
                   Status::Pending);
  TEST_ASSERT_TRUE(ref != 0);
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cScan", "{}") == Status::Busy);

  // A few addresses for each round: the system is never held.
  sys.advance(40);
  Doc running(sys.kernel(), "sys.bus");
  TEST_ASSERT_TRUE(running.v["scan"]["state"].stringEquals("running"));
  TEST_ASSERT_TRUE(wires.probes() > 0 && wires.probes() < 112);

  sys.advance(500);
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(112, wires.probes());
  Doc doc(sys.kernel(), "sys.bus");
  TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("done"));
  TEST_ASSERT_EQUAL_INT64(0, doc.v["scan"]["bus"].asInt());
  TEST_ASSERT_EQUAL_UINT(2, doc.v["scan"]["found"].size());
  TEST_ASSERT_TRUE(doc.v["scan"]["found"].at(0).stringEquals("0x3C"));
  TEST_ASSERT_TRUE(doc.v["scan"]["found"].at(1).stringEquals("0x76"));
  TEST_ASSERT_TRUE(sys.logHas("I2C 0: 2 chips found"));

  // A chip is read through the bus.
  wires.setRegister(0x76, 0xD0, 0x60);
  const uint8_t reg = 0xD0;
  uint8_t id = 0;
  hal::II2c* i2c = sys.kernel().api<IBus>()->i2c(0);
  TEST_ASSERT_TRUE(i2c->writeRead(0x76, &reg, 1, &id, 1) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX8(0x60, id);
  TEST_ASSERT_TRUE(i2c->writeRead(0x10, &reg, 1, &id, 1) == Status::NotFound);

  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cClose", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cClose", "{}") == Status::NotFound);
}

void test_a_scan_ends_when_the_bus_closes() {
  OnlineSystem sys(false);
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cOpen", "{\"sda\":21,\"scl\":22,\"bus\":1}") ==
                   Status::Ok);
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cScan", "{\"bus\":1}", nullptr, &ref) ==
                   Status::Pending);
  sys.advance(40);
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cClose", "{\"bus\":1}") == Status::Ok);
  sys.advance(100);
  Status outcome = Status::Ok;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Denied);
  Doc doc(sys.kernel(), "sys.bus");
  TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("idle"));
}

void test_more_chips_than_the_list_holds() {
  OnlineSystem sys(false);
  for (uint8_t a = 0x20; a < 0x38; ++a) sys.pc().i2c[0].addDevice(a);   // twenty-four
  sys.command("sys.bus", "i2cOpen", "{\"sda\":21,\"scl\":22}");
  TEST_ASSERT_TRUE(sys.command("sys.bus", "i2cScan", "{}") == Status::Pending);
  sys.advance(600);
  Doc doc(sys.kernel(), "sys.bus");
  TEST_ASSERT_EQUAL_UINT(16, doc.v["scan"]["found"].size());
  TEST_ASSERT_TRUE(doc.v["scan"]["more"].asBool());
}

void test_a_board_without_buses() {
  TestBoard tb;
  for (hal::II2c*& bus : tb.board.i2c) bus = nullptr;
  tb.board.spi = nullptr;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 500);
  TEST_ASSERT_TRUE(app.bus().running());
  TEST_ASSERT_EQUAL_UINT(0, app.bus().i2cCount());
  TEST_ASSERT_TRUE(app.bus().openI2c(0, 21, 22, 100000, "x") == Status::Unsupported);
  Doc doc(app.kernel(), "sys.bus");
  TEST_ASSERT_EQUAL_UINT(0, doc.v["i2c"].size());
  TEST_ASSERT_FALSE(doc.v["spi"].asBool(true));
  TEST_ASSERT_TRUE(app.bus().openSpi(18, -1, 23, "x") == Status::Unsupported);
}

// ---------------------------------------------------------------- SPI

void test_the_spi_bus_is_opened_and_shared() {
  OnlineSystem sys(false);
  IBus* bus = sys.kernel().api<IBus>();
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_NULL(bus->spi());   // nothing is open until somebody asks

  // A screen: nobody answers on the bus, so no pin comes back.
  TEST_ASSERT_TRUE(bus->openSpi(18, -1, 23, "screen") == Status::Ok);
  TEST_ASSERT_NOT_NULL(bus->spi());
  TEST_ASSERT_TRUE(sys.pc().spi.open());
  TEST_ASSERT_EQUAL_UINT8(18, sys.pc().spi.sck());
  TEST_ASSERT_EQUAL_UINT8(23, sys.pc().spi.mosi());
  TEST_ASSERT_EQUAL_STRING("spi", pins->ownerOf(18));
  TEST_ASSERT_EQUAL_STRING("spi", pins->ownerOf(23));
  TEST_ASSERT_TRUE(pins->claim(18, hal::PinMode::Output, "relay") == Status::Busy);

  // Another chip joins on the same pins, and is refused on others.
  TEST_ASSERT_TRUE(bus->openSpi(18, -1, 23, "radio") == Status::Ok);
  TEST_ASSERT_TRUE(bus->openSpi(18, 19, 23, "card") == Status::Busy);
  TEST_ASSERT_TRUE(bus->openSpi(18, -1, 18, "wrong") == Status::Invalid);

  const uint8_t bytes[] = {0x12, 0x34};
  TEST_ASSERT_TRUE(bus->spi()->transfer(hal::kSpiNoCs, hal::SpiConfig(), bytes, nullptr, 2) ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().spi.sent().size());

  Doc doc(sys.kernel(), "sys.bus");
  TEST_ASSERT_EQUAL_INT64(18, doc.v["spiBus"]["sck"].asInt());
  TEST_ASSERT_EQUAL_UINT(2, doc.v["spiBus"]["users"].size());

  // The bus closes when the last one leaves, and gives its pins back.
  TEST_ASSERT_TRUE(bus->closeSpi("stranger") == Status::Denied);
  TEST_ASSERT_TRUE(bus->closeSpi("screen") == Status::Ok);
  TEST_ASSERT_NOT_NULL(bus->spi());
  TEST_ASSERT_TRUE(bus->closeSpi("radio") == Status::Ok);
  TEST_ASSERT_NULL(bus->spi());
  TEST_ASSERT_FALSE(sys.pc().spi.open());
  TEST_ASSERT_NULL(pins->ownerOf(18));
  TEST_ASSERT_TRUE(bus->closeSpi("radio") == Status::NotFound);
}

// ---------------------------------------------------------------- pulses and edges

void test_a_pulse_is_sent_and_timed() {
  OnlineSystem sys(false);
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_TRUE(pins->claim(30, hal::PinMode::Output, "sonar", "TRIG") == Status::Ok);
  TEST_ASSERT_TRUE(pins->claim(31, hal::PinMode::Input, "sonar", "ECHO") == Status::Ok);

  // Only who has the pin, and only the right kind of pin.
  TEST_ASSERT_TRUE(pins->pulseOut(30, 10, "other") == Status::Denied);
  TEST_ASSERT_TRUE(pins->pulseOut(30, 5000, "sonar") == Status::Invalid);
  TEST_ASSERT_TRUE(pins->pulseOut(31, 10, "sonar") == Status::Invalid);
  TEST_ASSERT_TRUE(pins->pulseWatch(30, "sonar") == Status::Invalid);
  TEST_ASSERT_TRUE(pins->pulseOut(30, 10, "sonar") == Status::Ok);
  bool high = true;
  pins->read(30, high);
  TEST_ASSERT_FALSE(high);   // the pulse has ended

  uint32_t us = 0;
  TEST_ASSERT_TRUE(pins->pulseWatch(31, "sonar") == Status::Ok);
  TEST_ASSERT_TRUE(pins->pulseWatch(31, "sonar") == Status::Ok);   // already watched
  TEST_ASSERT_FALSE(pins->pulseTake(31, us));
  // The echo: high for two milliseconds.
  sys.pc().gpio.setInput(31, hal::PinLevel::High);
  TEST_ASSERT_FALSE(pins->pulseTake(31, us));   // not ended yet
  sys.tb.clock.now += 2;
  sys.pc().gpio.setInput(31, hal::PinLevel::Low);
  TEST_ASSERT_TRUE(pins->pulseTake(31, us));
  TEST_ASSERT_EQUAL_UINT32(2000, us);
  TEST_ASSERT_FALSE(pins->pulseTake(31, us));   // told once

  // The release of the pin ends the watch.
  TEST_ASSERT_TRUE(pins->release(31, "sonar") == Status::Ok);
  sys.pc().gpio.setInput(31, hal::PinLevel::High);
  sys.pc().gpio.setInput(31, hal::PinLevel::Low);
  TEST_ASSERT_FALSE(pins->pulseTake(31, us));
}

void test_edges_are_counted_between_two_rounds() {
  OnlineSystem sys(false);
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_TRUE(pins->claim(33, hal::PinMode::InputPullUp, "door", "REED") == Status::Ok);
  TEST_ASSERT_TRUE(pins->edgeWatch(33, hal::Edge::Falling, "other") == Status::Denied);
  TEST_ASSERT_TRUE(pins->edgeWatch(33, hal::Edge::Falling, "door") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));

  // The magnet passes twice, each time for less than a round of the system.
  for (int i = 0; i < 2; ++i) {
    sys.pc().gpio.setInput(33, hal::PinLevel::Low);
    sys.pc().gpio.setInput(33, hal::PinLevel::High);
  }
  TEST_ASSERT_EQUAL_UINT32(2, pins->edgeTake(33));
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));

  TEST_ASSERT_TRUE(pins->release(33, "door") == Status::Ok);
  sys.pc().gpio.setInput(33, hal::PinLevel::Low);
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));
}

void test_an_edge_counts_when_its_level_lasts() {
  OnlineSystem sys(false);
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_TRUE(pins->claim(33, hal::PinMode::InputPullUp, "door", "REED") == Status::Ok);
  TEST_ASSERT_TRUE(pins->edgeWatch(33, hal::Edge::Change, "door", 3000) == Status::Invalid);
  TEST_ASSERT_TRUE(pins->edgeWatch(33, hal::Edge::Falling, "door", 3000) == Status::Ok);

  // Spikes: the wire goes low and comes back within the same millisecond. Not edges.
  for (int i = 0; i < 5; ++i) {
    sys.pc().gpio.setInput(33, hal::PinLevel::Low);
    sys.pc().gpio.setInput(33, hal::PinLevel::High);
  }
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));

  // The contact closes for 20 ms and opens between two rounds: counted when it opens.
  sys.pc().gpio.setInput(33, hal::PinLevel::Low);
  sys.tb.clock.now += 20;
  sys.pc().gpio.setInput(33, hal::PinLevel::High);
  TEST_ASSERT_EQUAL_UINT32(1, pins->edgeTake(33));

  // It closes and stays closed: counted while it is, once, and not again when it opens.
  sys.pc().gpio.setInput(33, hal::PinLevel::Low);
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));   // not long enough yet
  sys.tb.clock.now += 10;
  TEST_ASSERT_EQUAL_UINT32(1, pins->edgeTake(33));
  sys.tb.clock.now += 500;
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));
  sys.pc().gpio.setInput(33, hal::PinLevel::High);
  TEST_ASSERT_EQUAL_UINT32(0, pins->edgeTake(33));
}

// ---------------------------------------------------------------- worker

namespace {

int g_ran = 0;
int g_order[8];
std::atomic<int> g_threaded{0};
std::atomic<bool> g_otherThread{false};
std::thread::id g_caller;

}  // namespace

void test_jobs_run_one_for_each_round() {
  Worker worker;
  worker.begin(nullptr);   // a board without tasks
  g_ran = 0;
  TEST_ASSERT_FALSE(worker.threaded());
  TEST_ASSERT_TRUE(worker.post([]() { g_order[g_ran++] = 1; }) == Status::Ok);
  TEST_ASSERT_TRUE(worker.post([]() { g_order[g_ran++] = 2; }) == Status::Ok);
  TEST_ASSERT_TRUE(worker.post([]() { g_order[g_ran++] = 3; }) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(3, worker.waiting());
  TEST_ASSERT_EQUAL_INT(0, g_ran);

  worker.poll();
  TEST_ASSERT_EQUAL_INT(1, g_ran);
  TEST_ASSERT_EQUAL_UINT(2, worker.waiting());
  worker.poll();
  worker.poll();
  worker.poll();   // nothing left
  TEST_ASSERT_EQUAL_INT(3, g_ran);
  TEST_ASSERT_EQUAL_INT(1, g_order[0]);
  TEST_ASSERT_EQUAL_INT(2, g_order[1]);
  TEST_ASSERT_EQUAL_INT(3, g_order[2]);
  TEST_ASSERT_EQUAL_UINT32(3, worker.done());
  TEST_ASSERT_FALSE(worker.busy());
}

void test_the_queue_of_the_jobs_has_an_end() {
  Worker worker;
  worker.begin(nullptr);
  g_ran = 0;
  for (int i = 0; i < LMP_WORKER_QUEUE; ++i) {
    TEST_ASSERT_TRUE(worker.post([]() { ++g_ran; }) == Status::Ok);
  }
  TEST_ASSERT_TRUE(worker.post([]() { ++g_ran; }) == Status::Busy);
  TEST_ASSERT_TRUE(worker.post(Worker::Job()) == Status::Invalid);
  worker.poll();
  TEST_ASSERT_TRUE(worker.post([]() { ++g_ran; }) == Status::Ok);
  for (int i = 0; i < 10; ++i) worker.poll();
  TEST_ASSERT_EQUAL_INT(LMP_WORKER_QUEUE + 1, g_ran);
}

void test_jobs_run_in_their_own_task() {
  // The task lives as long as the program: what it uses must do the same.
  static hal::native::Rtos rtos;
  static Worker worker;
  worker.begin(&rtos);
  worker.setInline(false);   // the tests before asked for the other way
  g_threaded = 0;
  g_otherThread = false;
  g_caller = std::this_thread::get_id();
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(worker.post([]() {
      if (std::this_thread::get_id() != g_caller) g_otherThread = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ++g_threaded;
    }) == Status::Ok);
  }
  TEST_ASSERT_TRUE(worker.threaded());
  worker.poll();   // does nothing here: the jobs are not of this task
  for (int i = 0; i < 400 && g_threaded < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  TEST_ASSERT_EQUAL_INT(3, g_threaded.load());
  TEST_ASSERT_TRUE(g_otherThread.load());
  for (int i = 0; i < 100 && worker.done() < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  TEST_ASSERT_EQUAL_UINT32(3, worker.done());
}

// ---------------------------------------------------------------- commands that end later

void test_a_command_that_ends_later_has_a_reference() {
  OnlineSystem sys(false);
  sys.pc().net.addNetwork(kHome, kHomePassword);
  Json<512> first;
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("sys.net", "scan", StringView(), ArgsForm::Json,
                                                  CmdSource::Local, first.w) == Status::Pending);
  const JsonValue a = first.parse();
  TEST_ASSERT_TRUE(a["status"].stringEquals("pending"));
  const uint32_t ref = static_cast<uint32_t>(a["ref"].asInt());
  TEST_ASSERT_TRUE(ref != 0);

  // Who asks again while it runs gets the same reference.
  Json<512> again;
  sys.kernel().commands().answer("sys.net", "scan", StringView(), ArgsForm::Json,
                                 CmdSource::Local, again.w);
  TEST_ASSERT_EQUAL_UINT32(ref, static_cast<uint32_t>(again.parse()["ref"].asInt()));

  sys.advance(1000);
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);

  // A command that ends at once has none.
  Json<512> plain;
  sys.kernel().commands().answer("sys.config", "set", StringView("{\"lang\":\"it\"}"),
                                 ArgsForm::Json, CmdSource::Local, plain.w);
  TEST_ASSERT_FALSE(plain.parse()["ref"].valid());

  // The next one has another reference, and the change of network tells its end too.
  uint32_t join = 0;
  TEST_ASSERT_TRUE(sys.command("sys.net", "join",
                               "{\"ssid\":\"Home\",\"psw\":\"home-password\"}", nullptr,
                               &join) == Status::Pending);
  TEST_ASSERT_TRUE(join != 0 && join != ref);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.done(join, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);

  uint32_t wrong = 0;
  TEST_ASSERT_TRUE(sys.command("sys.net", "join", "{\"ssid\":\"Nowhere\",\"psw\":\"12345678\"}",
                               nullptr, &wrong) == Status::Pending);
  sys.advance(20000);
  TEST_ASSERT_TRUE(sys.done(wrong, outcome));
  TEST_ASSERT_TRUE(outcome == Status::NotFound);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_bus_is_opened_and_shared);
  RUN_TEST(test_a_bus_that_cannot_be_opened);
  RUN_TEST(test_chips_are_found_on_a_bus);
  RUN_TEST(test_a_scan_ends_when_the_bus_closes);
  RUN_TEST(test_more_chips_than_the_list_holds);
  RUN_TEST(test_a_board_without_buses);
  RUN_TEST(test_the_spi_bus_is_opened_and_shared);
  RUN_TEST(test_a_pulse_is_sent_and_timed);
  RUN_TEST(test_edges_are_counted_between_two_rounds);
  RUN_TEST(test_an_edge_counts_when_its_level_lasts);
  RUN_TEST(test_jobs_run_one_for_each_round);
  RUN_TEST(test_the_queue_of_the_jobs_has_an_end);
  RUN_TEST(test_jobs_run_in_their_own_task);
  RUN_TEST(test_a_command_that_ends_later_has_a_reference);
  return UNITY_END();
}
