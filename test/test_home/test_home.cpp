// Tests on the PC of the bridge towards Home Assistant: what the modules show goes to
// the broker as Home Assistant wants it, and what Home Assistant asks reaches the modules.
#include "../support/online_system.h"

#include <string>

using namespace lmp;
using namespace lmp::test;
using lmp::hal::native::MqttSent;

lmp::sdk::Module& lmp_module_zappy2();
lmp::sdk::Module& lmp_module_rgb();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kBase = "lmp/loopmax-0001";

void withBroker(Config& config) {
  config.mqtt.enabled = true;
  config.mqtt.host.assign("broker.test");
  config.mqtt.events = false;
}

void withBrokerNoHome(Config& config) {
  withBroker(config);
  config.mqtt.home = false;
}

// Last message sent on a topic, or null.
const MqttSent* lastOn(OnlineSystem& sys, const std::string& topic) {
  const std::vector<MqttSent>& sent = sys.pc().mqtt.sent();
  for (size_t i = sent.size(); i > 0; --i) {
    if (sent[i - 1].topic == topic) return &sent[i - 1];
  }
  return nullptr;
}

size_t countOn(OnlineSystem& sys, const std::string& topic) {
  size_t n = 0;
  for (const MqttSent& m : sys.pc().mqtt.sent()) {
    if (m.topic == topic) ++n;
  }
  return n;
}

std::string base(const char* rest) { return std::string(kBase) + "/" + rest; }

// "lmp_" and the MAC address of the board of the test, as the bridge names the device.
std::string nodeOf(OnlineSystem& sys) {
  hal::Mac mac;
  sys.tb.board.sys->mac(mac);
  char text[20];
  snprintf(text, sizeof(text), "lmp_%02x%02x%02x%02x%02x%02x", mac.b[0], mac.b[1], mac.b[2],
           mac.b[3], mac.b[4], mac.b[5]);
  return text;
}

// Where Home Assistant looks for the description of a thing.
std::string what(OnlineSystem& sys, const char* kind, const char* thing) {
  return std::string("homeassistant/") + kind + "/" + nodeOf(sys) + "/" + thing + "/config";
}

JsonValue json(const MqttSent* message) {
  TEST_ASSERT_NOT_NULL(message);
  return JsonValue::parse(StringView(message->payload.data(), message->payload.size()));
}

}  // namespace

void test_home_assistant_is_told_the_things_of_a_module() {
  OnlineSystem sys(true, withBroker, 1500, {&lmp_module_zappy2()});
  sys.advance(2000);
  const std::string node = nodeOf(sys);

  // Each channel is a switch. What it is, kept by the broker.
  {
    const MqttSent* told = lastOn(sys, what(sys, "switch", "zappy2_ch0"));
    TEST_ASSERT_NOT_NULL(told);
    TEST_ASSERT_TRUE(told->retain);
    const JsonValue d = json(told);
    TEST_ASSERT_TRUE(d.valid());
    TEST_ASSERT_TRUE(d["uniq_id"].stringEquals((node + "_zappy2_ch0").c_str()));
    TEST_ASSERT_TRUE(d["stat_t"].stringEquals(base("home/zappy2/state").c_str()));
    TEST_ASSERT_TRUE(d["val_tpl"].stringEquals("{{ value_json.ch0 }}"));
    TEST_ASSERT_TRUE(d["cmd_t"].stringEquals(base("home/zappy2/set/ch0").c_str()));
    // A socket, for the voice assistants; and nothing technical about it.
    TEST_ASSERT_TRUE(d["dev_cla"].stringEquals("outlet"));
    TEST_ASSERT_FALSE(d["ent_cat"].valid());
    TEST_ASSERT_TRUE(d["avty_t"].stringEquals(base("status").c_str()));
    TEST_ASSERT_TRUE(d["dev"]["ids"].at(0).stringEquals(node.c_str()));
    TEST_ASSERT_TRUE(d["dev"]["name"].stringEquals("loopmax-0001"));
    TEST_ASSERT_TRUE(d["dev"]["mf"].stringEquals("SmartiLab"));
    TEST_ASSERT_TRUE(d["dev"]["cu"].stringEquals("http://192.168.1.50/"));
  }
  TEST_ASSERT_NOT_NULL(lastOn(sys, what(sys, "switch", "zappy2_ch1")));
  // And what they are now, all in one message, kept too.
  const MqttSent* state = lastOn(sys, base("home/zappy2/state"));
  TEST_ASSERT_NOT_NULL(state);
  TEST_ASSERT_TRUE(state->retain);
  TEST_ASSERT_EQUAL_STRING("{\"ch0\":\"OFF\",\"ch1\":\"OFF\"}", state->payload.c_str());

  // Home Assistant turns one on.
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(base("home/zappy2/set/ch0").c_str(), "ON"));
  sys.advance(800);
  {
    Doc doc(sys.kernel(), "mod.zappy2");
    TEST_ASSERT_EQUAL_INT64(1, doc.v["pins"].at(0)["level"].asInt());
    TEST_ASSERT_EQUAL_INT64(0, doc.v["pins"].at(1)["level"].asInt());
  }
  TEST_ASSERT_EQUAL_STRING("{\"ch0\":\"ON\",\"ch1\":\"OFF\"}",
                           lastOn(sys, base("home/zappy2/state"))->payload.c_str());

  // A change made from the console reaches it too.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "set", "{\"channel\":1,\"value\":true}") == Status::Ok);
  sys.advance(800);
  TEST_ASSERT_EQUAL_STRING("{\"ch0\":\"ON\",\"ch1\":\"ON\"}",
                           lastOn(sys, base("home/zappy2/state"))->payload.c_str());

  // Nothing changes: nothing is told.
  const size_t told = countOn(sys, base("home/zappy2/state"));
  const size_t described = countOn(sys, what(sys, "switch", "zappy2_ch0"));
  sys.advance(5000);
  TEST_ASSERT_EQUAL_UINT(told, countOn(sys, base("home/zappy2/state")));
  TEST_ASSERT_EQUAL_UINT(described, countOn(sys, what(sys, "switch", "zappy2_ch0")));

  // The owner gives a channel another name: what it is is told again.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "name", "{\"channel\":0,\"name\":\"Lamp\"}") ==
                   Status::Ok);
  sys.advance(1500);
  TEST_ASSERT_TRUE(countOn(sys, what(sys, "switch", "zappy2_ch0")) > described);
  TEST_ASSERT_TRUE(json(lastOn(sys, what(sys, "switch", "zappy2_ch0")))["name"].stringEquals("Lamp"));

  // Something that is not a thing of the module: nothing happens, and the device goes on.
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(base("home/zappy2/set/ch9").c_str(), "ON"));
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(base("home/nobody/set/ch0").c_str(), "ON"));
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.logHas("home: ch9 of zappy2 not done"));

  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_TRUE(doc.v["home"]["enabled"].asBool());
  TEST_ASSERT_TRUE(doc.v["home"]["prefix"].stringEquals("homeassistant"));
  TEST_ASSERT_TRUE(doc.v["home"]["node"].stringEquals(node.c_str()));
  TEST_ASSERT_TRUE(doc.v["home"]["asked"].asInt() >= 2);
}

void test_a_light_is_a_light_for_home_assistant() {
  OnlineSystem sys(true, withBroker, 1500, {&lmp_module_rgb()});
  sys.advance(2000);
  {
    const JsonValue d = json(lastOn(sys, what(sys, "light", "rgb_light0")));
    TEST_ASSERT_TRUE(d.valid());
    TEST_ASSERT_TRUE(d["schema"].stringEquals("json"));
    TEST_ASSERT_TRUE(d["stat_t"].stringEquals(base("home/rgb/light/light0").c_str()));
    TEST_ASSERT_TRUE(d["cmd_t"].stringEquals(base("home/rgb/set/light0").c_str()));
    TEST_ASSERT_TRUE(d["brightness"].asBool());
    TEST_ASSERT_TRUE(d["sup_clrm"].at(0).stringEquals("rgb"));
  }
  // Its channel is not told as a switch too: the light says itself whole.
  TEST_ASSERT_NULL(lastOn(sys, what(sys, "switch", "rgb_ch0")));
  TEST_ASSERT_TRUE(json(lastOn(sys, base("home/rgb/light/light0")))["state"].stringEquals("OFF"));

  // Home Assistant asks for green at half brightness.
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(
      base("home/rgb/set/light0").c_str(),
      "{\"state\":\"ON\",\"brightness\":128,\"color\":{\"r\":0,\"g\":255,\"b\":0}}"));
  sys.advance(800);
  {
    const JsonValue now = json(lastOn(sys, base("home/rgb/light/light0")));
    TEST_ASSERT_TRUE(now["state"].stringEquals("ON"));
    TEST_ASSERT_EQUAL_INT64(128, now["brightness"].asInt());
    TEST_ASSERT_TRUE(now["color_mode"].stringEquals("rgb"));
    TEST_ASSERT_EQUAL_INT64(0, now["color"]["r"].asInt());
    TEST_ASSERT_EQUAL_INT64(255, now["color"]["g"].asInt());
  }
  // And off.
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(base("home/rgb/set/light0").c_str(), "{\"state\":\"OFF\"}"));
  sys.advance(800);
  TEST_ASSERT_TRUE(json(lastOn(sys, base("home/rgb/light/light0")))["state"].stringEquals("OFF"));
}

void test_nothing_is_told_when_the_owner_turns_it_off() {
  OnlineSystem sys(true, withBrokerNoHome, 1500, {&lmp_module_zappy2()});
  sys.advance(2000);
  for (const MqttSent& m : sys.pc().mqtt.sent()) {
    TEST_ASSERT_NULL_MESSAGE(strstr(m.topic.c_str(), "homeassistant/"), m.topic.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(m.topic.c_str(), "/home/"), m.topic.c_str());
  }
  // Turned on, and with the place where another Home Assistant looks.
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"home\":true,\"homePrefix\":\"house\"}") ==
                   Status::Ok);
  sys.advance(4000);
  TEST_ASSERT_NOT_NULL(lastOn(sys, std::string("house/switch/") + nodeOf(sys) + "/zappy2_ch0/config"));
  TEST_ASSERT_NOT_NULL(lastOn(sys, base("home/zappy2/state")));
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"homePrefix\":\"a/#\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("homePrefix", field);
}

void test_everything_is_told_again_after_the_broker_comes_back() {
  OnlineSystem sys(true, withBroker, 1500, {&lmp_module_zappy2()});
  sys.advance(2000);
  const size_t described = countOn(sys, what(sys, "switch", "zappy2_ch0"));
  const size_t told = countOn(sys, base("home/zappy2/state"));
  sys.pc().mqtt.drop();
  sys.advance(3000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_UINT(described + 1, countOn(sys, what(sys, "switch", "zappy2_ch0")));
  TEST_ASSERT_EQUAL_UINT(told + 1, countOn(sys, base("home/zappy2/state")));
}

// ---------------------------------------------------------------- a module with parts

namespace {

// A module whose parts are devices of their own, as a hub with its nodes.
class Hub final : public sdk::Module {
  LMP_MODULE("hub", "Hub of the test", "", "1.0.0", sdk::ModuleKind::Bridge, 100)

 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }

  void things(sdk::ThingSink& out) const override {
    out.number("nodes", "Nodes", nodes);
    char id[8], name[24];
    for (int i = 1; i <= nodes; ++i) {
      snprintf(id, sizeof(id), "t%d", i);
      snprintf(name, sizeof(name), "Node %d", i);
      out.part(id, name);
      out.number("counts", "Counts", counts[i]);
      out.flag("alarm", "Alarm", counts[i] > 0, "motion");
      out.technical();
      out.number("battery", "Battery", 0, "%", "battery", 0, false);   // not known yet
      out.button("refresh", "Refresh");
    }
    out.part(nullptr, nullptr);
  }

  Status thingSet(const char* id, const sdk::ThingAsk& ask) override {
    asked = id;
    pressed = ask.press;
    return Status::Ok;
  }

  void tell() { markChanged(); }

  int nodes = 20;
  int counts[32] = {};
  std::string asked;
  bool pressed = false;
};

}  // namespace

void test_a_part_of_a_module_is_a_device_of_its_own() {
  static Hub hub;
  hub.nodes = 20;
  memset(hub.counts, 0, sizeof(hub.counts));
  OnlineSystem sys(true, withBroker, 1500, {&hub});
  const std::string node = nodeOf(sys);
  // A hundred messages do not go out together: a few for each round of the system.
  TEST_ASSERT_NULL(lastOn(sys, what(sys, "sensor", "hub_t20_counts")));
  sys.advance(6000);

  // The hub itself, and each node as a device reached through it.
  TEST_ASSERT_TRUE(json(lastOn(sys, what(sys, "sensor", "hub_nodes")))["dev"]["ids"].at(0).stringEquals(node.c_str()));
  {
    const JsonValue d = json(lastOn(sys, what(sys, "sensor", "hub_t7_counts")));
    TEST_ASSERT_TRUE(d.valid());
    TEST_ASSERT_TRUE(d["uniq_id"].stringEquals((node + "_hub_t7_counts").c_str()));
    TEST_ASSERT_TRUE(d["stat_t"].stringEquals(base("home/hub/t7/state").c_str()));
    TEST_ASSERT_TRUE(d["dev"]["ids"].at(0).stringEquals((node + "_hub_t7").c_str()));
    TEST_ASSERT_TRUE(d["dev"]["name"].stringEquals("Node 7"));
    TEST_ASSERT_TRUE(d["dev"]["via_device"].stringEquals(node.c_str()));
  }
  {
    const JsonValue d = json(lastOn(sys, what(sys, "binary_sensor", "hub_t20_alarm")));
    TEST_ASSERT_TRUE(d["dev_cla"].stringEquals("motion"));
    TEST_ASSERT_FALSE(d["ent_cat"].valid());
    const JsonValue b = json(lastOn(sys, what(sys, "sensor", "hub_t20_battery")));
    TEST_ASSERT_TRUE(b["unit_of_meas"].stringEquals("%"));
    TEST_ASSERT_TRUE(b["dev_cla"].stringEquals("battery"));
    // Technical: kept apart. Only the thing it was asked for: the button after it is not.
    TEST_ASSERT_TRUE(b["ent_cat"].stringEquals("diagnostic"));
    TEST_ASSERT_FALSE(json(lastOn(sys, what(sys, "button", "hub_t20_refresh")))["ent_cat"].valid());
    const JsonValue press = json(lastOn(sys, what(sys, "button", "hub_t20_refresh")));
    TEST_ASSERT_TRUE(press["cmd_t"].stringEquals(base("home/hub/set/t20.refresh").c_str()));
    TEST_ASSERT_FALSE(press["stat_t"].valid());
  }
  // The values of each part in a message of its own; what is not known is said so.
  TEST_ASSERT_EQUAL_STRING("{\"nodes\":20}", lastOn(sys, base("home/hub/state"))->payload.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"counts\":0,\"alarm\":\"OFF\",\"battery\":null}",
                           lastOn(sys, base("home/hub/t7/state"))->payload.c_str());

  // One node changes: only that one is told again.
  const size_t seven = countOn(sys, base("home/hub/t7/state"));
  const size_t eight = countOn(sys, base("home/hub/t8/state"));
  hub.counts[7] = 2;
  hub.tell();
  sys.advance(3000);
  TEST_ASSERT_EQUAL_UINT(seven + 1, countOn(sys, base("home/hub/t7/state")));
  TEST_ASSERT_EQUAL_UINT(eight, countOn(sys, base("home/hub/t8/state")));
  TEST_ASSERT_EQUAL_STRING("{\"counts\":2,\"alarm\":\"ON\",\"battery\":null}",
                           lastOn(sys, base("home/hub/t7/state"))->payload.c_str());

  // A button of a node, pressed from Home Assistant.
  TEST_ASSERT_TRUE(sys.pc().mqtt.deliver(base("home/hub/set/t7.refresh").c_str(), "PRESS"));
  sys.advance(500);
  TEST_ASSERT_EQUAL_STRING("t7.refresh", hub.asked.c_str());
  TEST_ASSERT_TRUE(hub.pressed);

  // A new node arrives: Home Assistant is told what it is, by itself.
  hub.nodes = 21;
  hub.tell();
  sys.advance(8000);
  TEST_ASSERT_NOT_NULL(lastOn(sys, what(sys, "sensor", "hub_t21_counts")));
  TEST_ASSERT_EQUAL_STRING("{\"nodes\":21}", lastOn(sys, base("home/hub/state"))->payload.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_part_of_a_module_is_a_device_of_its_own);
  RUN_TEST(test_home_assistant_is_told_the_things_of_a_module);
  RUN_TEST(test_a_light_is_a_light_for_home_assistant);
  RUN_TEST(test_nothing_is_told_when_the_owner_turns_it_off);
  RUN_TEST(test_everything_is_told_again_after_the_broker_comes_back);
  return UNITY_END();
}
