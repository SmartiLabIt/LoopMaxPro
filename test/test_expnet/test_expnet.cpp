// Tests on the PC of ExpNet, the free edition of Networks Explorer: the monitor of the
// services with its history, events and messages, the PC to wake, the search in the network, the
// live times. The tools and the commands of the chat are of the full version. The network is
// the one the test decides (hal/native/native_probe.h).
#include "../support/online_system.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../src/modules/expnet/src/history.h"
#include "../../src/modules/expnet/src/wan.h"

using namespace lmp;
using namespace lmp::test;

lmp::sdk::Module& lmp_module_expnet();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kTarget = "mod.expnet";
const hal::Ip4 kNas = hal::Ip4::of(192, 168, 1, 20);
const hal::Ip4 kPc = hal::Ip4::of(192, 168, 1, 30);
const uint8_t kNasMac[6] = {0x00, 0x11, 0x32, 0xAA, 0xBB, 0xCC};
const uint8_t kPcMac[6] = {0x3C, 0x7C, 0x3F, 0x01, 0x02, 0x03};
constexpr const char* kTelegram = "https://api.telegram.org/*";

struct Box {
  OnlineSystem sys{true, nullptr, 1500, {&lmp_module_expnet()}};
  Box() {
    TEST_ASSERT_TRUE(sys.command(kTarget, "config.reset", "{}") == Status::Ok);
    TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"unix\":1790000000}") == Status::Ok);
    sys.pc().httpClient.when(kTelegram, 200, "{\"ok\":true}");
  }
  hal::native::Probe& net() { return sys.pc().probe; }
  // A command that answers with a result: the result, as JSON.
  std::string result(const char* name, const char* args) {
    Json<8192> out;
    out.w.beginObject();
    CmdContext ctx;
    ctx.source = CmdSource::Local;
    ctx.result = &out.w;
    const Status s = sys.kernel().commands().execute(kTarget, name, JsonValue::parse(args), ctx);
    out.w.endObject();
    TEST_ASSERT_TRUE_MESSAGE(s == Status::Ok, name);
    return out.text();
  }
  // The messages sent to the chat: the questions of the bot are not messages.
  size_t telegrams() {
    size_t n = 0;
    for (const auto& s : sys.pc().httpClient.seen()) {
      if (s.url.find("/sendMessage") != std::string::npos) ++n;
    }
    return n;
  }
  std::string lastTelegram() {
    std::string body;
    for (const auto& s : sys.pc().httpClient.seen()) {
      if (s.url.find("/sendMessage") != std::string::npos) body = s.body;
    }
    return body;
  }
};

bool has(const std::string& text, const char* piece) { return text.find(piece) != std::string::npos; }
size_t times(const std::string& text, const char* piece) {
  size_t n = 0;
  for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + 1)) ++n;
  return n;
}

}  // namespace

// ---------------------------------------------------------------- the history, alone

void test_the_history_in_blocks_of_15_minutes() {
  uint8_t bytes[expnet::kHistoryBytes];
  expnet::HistoryFile f(bytes);
  f.clear();
  const uint32_t now = 2000000;
  expnet::Block b;
  b.total = 4;
  b.ok = 3;
  b.avgMs = 12;
  f.put(now - 1, b);
  b.ok = 4;
  b.avgMs = 20;
  f.put(now, b);
  TEST_ASSERT_EQUAL_UINT8(3, f.get(now - 1, now).ok);
  TEST_ASSERT_EQUAL_UINT16(20, f.get(now, now).avgMs);
  // Day: 7 of 8 answered, average weighed on the answers.
  const expnet::Summary day = expnet::summarize(f, now, expnet::kBlocksDay);
  TEST_ASSERT_EQUAL_INT32(875, day.uptime());
  TEST_ASSERT_EQUAL_UINT32(16, day.avgMs);   // (3*12 + 4*20) / 7
  // A week later the old blocks are no more what they were: a gap is emptied.
  f.put(now + expnet::kBlocks + 5, b);
  TEST_ASSERT_EQUAL_UINT8(0, f.get(now, now + expnet::kBlocks + 5).total);
  // Nothing known: -1, not zero.
  expnet::HistoryFile empty(bytes);
  empty.clear();
  TEST_ASSERT_EQUAL_INT32(-1, expnet::summarize(empty, now, expnet::kBlocksDay).uptime());
}

// ---------------------------------------------------------------- the monitor

void test_a_service_watched_falls_and_comes_back() {
  Box box;
  box.net().addHost(kNas, kNasMac, 3);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set",
                                   "{\"telegramToken\":\"123:abc\",\"telegramChat\":\"42\"}") == Status::Ok);
  const std::string added = box.result("service.add",
                                       "{\"name\":\"NAS\",\"check\":\"ping\",\"host\":\"192.168.1.20\",\"everySec\":10}");
  TEST_ASSERT_TRUE(has(added, "\"id\":1"));
  box.sys.advance(1000);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_TRUE(doc.v["services"].at(0)["up"].asBool(false));
    TEST_ASSERT_EQUAL_INT64(3, doc.v["services"].at(0)["ms"].asInt());
  }
  // One check lost: still up. Two in a row: down, and a message.
  box.net().setPing(kNas, -1);
  box.sys.advance(10500);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_TRUE(doc.v["services"].at(0)["up"].asBool(false));
  }
  TEST_ASSERT_EQUAL_UINT(0, box.telegrams());
  box.sys.advance(10500);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_FALSE(doc.v["services"].at(0)["up"].asBool(true));
  }
  TEST_ASSERT_EQUAL_UINT(1, box.telegrams());
  TEST_ASSERT_TRUE(has(box.lastTelegram(), "NAS"));
  TEST_ASSERT_TRUE(has(box.lastTelegram(), "\"chat_id\":\"42\""));
  // Back: a message that says it.
  box.net().setPing(kNas, 5);
  box.sys.advance(10500);
  TEST_ASSERT_EQUAL_UINT(2, box.telegrams());
  const std::string events = box.result("events", "{}");
  TEST_ASSERT_TRUE(has(events, "\"count\":2"));
  TEST_ASSERT_TRUE(has(events, "\"name\":\"NAS\""));
  TEST_ASSERT_TRUE(box.sys.logHas("NAS is down"));
  TEST_ASSERT_TRUE(box.sys.logHas("NAS is back"));
}

void test_a_port_and_a_page_as_services() {
  Box box;
  box.net().addHost(kNas, kNasMac);
  box.net().openPort(kNas, 445, 6);
  box.sys.pc().httpClient.when("http://192.168.1.20:5000/*", 200, "<html></html>");
  box.result("service.add", "{\"name\":\"SMB\",\"check\":\"tcp\",\"host\":\"192.168.1.20\",\"port\":445}");
  box.result("service.add", "{\"name\":\"DSM\",\"check\":\"http\",\"host\":\"192.168.1.20\",\"port\":5000}");
  box.result("service.add", "{\"name\":\"SSH\",\"check\":\"tcp\",\"host\":\"192.168.1.20\",\"port\":22}");
  for (int i = 0; i < 4; ++i) box.sys.advance(1000);
  box.sys.advance(61000);   // the second check of each: the closed port is down
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_TRUE(doc.v["services"].at(0)["up"].asBool(false));
  TEST_ASSERT_EQUAL_INT64(6, doc.v["services"].at(0)["ms"].asInt());
  TEST_ASSERT_TRUE(doc.v["services"].at(1)["up"].asBool(false));
  TEST_ASSERT_TRUE(doc.v["services"].at(1)["path"].stringEquals("/"));
  TEST_ASSERT_FALSE(doc.v["services"].at(2)["up"].asBool(true));   // a port refused
  // A tcp service without its port is not a service.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "service.add",
                                   "{\"name\":\"X\",\"check\":\"tcp\",\"host\":\"192.168.1.20\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("port", field);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "service.add",
                                   "{\"name\":\"X\",\"check\":\"ping\",\"host\":\"nas.local\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("host", field);
}

void test_without_its_network_nothing_is_checked() {
  Box box;
  box.net().addHost(kNas, kNasMac, 3);
  box.result("service.add", "{\"name\":\"NAS\",\"check\":\"ping\",\"host\":\"192.168.1.20\",\"everySec\":10}");
  box.sys.advance(1000);
  const unsigned before = box.net().calls();
  TEST_ASSERT_TRUE(before > 0);
  // The device loses its network: the NAS is out of sight, not down.
  box.sys.pc().net.removeNetwork(kHome);
  box.sys.pc().net.dropSta();
  box.sys.advance(40000);
  TEST_ASSERT_EQUAL_UINT(before, box.net().calls());
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_FALSE(doc.v["online"].asBool(true));
    TEST_ASSERT_TRUE(doc.v["services"].at(0)["up"].asBool(false));
  }
  // Back on its network: the checks start again, and nothing fell meanwhile.
  box.sys.pc().net.addNetwork(kHome, kHomePassword);
  for (int i = 0; i < 300 && !box.sys.app.net().connected(); ++i) box.sys.advance(1000);
  TEST_ASSERT_TRUE(box.sys.app.net().connected());
  box.sys.advance(3000);
  TEST_ASSERT_TRUE(box.net().calls() > before);
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_TRUE(doc.v["online"].asBool(false));
  TEST_ASSERT_TRUE(doc.v["services"].at(0)["up"].asBool(false));
  TEST_ASSERT_TRUE(has(box.result("events", "{}"), "\"count\":0"));
}

void test_the_history_of_a_service_and_its_day() {
  Box box;
  box.net().addHost(kNas, kNasMac, 8);
  box.result("service.add", "{\"name\":\"NAS\",\"check\":\"ping\",\"host\":\"192.168.1.20\",\"everySec\":60}");
  // Twenty minutes: a block of 15 minutes closes and goes to its file. The clock of the PC
  // moves its hour only when the test says.
  for (int i = 0; i < 20; ++i) {
    box.sys.tb.clock.unix += 60;
    box.sys.advance(60000);
  }
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_EQUAL_INT64(1000, doc.v["services"].at(0)["up24"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(8, doc.v["services"].at(0)["avg24"].asInt(-1));
  const std::string day = box.result("history", "{\"id\":1}");
  TEST_ASSERT_TRUE(has(day, "\"range\":\"day\""));
  TEST_ASSERT_TRUE(has(day, "\"stepSec\":900"));
  const std::string week = box.result("history", "{\"id\":1,\"range\":\"week\"}");
  TEST_ASSERT_TRUE(has(week, "\"stepSec\":3600"));
  TEST_ASSERT_TRUE(box.sys.kernel().board().fs->exists("/expnet/h1.bin"));
}

// ---------------------------------------------------------------- the PC

void test_a_pc_is_woken_and_found_on() {
  Box box;
  const std::string added = box.result(
      "pc.add", "{\"name\":\"PC Gaming\",\"mac\":\"3c:7c:3f:01:02:03\",\"host\":\"192.168.1.30\",\"port\":3389}");
  TEST_ASSERT_TRUE(has(added, "\"id\":1"));
  box.sys.advance(3000);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_FALSE(doc.v["pcs"].at(0)["on"].asBool(true));   // nobody answers on that port
  }
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "pc.wake", "{\"id\":1}") == Status::Ok);
  box.sys.advance(200);
  const auto& sent = box.net().sent();
  TEST_ASSERT_EQUAL_UINT(3, sent.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPcMac, &sent[0].data[6], 6);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_TRUE(doc.v["pcs"].at(0)["wakingSec"].valid());
  }
  // It wakes: the port refuses at first, which is a PC that answers.
  box.net().addHost(kPc, kPcMac, -1);
  box.sys.advance(6000);
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_TRUE(doc.v["pcs"].at(0)["on"].asBool(false));
  TEST_ASSERT_FALSE(doc.v["pcs"].at(0)["wakingSec"].valid());
  TEST_ASSERT_TRUE(doc.v["pcs"].at(0)["wokeSec"].valid());
  TEST_ASSERT_FALSE(doc.v["pcs"].at(0)["canOff"].asBool(true));
  // A PC without a webhook cannot be turned off.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "pc.off", "{\"id\":1}") == Status::Unsupported);
}

void test_a_pc_turned_off_with_its_webhook() {
  Box box;
  box.sys.pc().httpClient.when("http://192.168.1.10:8123/api/webhook/*", 200, "");
  box.result("pc.add", "{\"name\":\"Rig\",\"mac\":\"3c7c3f010203\","
                       "\"offUrl\":\"http://192.168.1.10:8123/api/webhook/rig_off\"}");
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "pc.off", "{\"id\":1}", nullptr, &ref) == Status::Pending);
  box.sys.advance(500);
  Status outcome = Status::Io;
  TEST_ASSERT_TRUE(box.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  bool posted = false;
  for (const auto& s : box.sys.pc().httpClient.seen()) {
    if (s.url.find("rig_off") != std::string::npos) posted = s.method == "POST";
  }
  TEST_ASSERT_TRUE(posted);
}

// ---------------------------------------------------------------- the network

void test_who_is_in_the_network_with_the_loopmaxpro_in_sight() {
  Box box;
  box.net().addHost(kNas, kNasMac, 2);
  box.net().addHost(kPc, kPcMac, -1);
  hal::MdnsHit printer;
  snprintf(printer.host, sizeof(printer.host), "printer");
  printer.ip = hal::Ip4::of(192, 168, 1, 40);
  printer.port = 631;
  box.net().addMdns("_ipp", "_tcp", printer);
  hal::SsdpHit tv;
  tv.ip = hal::Ip4::of(192, 168, 1, 60);   // the device itself is at .50
  snprintf(tv.server, sizeof(tv.server), "Linux UPnP/1.0 TV");
  box.net().addSsdp(tv);
  hal::NetPeer zappy;
  zappy.ip = kNas;
  snprintf(zappy.name, sizeof(zappy.name), "Office Plugs");
  snprintf(zappy.host, sizeof(zappy.host), "office-plugs");
  snprintf(zappy.title, sizeof(zappy.title), "Zappy 2");
  snprintf(zappy.fw, sizeof(zappy.fw), "1.0.8");
  zappy.port = 80;
  box.sys.pc().net.addPeer(zappy);

  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "scan", "{}", nullptr, &ref) == Status::Pending);
  for (int i = 0; i < 40; ++i) box.sys.advance(100);
  Status outcome = Status::Io;
  TEST_ASSERT_TRUE(box.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("done"));
    // The four found, and the device itself.
    TEST_ASSERT_EQUAL_INT64(5, doc.v["scan"]["found"].asInt());
    TEST_ASSERT_EQUAL_INT64(0, doc.v["scan"]["new"].asInt(-1));   // the first search: nobody is new
  }
  const std::string page = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(page, "\"ip\":\"192.168.1.20\""));
  TEST_ASSERT_TRUE(has(page, "\"mac\":\"00:11:32:AA:BB:CC\""));
  TEST_ASSERT_TRUE(has(page, "\"pingMs\":2"));
  TEST_ASSERT_TRUE(has(page, "\"loopmax\":{\"name\":\"Office Plugs\",\"title\":\"Zappy 2\""));
  TEST_ASSERT_TRUE(has(page, "\"name\":\"printer\""));
  TEST_ASSERT_TRUE(has(page, "\"services\":[\"printer\"]"));
  TEST_ASSERT_TRUE(has(page, "\"server\":\"Linux UPnP/1.0 TV\""));
  TEST_ASSERT_TRUE(has(page, "\"self\":true"));
  TEST_ASSERT_FALSE(has(page, "\"new\":true"));
  // In the order of the addresses.
  TEST_ASSERT_TRUE(page.find("192.168.1.20") < page.find("192.168.1.30"));

  // A name of yours for one of them, kept with it.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias",
                                   "{\"ip\":\"192.168.1.20\",\"mac\":\"00:11:32:aa:bb:cc\",\"alias\":\"Home NAS\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(has(box.result("hosts", "{}"), "\"alias\":\"Home NAS\""));
}

void scanNow(Box& box) {
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "scan", "{}", nullptr, &ref) == Status::Pending);
  // The search ends when it has asked everybody: a large network takes longer.
  Status outcome = Status::Io;
  bool over = false;
  for (int i = 0; i < 600 && !over; ++i) {
    box.sys.advance(100);
    over = box.sys.done(ref, outcome);
  }
  TEST_ASSERT_TRUE(over);
}

void test_a_device_never_seen_before_is_told() {
  Box box;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set",
                                   "{\"telegramToken\":\"123:abc\",\"telegramChat\":\"42\",\"botCommands\":false}") ==
                   Status::Ok);
  box.net().addHost(kNas, kNasMac, 2);
  scanNow(box);
  TEST_ASSERT_EQUAL_UINT(0, box.telegrams());
  TEST_ASSERT_TRUE(has(box.result("events", "{}"), "\"count\":0"));
  // A phone arrives, with a MAC of its own making.
  const uint8_t phone[6] = {0x5A, 0x11, 0x22, 0x33, 0x44, 0x55};
  box.net().addHost(hal::Ip4::of(192, 168, 1, 77), phone, 30);
  box.sys.tb.clock.unix += 900;
  scanNow(box);
  box.sys.advance(500);
  TEST_ASSERT_EQUAL_UINT(1, box.telegrams());
  TEST_ASSERT_TRUE(has(box.lastTelegram(), "192.168.1.77"));
  const std::string events = box.result("events", "{}");
  TEST_ASSERT_TRUE(has(events, "\"kind\":\"new\""));
  TEST_ASSERT_TRUE(has(events, "\"mac\":\"5A:11:22:33:44:55\""));
  const std::string page = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(page, "\"new\":true"));
  TEST_ASSERT_TRUE(has(page, "\"random\":true"));
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_EQUAL_INT64(1, doc.v["scan"]["new"].asInt());
  // Gone from the network: still in the list, out of sight.
  box.net().removeHost(hal::Ip4::of(192, 168, 1, 77));
  scanNow(box);
  TEST_ASSERT_TRUE(has(box.result("hosts", "{}"), "\"ip\":\"192.168.1.77\",\"mac\":\"5A:11:22:33:44:55\",\"random\":true,\"online\":false"));
}

void test_the_list_is_kept_and_searched_by_itself() {
  Box box;
  box.net().addHost(kNas, kNasMac, 2);
  // A minute after the network the first search goes by itself.
  for (int i = 0; i < 70; ++i) box.sys.advance(1000);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("done"));
    TEST_ASSERT_EQUAL_INT64(2, doc.v["scan"]["found"].asInt());
    TEST_ASSERT_EQUAL_INT64(15, doc.v["scan"]["everyMin"].asInt());
  }
  TEST_ASSERT_TRUE(box.sys.kernel().board().fs->exists("/expnet/hosts.bin"));
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"expnet\"}") == Status::Ok);
  box.sys.advance(500);
  TEST_ASSERT_TRUE(has(box.result("hosts", "{}"), "\"ip\":\"192.168.1.20\""));
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_EQUAL_INT64(2, doc.v["scan"]["found"].asInt());
}

void test_a_large_network_makes_the_list_grow() {
  Box box;
  for (uint8_t i = 0; i < 40; ++i) {
    const uint8_t mac[6] = {0x00, 0x11, 0x32, 0x00, 0x00, static_cast<uint8_t>(i)};
    box.net().addHost(hal::Ip4::of(192, 168, 1, static_cast<uint8_t>(100 + i)), mac, 2);
  }
  scanNow(box);
  {
    Doc doc(box.sys.kernel(), kTarget);
    TEST_ASSERT_EQUAL_INT64(41, doc.v["scan"]["found"].asInt());   // and the device itself
  }
  std::string all;
  for (int from = 0; from < 64; from += 16) all += box.result("hosts", ("{\"from\":" + std::to_string(from) + "}").c_str());
  TEST_ASSERT_TRUE(has(all, "\"ip\":\"192.168.1.100\""));
  TEST_ASSERT_TRUE(has(all, "\"ip\":\"192.168.1.139\""));
}

// A device asleep misses the ARP of a search and answers mDNS all the same: one place in the list,
// with its MAC. Where its web page is comes from mDNS: _http, else Home Assistant.
void test_a_device_asleep_at_the_arp_stays_one() {
  Box box;
  const hal::Ip4 printer = hal::Ip4::of(192, 168, 1, 40);
  const uint8_t printerMac[6] = {0x38, 0x1A, 0x52, 0x01, 0x02, 0x03};
  box.net().addHost(printer, printerMac, 5);
  hal::MdnsHit page;
  snprintf(page.host, sizeof(page.host), "printer");
  page.ip = printer;
  page.port = 8080;
  box.net().addMdns("_http", "_tcp", page);
  box.net().addHost(kPc, kPcMac, 3);
  hal::MdnsHit assistant;
  snprintf(assistant.host, sizeof(assistant.host), "homeassistant");
  assistant.ip = kPc;
  assistant.port = 8123;
  box.net().addMdns("_home-assistant", "_tcp", assistant);
  scanNow(box);
  std::string hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"webPort\":8080"));
  TEST_ASSERT_TRUE(has(hosts, "\"webPort\":8123"));
  // A search that starts keeps the times of the last one, until it has its own.
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "scan", "{}", nullptr, &ref) == Status::Pending);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"pingMs\":5"));
  TEST_ASSERT_TRUE(has(hosts, "\"pingMs\":3"));
  for (int i = 0; i < 40; ++i) box.sys.advance(100);
  box.net().removeHost(printer);
  box.sys.tb.clock.unix += 900;
  scanNow(box);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_EQUAL_UINT(1, times(hosts, "\"ip\":\"192.168.1.40\""));
  // Found by mDNS only: in the network, without a time of its own.
  TEST_ASSERT_TRUE(has(hosts, "\"ip\":\"192.168.1.40\",\"mac\":\"38:1A:52:01:02:03\",\"online\":true,\"name\""));
  TEST_ASSERT_FALSE(has(hosts, "\"new\":true"));
}

// A list of 1.0.0 with a device kept twice, with its MAC and without: read, and after a search one
// place for it, with the name its owner gave to either.
void test_a_list_of_1_0_0_is_read_and_mended() {
  Box box;
  const hal::Ip4 printer = hal::Ip4::of(192, 168, 1, 40);
  const uint8_t printerMac[6] = {0x38, 0x1A, 0x52, 0x01, 0x02, 0x03};
  box.net().addHost(printer, printerMac, 5);
  scanNow(box);
  // The file as 1.0.0 wrote it: a head of 12 bytes, then 104 for each device.
  const uint32_t seen = 1790000000;
  auto host = [&](const uint8_t* mac, const char* alias, const std::string& server) {
    std::string b(104, '\0');
    memcpy(&b[0], printer.b, 4);
    if (mac) memcpy(&b[4], mac, 6);
    b[10] = b[11] = '\xff';   // no ping
    b[14] = 1 | 4;            // in the network, there at the first search
    memcpy(&b[16], &seen, 4);
    memcpy(&b[20], &seen, 4);
    memcpy(&b[52], alias, strlen(alias));
    memcpy(&b[72], server.data(), server.size());
    return b;
  };
  std::string file("DBI1\x02\x00\x01\x00", 8);
  file.append(reinterpret_cast<const char*>(&seen), 4);
  file += host(printerMac, "", "");
  file += host(nullptr, "Printer", std::string(31, 'S'));
  hal::IFile* f = box.sys.kernel().board().fs->open("/expnet/hosts.bin", hal::OpenMode::Write);
  TEST_ASSERT_NOT_NULL(f);
  f->write(reinterpret_cast<const uint8_t*>(file.data()), file.size());
  f->close();
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"expnet\"}") == Status::Ok);
  box.sys.advance(500);
  std::string hosts = box.result("hosts", "{}");
  TEST_ASSERT_EQUAL_UINT(2, times(hosts, "\"ip\":\"192.168.1.40\""));
  TEST_ASSERT_TRUE(has(hosts, ("\"server\":\"" + std::string(27, 'S') + "\"").c_str()));
  box.sys.tb.clock.unix += 900;
  scanNow(box);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_EQUAL_UINT(1, times(hosts, "\"ip\":\"192.168.1.40\""));
  TEST_ASSERT_TRUE(has(hosts, "\"mac\":\"38:1A:52:01:02:03\""));
  TEST_ASSERT_TRUE(has(hosts, "\"alias\":\"Printer\""));
  TEST_ASSERT_FALSE(has(hosts, "\"new\":true"));
}

// ---------------------------------------------------------------- live

void test_live_times_and_the_traffic_of_the_router() {
  Box box;
  const uint8_t routerMac[6] = {0x3C, 0xA6, 0x2F, 0x01, 0x02, 0x03};
  box.net().addHost(hal::Ip4::of(192, 168, 1, 1), routerMac, 2);
  box.net().addHost(hal::Ip4::of(1, 1, 1, 1), nullptr, 15);
  hal::SsdpHit gateway;
  gateway.ip = hal::Ip4::of(192, 168, 1, 1);
  snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:38400/description.xml");
  box.net().addSsdp(gateway);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.1.1:38400/description.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><serviceId>urn:upnp-org:serviceId:WANCommonIFC1"
            "</serviceId><controlURL>/WANCommonInterfaceConfig:1</controlURL></service></serviceList>"
            "</device></root>");
  // A router as the one of the bench: the bytes received stuck at the top, the packets moving.
  const char* kControl = "http://192.168.1.1:38400/WANCommonInterfaceConfig:1";
  const auto counters = [&](uint32_t sent, uint32_t packets) {
    char body[400];
    snprintf(body, sizeof(body),
             "<NewTotalBytesReceived>2147483647</NewTotalBytesReceived><NewTotalBytesSent>%u</NewTotalBytesSent>"
             "<NewTotalPacketsReceived>%u</NewTotalPacketsReceived>"
             "<NewLayer1DownstreamMaxBitRate>56124000</NewLayer1DownstreamMaxBitRate>"
             "<NewLayer1UpstreamMaxBitRate>20983000</NewLayer1UpstreamMaxBitRate>",
             sent, packets);
    http.when(kControl, 200, body);
  };
  counters(1000000, 5000);
  uint32_t sent = 1000000, packets = 5000;
  for (int i = 0; i < 12; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    sent += 125000;      // 1 Mbit/s up
    packets += 893;      // about 10 Mbit/s down
    counters(sent, packets);
  }
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"gateway\":\"192.168.1.1\""));
  TEST_ASSERT_TRUE(has(r, "\"gwMs\":[2,"));
  TEST_ASSERT_TRUE(has(r, "\"netMs\":[15,"));
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.1.1\""));
  TEST_ASSERT_TRUE(has(r, "\"maxDown\":56124000,\"maxUp\":20983000,\"estimate\":true"));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  const lmp::JsonValue up = v["router"]["up"];
  TEST_ASSERT_TRUE(up.size() >= 2);
  TEST_ASSERT_INT64_WITHIN(150, 1000, up.at(up.size() - 1).asInt());
  const lmp::JsonValue down = v["router"]["down"];
  TEST_ASSERT_INT64_WITHIN(1500, 10000, down.at(down.size() - 1).asInt());
}

// The live samples keep their pace whatever the ticks of the module (200 ms): with 250 ms they
// were one every 400 (the S3, 2026-10-06), and the graphs of the page went back at every answer.
void test_the_live_samples_keep_their_pace() {
  Box box;
  box.net().addHost(hal::Ip4::of(192, 168, 1, 1), nullptr, 2);
  int64_t every = 0, age = 0;
  const auto seqOf = [&box, &every, &age]() {
    const std::string r = box.result("live", "{}");
    const JsonValue v = JsonValue::parse(StringView(r.c_str(), r.size()));
    every = v["everyMs"].asInt();
    age = v["ageMs"].asInt(-1000);
    return v["seq"].asInt();
  };
  const int64_t first = seqOf();
  for (int i = 0; i < 100; ++i) box.sys.advance(100);
  const int64_t taken = seqOf() - first;
  TEST_ASSERT_TRUE(every > 0);
  TEST_ASSERT_INT64_WITHIN(2, 10000 / every, taken);
  // How long ago the newest was due: at most a step and a tick.
  TEST_ASSERT_TRUE(age >= -200 && age <= every + 200);
}

void test_a_router_without_upnp_says_so() {
  Box box;
  box.result("live", "{}");
  for (int i = 0; i < 6; ++i) box.sys.advance(1000);
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"none\"}"));
}

// ---------------------------------------------------------------- Telegram

void test_quiet_hours_and_the_test_button() {
  Box box;
  // From midnight to 23 the messages are silent: the hour of the test is within.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set",
                                   "{\"telegramToken\":\"123:abc\",\"telegramChat\":\"42\",\"botCommands\":false,"
                                   "\"quietFrom\":0,\"quietTo\":23}") == Status::Ok);
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "notify.test", "{}", nullptr, &ref) == Status::Pending);
  box.sys.advance(500);
  Status outcome = Status::Io;
  TEST_ASSERT_TRUE(box.sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  TEST_ASSERT_TRUE(has(box.lastTelegram(), "\"disable_notification\":true"));
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_TRUE(doc.v["notify"]["quiet"].asBool(false));
  // Without the bot set, no test.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"telegramToken\":\"\"}") == Status::Ok);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "notify.test", "{}") == Status::Denied);
}

// The tools and the commands of the chat are of the full version: here there are none, and the
// switch of the commands stays off, whatever is sent.
void test_the_tools_and_the_chat_are_of_the_full_version() {
  Box box;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "tool.start", "{\"kind\":\"ping\",\"target\":\"192.168.1.20\"}") ==
                   Status::NotFound);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set",
                                   "{\"telegramToken\":\"123:abc\",\"telegramChat\":\"42\",\"botCommands\":true}") ==
                   Status::Ok);
  auto& http = box.sys.pc().httpClient;
  http.when("https://api.telegram.org/bot123:abc/getUpdates*", 200,
            "{\"ok\":true,\"result\":[{\"update_id\":100,\"message\":{\"chat\":{\"id\":42},\"text\":\"/status\"}}]}");
  for (int i = 0; i < 25; ++i) box.sys.advance(1000);
  for (const auto& s : http.seen()) TEST_ASSERT_TRUE(s.url.find("getUpdates") == std::string::npos);
  TEST_ASSERT_EQUAL_UINT(0, box.telegrams());
  // The messages go all the same.
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "notify.test", "{}", nullptr, &ref) == Status::Pending);
  box.sys.advance(500);
  TEST_ASSERT_EQUAL_UINT(1, box.telegrams());
}

void test_the_commands_fit_in_a_board() {
  Box box;
  // The boards of tier 2 have room for 96 commands, every service and module together.
  TEST_ASSERT_TRUE(box.sys.kernel().commands().commandCount() <= 96);
}

// ---------------------------------------------------------------- kept, and seen from outside

void test_the_lists_are_kept() {
  Box box;
  box.result("service.add", "{\"name\":\"Router\",\"check\":\"ping\",\"host\":\"192.168.1.1\"}");
  box.result("pc.add", "{\"name\":\"NAS\",\"mac\":\"00:11:32:aa:bb:cc\"}");
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"expnet\"}") == Status::Ok);
  box.sys.advance(500);
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_TRUE(doc.v["services"].at(0)["name"].stringEquals("Router"));
  TEST_ASSERT_TRUE(doc.v["pcs"].at(0)["mac"].stringEquals("00:11:32:AA:BB:CC"));
  // Removed: gone, also after a start.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "service.remove", "{\"id\":1}") == Status::Ok);
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"expnet\"}") == Status::Ok);
  box.sys.advance(500);
  Doc after(box.sys.kernel(), kTarget);
  TEST_ASSERT_EQUAL_UINT(0, after.v["services"].size());
}

void test_what_a_home_system_sees() {
  Box box;
  box.net().addHost(kNas, kNasMac, 3);
  box.result("service.add", "{\"name\":\"NAS\",\"check\":\"ping\",\"host\":\"192.168.1.20\"}");
  box.result("pc.add", "{\"name\":\"PC\",\"mac\":\"3c:7c:3f:01:02:03\",\"host\":\"192.168.1.30\"}");
  box.sys.advance(3000);
  const std::string things = box.result("things", "{}");
  TEST_ASSERT_TRUE(has(things, "\"s1\""));
  TEST_ASSERT_TRUE(has(things, "connectivity"));
  TEST_ASSERT_TRUE(has(things, "\"w1\""));
  TEST_ASSERT_TRUE(has(things, "\"p1\""));
  // Pressing the button of the PC wakes it.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "thing.set", "{\"thing\":\"w1\",\"value\":\"PRESS\"}") == Status::Ok);
  box.sys.advance(200);
  TEST_ASSERT_EQUAL_UINT(3, box.net().sent().size());
}

// What a device is: SSDP asked one kind at a time, the LoopMaxPro devices that are products of
// SmartiLab, and a type the owner gives a device, kept.
void test_what_a_device_is() {
  Box box;
  const hal::Ip4 router = hal::Ip4::of(192, 168, 1, 1);
  const hal::Ip4 repeater = hal::Ip4::of(192, 168, 1, 113);
  const hal::Ip4 tv = hal::Ip4::of(192, 168, 1, 70);
  const uint8_t routerMac[6] = {0xC8, 0x54, 0x4B, 0x01, 0x02, 0x03};
  const uint8_t repeaterMac[6] = {0x04, 0x95, 0xE6, 0x01, 0x02, 0x03};
  const uint8_t tvMac[6] = {0x8C, 0x79, 0xF5, 0x01, 0x02, 0x03};
  box.net().addHost(router, routerMac, 2);
  box.net().addHost(repeater, repeaterMac, 4);
  box.net().addHost(tv, tvMac, 9);
  box.net().addHost(kNas, kNasMac, 3);
  auto ssdp = [&](const hal::Ip4& ip, const char* st, const char* server) {
    hal::SsdpHit hit;
    hit.ip = ip;
    snprintf(hit.st, sizeof(hit.st), "%s", st);
    snprintf(hit.server, sizeof(hit.server), "%s", server);
    box.net().addSsdp(hit);
  };
  ssdp(router, "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "POSIX UPnP/1.0");
  ssdp(repeater, "urn:schemas-wifialliance-org:device:WFADevice:1", "1.0 ");
  ssdp(tv, "urn:dial-multiscreen-org:service:dial:1", "Linux/4.4 UPnP/1.0");
  ssdp(tv, "urn:schemas-upnp-org:device:MediaRenderer:1", "Linux/4.4 UPnP/1.0");
  hal::NetPeer zappy;
  zappy.ip = kNas;
  snprintf(zappy.name, sizeof(zappy.name), "Office Plugs");
  snprintf(zappy.host, sizeof(zappy.host), "office-plugs");
  snprintf(zappy.title, sizeof(zappy.title), "Zappy 2");
  snprintf(zappy.code, sizeof(zappy.code), "demo_w32");
  zappy.port = 80;
  box.sys.pc().net.addPeer(zappy);
  scanNow(box);
  std::string hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"server\":\"POSIX UPnP/1.0\",\"firstUnix\""));
  TEST_ASSERT_TRUE(has(hosts, "\"upnp\":[\"igd\"]"));
  TEST_ASSERT_TRUE(has(hosts, "\"upnp\":[\"wps\"]"));
  TEST_ASSERT_TRUE(has(hosts, "\"upnp\":[\"dial\",\"media\"]"));
  TEST_ASSERT_FALSE(has(hosts, "\"services\":[]"));
  TEST_ASSERT_TRUE(has(hosts, "\"title\":\"Zappy 2\""));
  TEST_ASSERT_TRUE(has(hosts, "\"pro\":true"));
  // The owner says what it is; a type not in the list is refused.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias", "{\"ip\":\"192.168.1.70\",\"mac\":\"8c:79:f5:01:02:03\",\"type\":\"tv\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias", "{\"ip\":\"192.168.1.70\",\"type\":\"toaster\"}") ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias", "{\"ip\":\"192.168.1.70\"}") == Status::Invalid);
  TEST_ASSERT_TRUE(has(box.result("hosts", "{}"), "\"type\":\"tv\""));
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"expnet\"}") == Status::Ok);
  box.sys.advance(500);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"type\":\"tv\""));
  TEST_ASSERT_TRUE(has(hosts, "\"upnp\":[\"dial\",\"media\"]"));
  // Given back to the page, which guesses it again; the name stays.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias", "{\"ip\":\"192.168.1.70\",\"mac\":\"8c:79:f5:01:02:03\",\"alias\":\"Living room\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "host.alias", "{\"ip\":\"192.168.1.70\",\"mac\":\"8c:79:f5:01:02:03\",\"type\":\"\"}") ==
                   Status::Ok);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_FALSE(has(hosts, "\"type\":"));
  TEST_ASSERT_TRUE(has(hosts, "\"alias\":\"Living room\""));
  // A LoopMaxPro device out of sight is still one, of a product.
  box.sys.pc().net.clearPeers();
  box.net().removeHost(kNas);
  box.sys.tb.clock.unix += 900;
  scanNow(box);
  hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"system\":\"pro\""));
  TEST_ASSERT_FALSE(has(hosts, "\"title\":\"Zappy 2\""));
}

// Cameras and recorders by their typical ports: the video (RTSP) and the port of the platform, and
// for a Hikvision recorder 9010 and 9020. The phones are not asked.
void test_cameras_and_recorders_by_their_ports() {
  Box box;
  const hal::Ip4 nvr = hal::Ip4::of(192, 168, 1, 200);
  const hal::Ip4 camera = hal::Ip4::of(192, 168, 1, 202);
  const hal::Ip4 other = hal::Ip4::of(192, 168, 1, 201);
  const hal::Ip4 phone = hal::Ip4::of(192, 168, 1, 77);
  const uint8_t nvrMac[6] = {0xC0, 0x6D, 0xED, 0x01, 0x02, 0x03};
  const uint8_t cameraMac[6] = {0xE0, 0xCA, 0x3C, 0x01, 0x02, 0x03};
  const uint8_t otherMac[6] = {0x00, 0x46, 0xB8, 0x01, 0x02, 0x03};
  const uint8_t phoneMac[6] = {0x5A, 0x11, 0x22, 0x33, 0x44, 0x55};
  box.net().addHost(nvr, nvrMac, 6);
  box.net().addHost(camera, cameraMac, 6);
  box.net().addHost(other, otherMac, 6);
  box.net().addHost(phone, phoneMac, 30);
  for (uint16_t p : {80, 554, 8000, 9010, 9020}) box.net().openPort(nvr, p);
  for (uint16_t p : {80, 443, 554, 7681, 8000, 8443}) box.net().openPort(camera, p);
  for (uint16_t p : {80, 554, 34567}) box.net().openPort(other, p);
  box.net().openPort(phone, 554);
  scanNow(box);
  const std::string hosts = box.result("hosts", "{}");
  TEST_ASSERT_TRUE(has(hosts, "\"ports\":[554,8000,9010,9020]"));
  TEST_ASSERT_TRUE(has(hosts, "\"ports\":[554,8000,7681]"));
  TEST_ASSERT_TRUE(has(hosts, "\"ports\":[554,34567]"));
  TEST_ASSERT_EQUAL_UINT(3, times(hosts, "\"ports\":"));
}

// The extended table of the makers, a page at a time, on the boards with room.
void test_the_makers_a_page_at_a_time() {
  Box box;
  std::string all;
  int pages = 0;
  for (int from = 0; from >= 0 && pages < 100; ++pages) {
    const std::string page = box.result("makers", ("{\"from\":" + std::to_string(from) + "}").c_str());
    all += page;
    const size_t at = page.find("\"next\":");
    from = at == std::string::npos ? -1 : std::atoi(page.c_str() + at + 7);
  }
  TEST_ASSERT_TRUE(pages > 5);
  TEST_ASSERT_TRUE(has(all, "[\"Oppo\",\"phone\","));
  TEST_ASSERT_TRUE(has(all, "[\"Apple\",\"\","));
  TEST_ASSERT_TRUE(all.size() > 20000);
}

// A router whose counter of the bytes sent stops at its top (the router of the owner, 2026-10-07):
// the upload told by the packets sent, without the acknowledgements of a download.
void test_the_upload_of_a_router_that_stops_counting_bytes() {
  Box box;
  box.net().addHost(hal::Ip4::of(192, 168, 1, 1), nullptr, 2);
  hal::SsdpHit gateway;
  gateway.ip = hal::Ip4::of(192, 168, 1, 1);
  snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:38400/description.xml");
  box.net().addSsdp(gateway);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.1.1:38400/description.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><serviceId>urn:upnp-org:serviceId:WANCommonIFC1"
            "</serviceId><controlURL>/WANCommonInterfaceConfig:1</controlURL></service></serviceList>"
            "</device></root>");
  uint32_t received = 400000000, packetsIn = 60000000, packetsOut = 40000000;
  const auto counters = [&]() {
    char body[500];
    snprintf(body, sizeof(body),
             "<NewTotalBytesReceived>%u</NewTotalBytesReceived><NewTotalBytesSent>2147483647</NewTotalBytesSent>"
             "<NewTotalPacketsReceived>%u</NewTotalPacketsReceived><NewTotalPacketsSent>%u</NewTotalPacketsSent>"
             "<NewLayer1DownstreamMaxBitRate>56124000</NewLayer1DownstreamMaxBitRate>"
             "<NewLayer1UpstreamMaxBitRate>20983000</NewLayer1UpstreamMaxBitRate>",
             received, packetsIn, packetsOut);
    http.when("http://192.168.1.1:38400/WANCommonInterfaceConfig:1", 200, body);
  };
  const auto last = [](const lmp::JsonValue& list) { return list.at(list.size() - 1).asInt(); };
  // An upload of 20 Mbit/s: full packets out, the acknowledgements in.
  counters();
  for (int i = 0; i < 8; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    packetsOut += 1786;
    received += 60000;
    packetsIn += 900;
    counters();
  }
  std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"estimateUp\":true"));
  const lmp::JsonValue upload = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_INT64_WITHIN(2000, 20000, last(upload["router"]["up"]));
  // A download of 50 Mbit/s: the acknowledgements it makes the router send are not an upload.
  for (int i = 0; i < 8; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    received += 6250000;
    packetsIn += 4464;
    packetsOut += 2232;
    counters();
  }
  r = box.result("live", "{}");
  const lmp::JsonValue download = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_INT64_WITHIN(5000, 50000, last(download["router"]["down"]));
  TEST_ASSERT_TRUE(last(download["router"]["up"]) < 2000);
}

// Two questions a round to the router, all of them and the line once a minute: four every second
// were too many for the router of the owner, which stopped answering UPnP (2026-10-07).
void test_the_router_is_asked_little() {
  Box box;
  box.net().addHost(hal::Ip4::of(192, 168, 1, 1), nullptr, 2);
  hal::SsdpHit gateway;
  gateway.ip = hal::Ip4::of(192, 168, 1, 1);
  snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:38400/description.xml");
  box.net().addSsdp(gateway);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.1.1:38400/description.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><serviceId>urn:upnp-org:serviceId:WANCommonIFC1"
            "</serviceId><controlURL>/WANCommonInterfaceConfig:1</controlURL></service></serviceList>"
            "</device></root>");
  // As the router of the owner: the bytes sent stuck at the top.
  uint32_t received = 400000000, packetsOut = 40000000;
  const auto counters = [&]() {
    char body[500];
    snprintf(body, sizeof(body),
             "<NewTotalBytesReceived>%u</NewTotalBytesReceived><NewTotalBytesSent>2147483647</NewTotalBytesSent>"
             "<NewTotalPacketsReceived>60000000</NewTotalPacketsReceived><NewTotalPacketsSent>%u</NewTotalPacketsSent>"
             "<NewLayer1DownstreamMaxBitRate>56124000</NewLayer1DownstreamMaxBitRate>"
             "<NewLayer1UpstreamMaxBitRate>20983000</NewLayer1UpstreamMaxBitRate>",
             received, packetsOut);
    http.when("http://192.168.1.1:38400/WANCommonInterfaceConfig:1", 200, body);
  };
  const auto asked = [&http](const char* action) {
    int n = 0;
    for (const auto& s : http.seen()) n += s.body.find(std::string("<u:") + action + " ") != std::string::npos;
    return n;
  };
  counters();
  for (int i = 0; i < 30; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    received += 1250000;
    packetsOut += 300;
    counters();
  }
  // Every round the bytes received and the packets sent, the counter of the bytes sent being at
  // its top; the rest once.
  const int rounds = asked("GetTotalBytesReceived");
  TEST_ASSERT_TRUE(rounds >= 10);
  TEST_ASSERT_EQUAL_INT(rounds, asked("GetTotalPacketsSent"));
  TEST_ASSERT_EQUAL_INT(1, asked("GetTotalBytesSent"));
  TEST_ASSERT_EQUAL_INT(1, asked("GetTotalPacketsReceived"));
  TEST_ASSERT_EQUAL_INT(1, asked("GetCommonLinkProperties"));
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"estimateUp\":true"));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  const lmp::JsonValue down = v["router"]["down"];
  TEST_ASSERT_INT64_WITHIN(1000, 10000, down.at(down.size() - 1).asInt());
  // A minute later, all of them again.
  for (int i = 0; i < 35; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    counters();
  }
  TEST_ASSERT_EQUAL_INT(2, asked("GetCommonLinkProperties"));
  TEST_ASSERT_EQUAL_INT(2, asked("GetTotalBytesSent"));
}

// Nobody looks and nobody is told: the search waits for the first command, which starts it at
// once (the owner, 2026-10-07: a tool nobody looks at does not keep the network busy). With a
// message for the new devices it goes on: it is what tells of them.
void test_the_search_rests_when_nobody_looks() {
  for (int told = 0; told < 2; ++told) {
    Box box;
    if (told) {
      TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"telegramToken\":\"123:abc\",\"telegramChat\":\"42\"}") ==
                       Status::Ok);
    }
    box.net().addHost(hal::Ip4::of(192, 168, 1, 1), nullptr, 2);
    box.net().addHost(kNas, kNasMac, 3);
    // The half hour after the start: searches every 15 minutes.
    for (int i = 0; i < 31 * 60; ++i) box.sys.advance(1000);
    const unsigned before = box.net().calls();
    TEST_ASSERT_TRUE(before > 0);
    for (int i = 0; i < 20 * 60; ++i) box.sys.advance(1000);
    if (told) {
      TEST_ASSERT_TRUE(box.net().calls() > before);
      continue;
    }
    TEST_ASSERT_EQUAL_UINT(before, box.net().calls());
    // The first look: a search at once, its time being past.
    box.result("hosts", "{}");
    for (int i = 0; i < 20; ++i) box.sys.advance(1000);
    TEST_ASSERT_TRUE(box.net().calls() > before);
  }
}

// With the setting empty only the router of the network is read: an access point that answers as
// a router, with the address of a network of its own (a Tenda, 2026-10-07), is not asked.
void test_only_the_router_of_the_network_by_itself() {
  Box box;
  box.net().addHost(hal::Ip4::of(192, 168, 1, 1), nullptr, 2);
  hal::SsdpHit ap;
  ap.ip = hal::Ip4::of(192, 168, 1, 113);
  snprintf(ap.st, sizeof(ap.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(ap.location, sizeof(ap.location), "http://192.168.0.1:5500/rootDesc.xml");
  box.net().addSsdp(ap);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.0.1:5500/rootDesc.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service>"
            "</serviceList></device></root>");
  for (int i = 0; i < 6; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"none\"}"));
  for (const auto& s : http.seen()) TEST_ASSERT_TRUE(s.url.find("192.168.0.1") == std::string::npos);
}

// A UPnP device chosen by the owner, in another subnet: asked directly, its counters read; one
// that does not answer is said so.
void test_a_upnp_device_chosen_by_hand() {
  Box box;
  box.net().onUdp([](hal::Ip4 ip, uint16_t port, const std::vector<uint8_t>& q) {
    const std::string ask(q.begin(), q.end());
    if (!(ip == hal::Ip4::of(10, 0, 0, 1)) || port != 1900 || ask.find("M-SEARCH") != 0) return std::vector<uint8_t>();
    const std::string a = "HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                          "Location: http://10.0.0.1:5000/rootDesc.xml\r\n\r\n";
    return std::vector<uint8_t>(a.begin(), a.end());
  });
  auto& http = box.sys.pc().httpClient;
  http.when("http://10.0.0.1:5000/rootDesc.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service>"
            "</serviceList></device></root>");
  http.when("http://10.0.0.1:5000/ctl/CmnIfCfg", 200,
            "<NewTotalBytesReceived>1000</NewTotalBytesReceived><NewTotalBytesSent>500</NewTotalBytesSent>"
            "<NewTotalPacketsReceived>10</NewTotalPacketsReceived><NewTotalPacketsSent>5</NewTotalPacketsSent>"
            "<NewLayer1DownstreamMaxBitRate>100000000</NewLayer1DownstreamMaxBitRate>"
            "<NewLayer1UpstreamMaxBitRate>20000000</NewLayer1UpstreamMaxBitRate>");
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"upnpHost\":\"10.0.0.1\"}") == Status::Ok);
  for (int i = 0; i < 6; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"manual\":\"10.0.0.1\",\"ip\":\"10.0.0.1\""));
  TEST_ASSERT_TRUE(has(r, "\"maxDown\":100000000"));
  // A device that does not answer UPnP: said so, not searched elsewhere.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"upnpHost\":\"10.0.0.9\"}") == Status::Ok);
  for (int i = 0; i < 8; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"none\",\"manual\":\"10.0.0.9\""));
}

// ---------------------------------------------------------------- the devices without UPnP

namespace {

const hal::Ip4 kGw = hal::Ip4::of(192, 168, 1, 1);
constexpr const char* kDishUrl = "http://192.168.100.1:9201/SpaceX.API.Device.Device/Handle";
constexpr const char* kHuaweiUrl = "http://192.168.1.1/api/monitoring/traffic-statistics";

// ---- a dish of Starlink: its answer, as a dish writes it
void wanVarint(std::string& out, uint64_t v) {
  for (; v >= 0x80; v >>= 7) out.push_back(static_cast<char>(0x80 | (v & 0x7F)));
  out.push_back(static_cast<char>(v));
}
void wanNumber(std::string& out, uint32_t field, uint64_t v) {
  wanVarint(out, (static_cast<uint64_t>(field) << 3) | 0);
  wanVarint(out, v);
}
void wanFloat(std::string& out, uint32_t field, float f) {
  wanVarint(out, (static_cast<uint64_t>(field) << 3) | 5);
  char raw[4];
  memcpy(raw, &f, 4);
  out.append(raw, 4);
}
void wanMessage(std::string& out, uint32_t field, const std::string& inner) {
  wanVarint(out, (static_cast<uint64_t>(field) << 3) | 2);
  wanVarint(out, inner.size());
  out += inner;
}
std::string wanFrame(uint8_t flags, const std::string& body) {
  std::string out(1, static_cast<char>(flags));
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(body.size() >> shift));
  return out + body;
}
std::string dishAnswer(float down, float up, float ping, float drop, float obstructed, uint32_t uptime) {
  std::string state, obstruction, status, answer;
  wanNumber(state, 1, uptime);
  wanFloat(obstruction, 1, obstructed);
  wanMessage(status, 2, state);
  wanFloat(status, 1003, drop);
  wanMessage(status, 1004, obstruction);
  wanFloat(status, 1007, down);
  wanFloat(status, 1008, up);
  wanFloat(status, 1009, ping);
  wanNumber(answer, 1, 7);
  wanNumber(answer, 3, 4);
  wanMessage(answer, 2004, status);
  return wanFrame(0, answer) + wanFrame(0x80, "grpc-status:0\r\n");
}

// ---- a router with SNMP: it answers from a table of objects, written here from the rules of
// the protocol and not with the code of the module
std::string berPiece(uint8_t tag, const std::string& body) {
  std::string out(1, static_cast<char>(tag));
  if (body.size() < 128) {
    out.push_back(static_cast<char>(body.size()));
  } else {
    out.push_back(static_cast<char>(0x82));
    out.push_back(static_cast<char>(body.size() >> 8));
    out.push_back(static_cast<char>(body.size()));
  }
  return out + body;
}
std::string berValue(uint8_t tag, uint64_t v) {
  std::string body;
  do {
    body.insert(body.begin(), static_cast<char>(v));
    v >>= 8;
  } while (v);
  if (body[0] & 0x80) body.insert(body.begin(), '\0');
  return berPiece(tag, body);
}
std::string berName(const std::vector<uint32_t>& ids) {
  std::string body(1, static_cast<char>(ids[0] * 40 + ids[1]));
  for (size_t i = 2; i < ids.size(); ++i) {
    std::string one(1, static_cast<char>(ids[i] & 0x7F));
    for (uint32_t v = ids[i] >> 7; v; v >>= 7) one.insert(one.begin(), static_cast<char>(0x80 | (v & 0x7F)));
    body += one;
  }
  return berPiece(0x06, body);
}
struct SnmpAgent {
  std::map<std::vector<uint32_t>, std::pair<uint8_t, uint64_t>> objects;
  std::string community = "public";
  unsigned asked = 0;

  std::vector<uint8_t> answer(const std::vector<uint8_t>& q) {
    using namespace expnet::wan;
    BerIn in{q.data(), q.data() + q.size()}, message{}, part{}, pdu{}, list{}, bind{}, name{};
    uint8_t tag = 0;
    if (!berNext(in, tag, message) || tag != kBerSeq || !berNext(message, tag, part)) return {};
    if (!berNext(message, tag, part) || std::string(part.at, part.end) != community) return {};
    if (!berNext(message, tag, pdu)) return {};
    const uint8_t kind = tag;
    if (!berNext(pdu, tag, part)) return {};
    const uint64_t id = berNumber(part);
    if (!berNext(pdu, tag, part) || !berNext(pdu, tag, part) || !berNext(pdu, tag, list)) return {};
    ++asked;
    std::string binds;
    while (berNext(list, tag, bind)) {
      if (!berNext(bind, tag, name)) return {};
      std::vector<uint32_t> ids{static_cast<uint32_t>(name.at[0] / 40), static_cast<uint32_t>(name.at[0] % 40)};
      uint32_t v = 0;
      for (const uint8_t* at = name.at + 1; at < name.end; ++at) {
        v = (v << 7) | (*at & 0x7F);
        if (!(*at & 0x80)) {
          ids.push_back(v);
          v = 0;
        }
      }
      const auto it = kind == kSnmpNext ? objects.upper_bound(ids) : objects.find(ids);
      // An object that is not there: said so in its place, the answer has no error.
      binds += berPiece(0x30, it == objects.end()
                                  ? berName(ids) + std::string(1, static_cast<char>(kind == kSnmpNext ? 0x82 : 0x80)) +
                                        std::string(1, '\0')
                                  : berName(it->first) + berValue(it->second.first, it->second.second));
    }
    const std::string out = berPiece(
        0x30, berValue(0x02, 1) + berPiece(0x04, community) +
                  berPiece(0xA2, berValue(0x02, id) + berValue(0x02, 0) + berValue(0x02, 0) + berPiece(0x30, binds)));
    return std::vector<uint8_t>(out.begin(), out.end());
  }
};

int64_t wanLast(const lmp::JsonValue& values) {
  return values.size() ? values.at(values.size() - 1).asInt() : -1;
}

}  // namespace

// How each device is asked and read, without the module around.
void test_the_devices_without_upnp_alone() {
  using namespace expnet::wan;
  // A dish: the traffic, and what only a dish says.
  const std::string dish = dishAnswer(25e6f, 3e6f, 38.4f, 0.02f, 0.015f, 86400);
  Starlink s;
  TEST_ASSERT_TRUE(starlinkRead(reinterpret_cast<const uint8_t*>(dish.data()), dish.size(), s));
  TEST_ASSERT_EQUAL_UINT32(25000, static_cast<uint32_t>(s.downBps / 1000));
  TEST_ASSERT_EQUAL_UINT32(3000, static_cast<uint32_t>(s.upBps / 1000));
  TEST_ASSERT_EQUAL_UINT32(38, static_cast<uint32_t>(s.pingMs));
  TEST_ASSERT_EQUAL_UINT32(20, static_cast<uint32_t>(s.drop * 1000 + 0.5f));
  TEST_ASSERT_EQUAL_UINT32(15, static_cast<uint32_t>(s.obstructed * 1000 + 0.5f));
  TEST_ASSERT_EQUAL_UINT32(86400, s.uptimeS);
  // Cut by a small buffer in its last number: what came before is read.
  TEST_ASSERT_TRUE(starlinkRead(reinterpret_cast<const uint8_t*>(dish.data()), dish.size() - 20 - 2, s));
  TEST_ASSERT_EQUAL_UINT32(25000, static_cast<uint32_t>(s.downBps / 1000));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(s.pingMs));
  // A dish that passes nothing writes no numbers: it is still a dish, with zeros.
  std::string still, quiet;
  wanMessage(still, 2, std::string("\x08\x05", 2));
  wanMessage(quiet, 2004, still);
  quiet = wanFrame(0, quiet);
  TEST_ASSERT_TRUE(starlinkRead(reinterpret_cast<const uint8_t*>(quiet.data()), quiet.size(), s));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(s.downBps));
  TEST_ASSERT_EQUAL_UINT32(5, s.uptimeS);
  // An answer that is only its end, the page of something else, nothing: not a dish.
  const std::string refusal = wanFrame(0x80, "grpc-status:12\r\n");
  TEST_ASSERT_FALSE(starlinkRead(reinterpret_cast<const uint8_t*>(refusal.data()), refusal.size(), s));
  const std::string page = "<html><body>404 Not Found</body></html>";
  TEST_ASSERT_FALSE(starlinkRead(reinterpret_cast<const uint8_t*>(page.data()), page.size(), s));
  TEST_ASSERT_FALSE(starlinkRead(nullptr, 0, s));
  TEST_ASSERT_FALSE(s.has);

  // A router of Huawei: bytes a second.
  uint32_t down = 0, up = 0;
  bool refused = true;
  TEST_ASSERT_TRUE(huaweiRead("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><CurrentConnectTime>120"
                              "</CurrentConnectTime><CurrentUpload>1</CurrentUpload><CurrentDownload>2"
                              "</CurrentDownload><CurrentDownloadRate>1250000</CurrentDownloadRate>"
                              "<CurrentUploadRate>125000</CurrentUploadRate><TotalUpload>9</TotalUpload></response>",
                              down, up, refused));
  TEST_ASSERT_FALSE(refused);
  TEST_ASSERT_EQUAL_UINT32(1250000, down);
  TEST_ASSERT_EQUAL_UINT32(125000, up);
  TEST_ASSERT_FALSE(huaweiRead("<error><code>125002</code><message></message></error>", down, up, refused));
  TEST_ASSERT_TRUE(refused);
  TEST_ASSERT_FALSE(huaweiRead("<html>the page of another router</html>", down, up, refused));
  TEST_ASSERT_FALSE(refused);
  char cookie[32];
  TEST_ASSERT_TRUE(huaweiSession("<response><SesInfo>SessionID=q1w2e3</SesInfo><TokInfo>t0k</TokInfo></response>",
                                 cookie, sizeof(cookie)));
  TEST_ASSERT_EQUAL_STRING("SessionID=q1w2e3", cookie);
  TEST_ASSERT_TRUE(huaweiSession("<response><SesInfo>q1w2e3</SesInfo></response>", cookie, sizeof(cookie)));
  TEST_ASSERT_EQUAL_STRING("SessionID=q1w2e3", cookie);
  TEST_ASSERT_FALSE(huaweiSession("<response><SesInfo>a-session-too-long-for-this-room</SesInfo></response>", cookie,
                                  sizeof(cookie)));
  TEST_ASSERT_FALSE(huaweiSession("<html></html>", cookie, sizeof(cookie)));

  // SNMP: the question the books show, the name of a system asked to "public" with number 1.
  static const uint8_t kBook[] = {0x30, 0x26, 0x02, 0x01, 0x01, 0x04, 0x06, 'p',  'u',  'b',  'l',  'i',  'c',  0xA0,
                                  0x19, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00, 0x30, 0x0E, 0x30, 0x0C,
                                  0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00, 0x05, 0x00};
  const uint16_t kSystem[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
  uint8_t room[200];
  size_t len = 0;
  Oid one = oidOf(kSystem);
  const uint8_t* ask = snmpAsk(room, sizeof(room), "public", 1, kSnmpGet, &one, 1, len);
  TEST_ASSERT_NOT_NULL(ask);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kBook), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kBook, ask, sizeof(kBook));
  TEST_ASSERT_NULL(snmpAsk(room, 20, "public", 1, kSnmpGet, &one, 1, len));
  // Three objects of a port with a large number, a number of the question over 127: an agent
  // understands them, and its answer is read. One of the three is not there.
  SnmpAgent agent;
  agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 6, 436207616}] = {0x46, 0x1234567890ull};
  agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15, 436207616}] = {0x42, 1000};
  const Oid three[] = {oidOf(kOidInHc, 436207616u), oidOf(kOidOutHc, 436207616u), oidOf(kOidMbit, 436207616u)};
  ask = snmpAsk(room, sizeof(room), "public", 70000, kSnmpGet, three, 3, len);
  TEST_ASSERT_NOT_NULL(ask);
  std::vector<uint8_t> said = agent.answer(std::vector<uint8_t>(ask, ask + len));
  SnmpValue v[3];
  uint8_t count = 0;
  TEST_ASSERT_TRUE(snmpRead(said.data(), said.size(), 70000, v, 3, count));
  TEST_ASSERT_EQUAL_UINT8(3, count);
  TEST_ASSERT_TRUE(v[0].has && v[0].number == 0x1234567890ull);
  TEST_ASSERT_FALSE(v[1].has);
  TEST_ASSERT_TRUE(v[2].has && v[2].number == 1000);
  // The answer to another question, bytes cut, another word: not read.
  TEST_ASSERT_FALSE(snmpRead(said.data(), said.size(), 70001, v, 3, count));
  TEST_ASSERT_FALSE(snmpRead(said.data(), said.size() - 3, 70000, v, 3, count));
  ask = snmpAsk(room, sizeof(room), "private", 5, kSnmpGet, three, 3, len);
  TEST_ASSERT_TRUE(agent.answer(std::vector<uint8_t>(ask, ask + len)).empty());
  // What follows a row of a table is of that table, or of the one after it.
  agent.objects[{1, 3, 6, 1, 2, 1, 4, 24, 4, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 1}] = {0x02, 3};
  Oid route = oidOf(kOidRouteCidr);
  ask = snmpAsk(room, sizeof(room), "public", 6, kSnmpNext, &route, 1, len);
  said = agent.answer(std::vector<uint8_t>(ask, ask + len));
  TEST_ASSERT_TRUE(snmpRead(said.data(), said.size(), 6, v, 3, count));
  TEST_ASSERT_TRUE(count == 1 && v[0].has && v[0].number == 3 && snmpUnder(v[0], route));
  route = oidOf(kOidRoute);
  ask = snmpAsk(room, sizeof(room), "public", 7, kSnmpNext, &route, 1, len);
  said = agent.answer(std::vector<uint8_t>(ask, ask + len));
  TEST_ASSERT_TRUE(snmpRead(said.data(), said.size(), 7, v, 3, count));
  TEST_ASSERT_TRUE(v[0].has && !snmpUnder(v[0], route));
}

// A house under Starlink: no router with UPnP, the dish at its address says the traffic.
void test_a_dish_of_starlink_says_the_traffic() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  auto& http = box.sys.pc().httpClient;
  http.when(kDishUrl, 200, dishAnswer(25e6f, 3e6f, 38.4f, 0.02f, 0.015f, 86400));
  for (int i = 0; i < 16; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.100.1\",\"via\":\"starlink\""));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_TRUE(v["router"]["down"].size() >= 2);
  TEST_ASSERT_EQUAL_INT64(25000, wanLast(v["router"]["down"]));
  TEST_ASSERT_EQUAL_INT64(3000, wanLast(v["router"]["up"]));
  // Asked as a dish wants it.
  bool asked = false;
  for (const auto& s : http.seen()) {
    if (s.url != kDishUrl) continue;
    asked = true;
    TEST_ASSERT_EQUAL_STRING("POST", s.method.c_str());
    TEST_ASSERT_EQUAL_STRING("application/grpc-web+proto", s.contentType.c_str());
    TEST_ASSERT_TRUE(s.body == std::string("\x00\x00\x00\x00\x03\xE2\x3E\x00", 8));
    TEST_ASSERT_TRUE(s.headers.size() == 1 && s.headers[0].first == "X-Grpc-Web" && s.headers[0].second == "1");
  }
  TEST_ASSERT_TRUE(asked);
  // The dish goes away: after a while nothing is read, and it is said.
  http.when(kDishUrl, 503, "");
  http.whenFails(kDishUrl, Status::Timeout);
  for (int i = 0; i < 30; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"none\""));
}

// A 4G router of Huawei that wants a session: asked for once, its cookie sent with the question.
void test_a_router_of_huawei_says_the_traffic() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  auto& http = box.sys.pc().httpClient;
  bool session = false;
  http.whenMade("http://192.168.1.1/api/webserver/SesTokInfo", 200, [&session] {
    session = true;
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><SesInfo>SessionID=q1w2e3</SesInfo>"
                       "<TokInfo>t0k</TokInfo></response>");
  });
  http.whenMade(kHuaweiUrl, 200, [&session] {
    return std::string(session ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><CurrentConnectTime>120"
                                 "</CurrentConnectTime><CurrentDownloadRate>1250000</CurrentDownloadRate>"
                                 "<CurrentUploadRate>125000</CurrentUploadRate></response>"
                               : "<?xml version=\"1.0\" encoding=\"UTF-8\"?><error><code>125002</code>"
                                 "<message></message></error>");
  });
  for (int i = 0; i < 16; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.1.1\",\"via\":\"huawei\""));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_EQUAL_INT64(10000, wanLast(v["router"]["down"]));
  TEST_ASSERT_EQUAL_INT64(1000, wanLast(v["router"]["up"]));
  bool cookie = false;
  for (const auto& s : http.seen()) {
    if (s.url == kHuaweiUrl && s.headers.size() == 1 && s.headers[0].first == "Cookie" &&
        s.headers[0].second == "SessionID=q1w2e3") {
      cookie = true;
    }
  }
  TEST_ASSERT_TRUE(cookie);
}

// A router with SNMP: its port towards Internet found in its routes, the traffic from the
// counters of 64 bit of that port, the line from its speed.
void test_a_router_with_snmp_says_the_traffic() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  SnmpAgent agent;
  // Its routes only in the second of the tables the module knows.
  agent.objects[{1, 3, 6, 1, 2, 1, 4, 24, 4, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 1}] = {0x02, 3};
  agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15, 3}] = {0x42, 1000};
  uint64_t in = 5000000000ull, out = 100000;
  const auto counters = [&agent, &in, &out]() {
    agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 6, 3}] = {0x46, in};
    agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 10, 3}] = {0x46, out};
  };
  counters();
  box.net().onUdp([&agent](hal::Ip4 ip, uint16_t port, const std::vector<uint8_t>& q) {
    return port == 161 && ip == kGw ? agent.answer(q) : std::vector<uint8_t>();
  });
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpCommunity\":\"public\"}") == Status::Ok);
  for (int i = 0; i < 24; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
    in += 1250000;    // 10 Mbit/s down
    out += 125000;    // 1 Mbit/s up
    counters();
  }
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.1.1\",\"via\":\"snmp\""));
  TEST_ASSERT_TRUE(has(r, "\"maxDown\":1000000000,\"maxUp\":1000000000"));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_TRUE(v["router"]["down"].size() >= 2);
  TEST_ASSERT_INT64_WITHIN(3000, 10000, wanLast(v["router"]["down"]));
  TEST_ASSERT_INT64_WITHIN(300, 1000, wanLast(v["router"]["up"]));
}

// A router with SNMP that does not say its routes, and has only the old counters: it is said
// that its port is wanted, and with the port in the settings the traffic is read.
void test_a_router_with_snmp_wants_its_port() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  SnmpAgent agent;
  agent.community = "house";
  agent.objects[{1, 3, 6, 1, 2, 1, 2, 2, 1, 5, 2}] = {0x42, 100000000};
  uint32_t in = 4294000000u, out = 100000;   // the counter of 32 bit goes round in the test
  const auto counters = [&agent, &in, &out]() {
    agent.objects[{1, 3, 6, 1, 2, 1, 2, 2, 1, 10, 2}] = {0x41, in};
    agent.objects[{1, 3, 6, 1, 2, 1, 2, 2, 1, 16, 2}] = {0x41, out};
  };
  counters();
  box.net().onUdp([&agent](hal::Ip4 ip, uint16_t port, const std::vector<uint8_t>& q) {
    return port == 161 && ip == kGw ? agent.answer(q) : std::vector<uint8_t>();
  });
  const auto wait = [&](int seconds) {
    for (int i = 0; i < seconds; ++i) {
      box.result("live", "{}");
      box.sys.advance(1000);
      in += 1250000;
      out += 125000;
      counters();
    }
  };
  // A word that is not the one of this router: it does not even answer.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpCommunity\":\"public\"}") == Status::Ok);
  wait(12);
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"none\"}"));
  TEST_ASSERT_EQUAL_UINT(0, agent.asked);   // asked, and silent
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpCommunity\":\"house\"}") == Status::Ok);
  wait(14);
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"none\",\"needPort\":true}"));
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpIf\":2}") == Status::Ok);
  wait(24);
  const std::string r = box.result("live", "{}");
  TEST_ASSERT_TRUE(has(r, "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.1.1\",\"via\":\"snmp\""));
  TEST_ASSERT_TRUE(has(r, "\"maxDown\":100000000"));
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_INT64_WITHIN(3000, 10000, wanLast(v["router"]["down"]));
}

// A router with UPnP is still the first choice: with one in the network nothing else is asked.
void test_with_upnp_the_other_devices_are_not_asked() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  hal::SsdpHit gateway;
  gateway.ip = kGw;
  snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:5000/rootDesc.xml");
  box.net().addSsdp(gateway);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.1.1:5000/rootDesc.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service>"
            "</serviceList></device></root>");
  http.when("http://192.168.1.1:5000/ctl/CmnIfCfg", 200,
            "<NewTotalBytesReceived>1000</NewTotalBytesReceived><NewTotalBytesSent>500</NewTotalBytesSent>"
            "<NewTotalPacketsReceived>10</NewTotalPacketsReceived><NewTotalPacketsSent>5</NewTotalPacketsSent>");
  http.when(kDishUrl, 200, dishAnswer(25e6f, 3e6f, 38.4f, 0, 0, 10));
  unsigned datagrams = 0;
  box.net().onUdp([&datagrams](hal::Ip4, uint16_t port, const std::vector<uint8_t>&) {
    if (port == 161) ++datagrams;
    return std::vector<uint8_t>();
  });
  for (int i = 0; i < 12; ++i) {
    box.result("live", "{}");
    box.sys.advance(1000);
  }
  TEST_ASSERT_TRUE(has(box.result("live", "{}"), "\"router\":{\"state\":\"ok\",\"ip\":\"192.168.1.1\",\"via\":\"upnp\""));
  for (const auto& s : http.seen()) {
    TEST_ASSERT_TRUE(s.url != kDishUrl && s.url != kHuaweiUrl);
  }
  TEST_ASSERT_EQUAL_UINT(0, datagrams);
}

// The device to read is changed while another one is being looked for: the answers of the
// search of before, which arrive later, are not taken for answers of the new one.
void test_the_device_changed_while_it_is_looked_for() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  auto& http = box.sys.pc().httpClient;
  bool session = false;
  http.whenMade("http://10.0.0.2/api/webserver/SesTokInfo", 200, [&session] {
    session = true;
    return std::string("<response><SesInfo>SessionID=q1w2e3</SesInfo><TokInfo>t0k</TokInfo></response>");
  });
  http.whenMade("http://10.0.0.2/api/monitoring/traffic-statistics", 200, [&session] {
    return std::string(session ? "<response><CurrentDownloadRate>1250000</CurrentDownloadRate>"
                                 "<CurrentUploadRate>125000</CurrentUploadRate></response>"
                               : "<error><code>125002</code><message></message></error>");
  });
  for (int round = 0; round < 3; ++round) {
    session = false;
    TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"upnpHost\":\"10.0.0.1\"}") == Status::Ok);
    box.result("live", "{}");
    // At every moment of the search of the first: its questions leave, some are still waited for.
    for (int i = 0; i < round * 3; ++i) box.sys.advance(200);
    TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"upnpHost\":\"10.0.0.2\"}") == Status::Ok);
    for (int i = 0; i < 16; ++i) {
      box.result("live", "{}");
      box.sys.advance(1000);
    }
    const std::string r = box.result("live", "{}");
    TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"ok\",\"manual\":\"10.0.0.2\",\"ip\":\"10.0.0.2\",\"via\":\"huawei\""),
                             r.c_str());
  }
}

// Who tells the traffic chosen by the owner, and the port of the device: only that kind is
// asked, where the settings say.
void test_who_tells_the_traffic_chosen_by_hand() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  hal::SsdpHit gateway;
  gateway.ip = kGw;
  snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
  snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:5000/rootDesc.xml");
  box.net().addSsdp(gateway);
  auto& http = box.sys.pc().httpClient;
  http.when("http://192.168.1.1:5000/rootDesc.xml", 200,
            "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
            "WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service>"
            "</serviceList></device></root>");
  http.when("http://192.168.1.1:5000/ctl/CmnIfCfg", 200,
            "<NewTotalBytesReceived>1000</NewTotalBytesReceived><NewTotalBytesSent>500</NewTotalBytesSent>"
            "<NewTotalPacketsReceived>10</NewTotalPacketsReceived><NewTotalPacketsSent>5</NewTotalPacketsSent>");
  http.when(kDishUrl, 200, dishAnswer(25e6f, 3e6f, 38.4f, 0, 0, 10));
  http.when("http://10.0.0.5:9999/SpaceX.API.Device.Device/Handle", 200, dishAnswer(8e6f, 1e6f, 40, 0, 0, 10));
  const auto wait = [&box](int seconds) {
    for (int i = 0; i < seconds; ++i) {
      box.result("live", "{}");
      box.sys.advance(1000);
    }
    return box.result("live", "{}");
  };
  const auto set = [&box](const char* json) {
    TEST_ASSERT_TRUE_MESSAGE(box.sys.command(kTarget, "config.set", json) == Status::Ok, json);
  };
  // With a UPnP router in the network, the dish asked for is the one read.
  set("{\"trafficFrom\":2}");
  std::string r = wait(10);
  TEST_ASSERT_TRUE_MESSAGE(
      has(r, "\"router\":{\"state\":\"ok\",\"only\":\"starlink\",\"ip\":\"192.168.100.1\",\"via\":\"starlink\""), r.c_str());
  // A dish at another address, and at a port of its own.
  set("{\"upnpHost\":\"10.0.0.5:9999\"}");
  r = wait(10);
  TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"ok\",\"manual\":\"10.0.0.5:9999\",\"only\":\"starlink\","
                                  "\"ip\":\"10.0.0.5\",\"via\":\"starlink\""),
                           r.c_str());
  lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(r.c_str(), r.size()));
  TEST_ASSERT_EQUAL_INT64(8000, wanLast(v["router"]["down"]));
  // A router with SNMP at a port of its own: nothing else is asked meanwhile.
  SnmpAgent agent;
  agent.objects[{1, 3, 6, 1, 2, 1, 4, 21, 1, 2, 0, 0, 0, 0}] = {0x02, 4};
  agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 6, 4}] = {0x46, 1000};
  agent.objects[{1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 10, 4}] = {0x46, 500};
  box.net().onUdp([&agent](hal::Ip4 ip, uint16_t port, const std::vector<uint8_t>& q) {
    return port == 1161 && ip == kGw ? agent.answer(q) : std::vector<uint8_t>();
  });
  const size_t before = http.seen().size();
  set("{\"trafficFrom\":4,\"upnpHost\":\"192.168.1.1:1161\",\"snmpCommunity\":\"public\"}");
  r = wait(10);
  TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"ok\",\"manual\":\"192.168.1.1:1161\",\"only\":\"snmp\","
                                  "\"ip\":\"192.168.1.1\",\"via\":\"snmp\""),
                           r.c_str());
  const auto seen = http.seen();
  for (size_t i = before; i < seen.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(seen[i].url.find("SpaceX") == std::string::npos &&
                                 seen[i].url.find("192.168.1.1") == std::string::npos,
                             seen[i].url.c_str());
  }
  // The kind asked for does not answer: it is said, and no other kind is tried in its place.
  set("{\"trafficFrom\":3,\"upnpHost\":\"\"}");
  r = wait(8);
  TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"none\",\"only\":\"huawei\"}"), r.c_str());
  // Only the router with UPnP, as before the other kinds were there.
  set("{\"trafficFrom\":1}");
  r = wait(8);
  TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"ok\",\"only\":\"upnp\",\"ip\":\"192.168.1.1\",\"via\":\"upnp\""),
                           r.c_str());
  // An address that is not one: nothing is asked, and it is said.
  set("{\"trafficFrom\":0,\"upnpHost\":\"router:80\"}");
  r = wait(8);
  TEST_ASSERT_TRUE_MESSAGE(has(r, "\"router\":{\"state\":\"none\",\"manual\":\"router:80\"}"), r.c_str());
}

// No word of the factory goes to a router: until the owner writes the community of his, SNMP is
// not asked at all, by itself or chosen by hand.
void test_snmp_is_not_asked_until_its_word_is_written() {
  Box box;
  box.net().addHost(kGw, nullptr, 2);
  unsigned datagrams = 0;
  box.net().onUdp([&datagrams](hal::Ip4, uint16_t port, const std::vector<uint8_t>&) {
    if (port == 161) ++datagrams;
    return std::vector<uint8_t>();
  });
  const auto wait = [&box](int seconds) {
    for (int i = 0; i < seconds; ++i) {
      box.result("live", "{}");
      box.sys.advance(1000);
    }
    return box.result("live", "{}");
  };
  TEST_ASSERT_TRUE(has(wait(12), "\"router\":{\"state\":\"none\"}"));
  TEST_ASSERT_EQUAL_UINT(0, datagrams);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"trafficFrom\":4}") == Status::Ok);
  TEST_ASSERT_TRUE(has(wait(8), "\"router\":{\"state\":\"none\",\"only\":\"snmp\"}"));
  TEST_ASSERT_EQUAL_UINT(0, datagrams);
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpCommunity\":\"house\"}") == Status::Ok);
  wait(8);
  TEST_ASSERT_TRUE(datagrams > 0);
  // Written, it is a setting as the others: it can be emptied again, and SNMP stops.
  TEST_ASSERT_TRUE(box.sys.command(kTarget, "config.set", "{\"snmpCommunity\":\"\"}") == Status::Ok);
  wait(4);
  const unsigned before = datagrams;
  wait(10);
  TEST_ASSERT_EQUAL_UINT(before, datagrams);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_history_in_blocks_of_15_minutes);
  RUN_TEST(test_a_service_watched_falls_and_comes_back);
  RUN_TEST(test_a_port_and_a_page_as_services);
  RUN_TEST(test_without_its_network_nothing_is_checked);
  RUN_TEST(test_the_history_of_a_service_and_its_day);
  RUN_TEST(test_a_pc_is_woken_and_found_on);
  RUN_TEST(test_a_pc_turned_off_with_its_webhook);
  RUN_TEST(test_who_is_in_the_network_with_the_loopmaxpro_in_sight);
  RUN_TEST(test_a_device_never_seen_before_is_told);
  RUN_TEST(test_the_list_is_kept_and_searched_by_itself);
  RUN_TEST(test_a_large_network_makes_the_list_grow);
  RUN_TEST(test_a_device_asleep_at_the_arp_stays_one);
  RUN_TEST(test_a_list_of_1_0_0_is_read_and_mended);
  RUN_TEST(test_what_a_device_is);
  RUN_TEST(test_cameras_and_recorders_by_their_ports);
  RUN_TEST(test_the_makers_a_page_at_a_time);
  RUN_TEST(test_live_times_and_the_traffic_of_the_router);
  RUN_TEST(test_the_upload_of_a_router_that_stops_counting_bytes);
  RUN_TEST(test_the_router_is_asked_little);
  RUN_TEST(test_only_the_router_of_the_network_by_itself);
  RUN_TEST(test_the_search_rests_when_nobody_looks);
  RUN_TEST(test_a_upnp_device_chosen_by_hand);
  RUN_TEST(test_a_router_without_upnp_says_so);
  RUN_TEST(test_the_devices_without_upnp_alone);
  RUN_TEST(test_a_dish_of_starlink_says_the_traffic);
  RUN_TEST(test_a_router_of_huawei_says_the_traffic);
  RUN_TEST(test_a_router_with_snmp_says_the_traffic);
  RUN_TEST(test_a_router_with_snmp_wants_its_port);
  RUN_TEST(test_with_upnp_the_other_devices_are_not_asked);
  RUN_TEST(test_the_device_changed_while_it_is_looked_for);
  RUN_TEST(test_who_tells_the_traffic_chosen_by_hand);
  RUN_TEST(test_snmp_is_not_asked_until_its_word_is_written);
  RUN_TEST(test_the_live_samples_keep_their_pace);
  RUN_TEST(test_quiet_hours_and_the_test_button);
  RUN_TEST(test_the_tools_and_the_chat_are_of_the_full_version);
  RUN_TEST(test_the_commands_fit_in_a_board);
  RUN_TEST(test_the_lists_are_kept);
  RUN_TEST(test_what_a_home_system_sees);
  return UNITY_END();
}
