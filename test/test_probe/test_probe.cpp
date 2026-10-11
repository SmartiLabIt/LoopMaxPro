// Tests on the PC of sys.probe, the questions to the network: ping, ports, the packet that
// wakes a PC, who is in the network, what is announced with mDNS and SSDP. The network is the
// one the test decides (hal/native/native_probe.h).
#include "../support/online_system.h"

#include <string>

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

const hal::Ip4 kNas = hal::Ip4::of(192, 168, 1, 20);
const hal::Ip4 kPc = hal::Ip4::of(192, 168, 1, 30);
const uint8_t kNasMac[6] = {0x00, 0x11, 0x32, 0xAA, 0xBB, 0xCC};
const uint8_t kPcMac[6] = {0x3C, 0x7C, 0x3F, 0x01, 0x02, 0x03};

IProbeService& probeOf(OnlineSystem& sys) { return *sys.kernel().api<IProbeService>(); }

// Asks and lets the time pass until the answer, which is copied out of done.
struct Asked {
  bool came = false;
  ProbeAnswer answer;
  std::vector<ProbeHost> hosts;
  std::vector<hal::MdnsHit> services;
  std::vector<hal::SsdpHit> devices;
};

Asked askAndWait(OnlineSystem& sys, const ProbeAsk& what) {
  static Asked got;
  got = Asked();
  TEST_ASSERT_TRUE(probeOf(sys).ask(what, [](const ProbeAnswer& a) {
    got.came = true;
    got.answer = a;
    got.hosts.assign(a.hosts.begin(), a.hosts.end());
    got.services.assign(a.services.begin(), a.services.end());
    got.devices.assign(a.devices.begin(), a.devices.end());
  }) == Status::Ok);
  for (int i = 0; i < 20 && !got.came; ++i) sys.advance(50);
  TEST_ASSERT_TRUE(got.came);
  return got;
}

}  // namespace

void test_texts_of_addresses() {
  hal::Ip4 ip;
  TEST_ASSERT_TRUE(hal::Ip4::parse("192.168.1.20", ip));
  TEST_ASSERT_TRUE(ip == kNas);
  TEST_ASSERT_FALSE(hal::Ip4::parse("192.168.1", ip));
  TEST_ASSERT_FALSE(hal::Ip4::parse("192.168.1.256", ip));
  TEST_ASSERT_FALSE(hal::Ip4::parse("192.168.1.20 ", ip));
  TEST_ASSERT_FALSE(hal::Ip4::parse("a.b.c.d", ip));

  uint8_t mac[6];
  TEST_ASSERT_TRUE(parseMac("00:11:32:aa:bb:cc", mac));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kNasMac, mac, 6);
  TEST_ASSERT_TRUE(parseMac("00-11-32-AA-BB-CC", mac));
  TEST_ASSERT_TRUE(parseMac("001132AABBCC", mac));
  TEST_ASSERT_FALSE(parseMac("00:11:32:AA:BB", mac));
  TEST_ASSERT_FALSE(parseMac("00:11:32:AA:BB:CG", mac));
  char text[18];
  macToString(kNasMac, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("00:11:32:AA:BB:CC", text);

  TEST_ASSERT_TRUE(broadcastOf(kNas, hal::Ip4::of(255, 255, 255, 0)) == hal::Ip4::of(192, 168, 1, 255));
  TEST_ASSERT_TRUE(broadcastOf(kNas, hal::Ip4::of(255, 255, 0, 0)) == hal::Ip4::of(192, 168, 255, 255));
}

void test_a_ping_and_a_device_that_does_not_answer() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac, 4);
  ProbeAsk what;
  what.kind = ProbeKind::Ping;
  what.ip = kNas;
  Asked got = askAndWait(sys, what);
  TEST_ASSERT_TRUE(got.answer.result == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(4, got.answer.rttMs);

  what.ip = kPc;
  got = askAndWait(sys, what);
  TEST_ASSERT_TRUE(got.answer.result == Status::Timeout);

  Doc doc(sys.kernel(), "sys.probe");
  TEST_ASSERT_TRUE(doc.v["last"]["what"].stringEquals("ping"));
  TEST_ASSERT_TRUE(doc.v["last"]["host"].stringEquals("192.168.1.30"));
  TEST_ASSERT_TRUE(doc.v["last"]["result"].stringEquals("timeout"));
  TEST_ASSERT_EQUAL_INT64(0, doc.v["failures"].asInt(-1));   // no answer is an answer
}

void test_a_port_open_refused_or_nobody() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac);
  sys.pc().probe.openPort(kNas, 445, 7);
  ProbeAsk what;
  what.kind = ProbeKind::Tcp;
  what.ip = kNas;
  what.port = 445;
  Asked got = askAndWait(sys, what);
  TEST_ASSERT_TRUE(got.answer.result == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(7, got.answer.rttMs);
  what.port = 3389;
  TEST_ASSERT_TRUE(askAndWait(sys, what).answer.result == Status::Denied);
  what.ip = kPc;
  TEST_ASSERT_TRUE(askAndWait(sys, what).answer.result == Status::Timeout);
}

void test_the_packet_that_wakes_a_pc() {
  OnlineSystem sys;
  ProbeAsk what;
  what.kind = ProbeKind::Wake;
  memcpy(what.mac, kPcMac, 6);
  TEST_ASSERT_TRUE(askAndWait(sys, what).answer.result == Status::Ok);
  const auto& sent = sys.pc().probe.sent();
  TEST_ASSERT_EQUAL_UINT(3, sent.size());   // three times: a lost datagram is not asked again
  // To the broadcast of the network of the device, port 9.
  char to[16];
  sent[0].ip.toString(to, sizeof(to));
  TEST_ASSERT_EQUAL_STRING("192.168.1.255", to);
  TEST_ASSERT_EQUAL_UINT16(9, sent[0].port);
  TEST_ASSERT_EQUAL_UINT(102, sent[0].data.size());
  for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, sent[0].data[i]);
  for (int r = 0; r < 16; ++r) TEST_ASSERT_EQUAL_UINT8_ARRAY(kPcMac, &sent[0].data[6 + r * 6], 6);

  // A MAC of nothing is not a PC.
  ProbeAsk none;
  none.kind = ProbeKind::Wake;
  TEST_ASSERT_TRUE(probeOf(sys).ask(none, ProbeDone()) == Status::Invalid);
}

void test_who_is_in_the_network() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac);
  sys.pc().probe.addHost(kPc, kPcMac, -1);   // a PC that does not answer the ping: ARP finds it
  ProbeAsk what;
  what.kind = ProbeKind::Sweep;
  what.ip = hal::Ip4::of(192, 168, 1, 1);
  what.count = 254;
  what.timeoutMs = 300;
  const Asked got = askAndWait(sys, what);
  TEST_ASSERT_TRUE(got.answer.result == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(2, got.hosts.size());
  TEST_ASSERT_TRUE(got.hosts[0].ip == kNas);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kNasMac, got.hosts[0].mac, 6);
  TEST_ASSERT_TRUE(got.hosts[1].ip == kPc);
  Doc doc(sys.kernel(), "sys.probe");
  TEST_ASSERT_EQUAL_INT64(2, doc.v["last"]["found"].asInt());

  // A sweep of more than a small network is not asked.
  what.count = 600;
  TEST_ASSERT_TRUE(probeOf(sys).ask(what, ProbeDone()) == Status::Invalid);
}

void test_what_is_announced() {
  OnlineSystem sys;
  hal::MdnsHit printer;
  snprintf(printer.instance, sizeof(printer.instance), "Office printer");
  snprintf(printer.host, sizeof(printer.host), "printer");
  printer.ip = hal::Ip4::of(192, 168, 1, 40);
  printer.port = 631;
  sys.pc().probe.addMdns("_ipp", "_tcp", printer);
  ProbeAsk what;
  what.kind = ProbeKind::Mdns;
  what.service = "_ipp";
  Asked got = askAndWait(sys, what);
  TEST_ASSERT_EQUAL_UINT(1, got.services.size());
  TEST_ASSERT_EQUAL_STRING("Office printer", got.services[0].instance);
  TEST_ASSERT_EQUAL_UINT16(631, got.services[0].port);
  what.service = "_http";
  TEST_ASSERT_EQUAL_UINT(0, askAndWait(sys, what).services.size());

  // A device answers SSDP once for each thing it is: kept once, with its name.
  hal::SsdpHit tv;
  tv.ip = hal::Ip4::of(192, 168, 1, 50);
  snprintf(tv.st, sizeof(tv.st), "upnp:rootdevice");
  sys.pc().probe.addSsdp(tv);
  snprintf(tv.server, sizeof(tv.server), "Linux/4.9 UPnP/1.0 TV/2.0");
  snprintf(tv.st, sizeof(tv.st), "urn:schemas-upnp-org:device:MediaRenderer:1");
  sys.pc().probe.addSsdp(tv);
  ProbeAsk upnp;
  upnp.kind = ProbeKind::Ssdp;
  got = askAndWait(sys, upnp);
  TEST_ASSERT_EQUAL_UINT(1, got.devices.size());
  TEST_ASSERT_EQUAL_STRING("Linux/4.9 UPnP/1.0 TV/2.0", got.devices[0].server);
}

// Who is told can ask again at once, from inside the answer: the new question has room of its
// own, not the one of the answer that is being read (a search of ExpNet crashed on it).
void test_asking_again_from_inside_an_answer() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac);
  hal::MdnsHit printer;
  snprintf(printer.host, sizeof(printer.host), "printer");
  printer.ip = hal::Ip4::of(192, 168, 1, 40);
  sys.pc().probe.addMdns("_ipp", "_tcp", printer);
  static OnlineSystem* self = nullptr;
  static size_t hosts = 0, services = 0;
  static std::string name;
  self = &sys;
  hosts = services = 0;
  name.clear();
  ProbeAsk sweep;
  sweep.kind = ProbeKind::Sweep;
  sweep.ip = hal::Ip4::of(192, 168, 1, 1);
  sweep.count = 254;
  TEST_ASSERT_TRUE(probeOf(sys).ask(sweep, [](const ProbeAnswer& a) {
    hosts = a.hosts.size();
    ProbeAsk mdns;
    mdns.kind = ProbeKind::Mdns;
    mdns.service = "_ipp";
    TEST_ASSERT_TRUE(probeOf(*self).ask(mdns, [](const ProbeAnswer& b) {
      services = b.services.size();
      if (services) name = b.services[0].host;
    }) == Status::Ok);
    // The answer is still whole after the new question left.
    TEST_ASSERT_TRUE(a.hosts[0].ip == kNas);
  }) == Status::Ok);
  for (int i = 0; i < 10; ++i) sys.advance(50);
  TEST_ASSERT_EQUAL_UINT(1, hosts);
  TEST_ASSERT_EQUAL_UINT(1, services);
  TEST_ASSERT_EQUAL_STRING("printer", name.c_str());
}

void test_questions_wait_their_turn() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac);
  static int answers = 0;
  answers = 0;
  ProbeAsk what;
  what.kind = ProbeKind::Ping;
  what.ip = kNas;
  // On the PC the task of the service runs inside the round: the queue fills.
  size_t taken = 0;
  for (int i = 0; i < LMP_PROBE_QUEUE + 4; ++i) {
    if (probeOf(sys).ask(what, [](const ProbeAnswer&) { ++answers; }) == Status::Ok) ++taken;
  }
  TEST_ASSERT_TRUE(taken >= LMP_PROBE_QUEUE);
  TEST_ASSERT_TRUE(taken < static_cast<size_t>(LMP_PROBE_QUEUE + 4));   // Busy beyond the queue
  for (int i = 0; i < 40; ++i) sys.advance(50);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(taken), answers);
}

void test_the_commands_of_the_console() {
  OnlineSystem sys;
  sys.pc().probe.addHost(kNas, kNasMac, 3);
  sys.pc().probe.openPort(kNas, 80);
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.probe", "ping", "{\"host\":\"192.168.1.20\"}", nullptr, &ref) ==
                   Status::Pending);
  sys.advance(200);
  Status outcome = Status::Io;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  Doc doc(sys.kernel(), "sys.probe");
  TEST_ASSERT_EQUAL_INT64(3, doc.v["last"]["rttMs"].asInt());

  TEST_ASSERT_TRUE(sys.command("sys.probe", "tcp", "{\"host\":\"192.168.1.20\",\"port\":22}", nullptr,
                               &ref) == Status::Pending);
  sys.advance(200);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Denied);   // the device is there and refuses that port

  TEST_ASSERT_TRUE(sys.command("sys.probe", "wake", "{\"mac\":\"3c:7c:3f:01:02:03\"}", nullptr, &ref) ==
                   Status::Pending);
  sys.advance(200);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(3, sys.pc().probe.sent().size());

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.probe", "ping", "{\"host\":\"home\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("host", field);
  TEST_ASSERT_TRUE(sys.command("sys.probe", "wake", "{\"mac\":\"3c:7c:3f:01:02:zz\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("mac", field);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_texts_of_addresses);
  RUN_TEST(test_a_ping_and_a_device_that_does_not_answer);
  RUN_TEST(test_a_port_open_refused_or_nobody);
  RUN_TEST(test_the_packet_that_wakes_a_pc);
  RUN_TEST(test_who_is_in_the_network);
  RUN_TEST(test_what_is_announced);
  RUN_TEST(test_asking_again_from_inside_an_answer);
  RUN_TEST(test_questions_wait_their_turn);
  RUN_TEST(test_the_commands_of_the_console);
  return UNITY_END();
}
