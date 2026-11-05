// Tests on the PC of the messages through a broker: connection, commands, documents, events.
#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;
using lmp::hal::native::MqttSent;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kBase = "lmp/loopmax-0001";

void withBroker(Config& config) {
  config.mqtt.enabled = true;
  config.mqtt.host.assign("broker.test");
  config.mqtt.user.assign("device-user");
  config.mqtt.psw.assign("broker-password");
}

void withBrokerNoEvents(Config& config) {
  withBroker(config);
  config.mqtt.events = false;
}

// The broker is there and does not want the device.
void withBrokerRefusing(Config& config) {
  withBroker(config);
  hal::native::objects().mqtt.setRefusing("not authorized");
}

void withOwnTopics(Config& config) {
  withBroker(config);
  config.mqtt.base.assign("house/cellar");
  config.mqtt.port = 1883;
  config.mqtt.tls = false;
  config.mqtt.keepAliveSec = 30;
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

std::string topic(const char* rest) { return std::string(kBase) + "/" + rest; }

}  // namespace

// ---------------------------------------------------------------- connection

void test_without_settings_nobody_is_called() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Ready);
  TEST_ASSERT_FALSE(sys.app.mqtt().connected());
  TEST_ASSERT_FALSE(sys.pc().mqtt.configured);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().mqtt.connects());
  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_TRUE(doc.v["available"].asBool());
  TEST_ASSERT_FALSE(doc.v["enabled"].asBool(true));
  TEST_ASSERT_FALSE(doc.v["connected"].asBool(true));
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "reconnect", "{}") == Status::Denied);
  TEST_ASSERT_TRUE(sys.app.mqtt().publish("x/y", StringView("1"), false) == Status::Denied);
}

void test_the_device_connects_to_the_broker() {
  OnlineSystem sys(true, withBroker);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Ready);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::MqttConnected));

  TEST_ASSERT_EQUAL_STRING("broker.test", broker.host.c_str());
  TEST_ASSERT_EQUAL_UINT16(8883, broker.port);
  TEST_ASSERT_TRUE(broker.useTls);
  TEST_ASSERT_EQUAL_STRING("lmp-loopmax-0001", broker.clientId.c_str());
  TEST_ASSERT_EQUAL_STRING("device-user", broker.username.c_str());
  TEST_ASSERT_EQUAL_STRING("broker-password", broker.password.c_str());
  TEST_ASSERT_EQUAL_UINT16(60, broker.keepAliveSec);
  // If the device disappears the broker says so for it.
  TEST_ASSERT_EQUAL_STRING(topic("status").c_str(), broker.willTopic.c_str());
  TEST_ASSERT_EQUAL_STRING("offline", broker.willMessage.c_str());

  // Commands, documents, and what Home Assistant asks of the things of the modules.
  TEST_ASSERT_EQUAL_UINT(3, broker.subscriptions().size());
  TEST_ASSERT_EQUAL_STRING(topic("cmd/#").c_str(), broker.subscriptions()[0].c_str());
  TEST_ASSERT_EQUAL_STRING(topic("get/#").c_str(), broker.subscriptions()[1].c_str());
  TEST_ASSERT_EQUAL_STRING(topic("home/#").c_str(), broker.subscriptions()[2].c_str());

  const MqttSent* status = lastOn(sys, topic("status"));
  TEST_ASSERT_NOT_NULL(status);
  TEST_ASSERT_EQUAL_STRING("online", status->payload.c_str());
  TEST_ASSERT_TRUE(status->retain);

  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_TRUE(doc.v["enabled"].asBool());
  TEST_ASSERT_TRUE(doc.v["connected"].asBool());
  TEST_ASSERT_TRUE(doc.v["host"].stringEquals("broker.test"));
  TEST_ASSERT_TRUE(doc.v["base"].stringEquals(kBase));
  TEST_ASSERT_TRUE(doc.v["pswSet"].asBool());
  TEST_ASSERT_EQUAL_INT64(1, doc.v["connections"].asInt());
  // The password is never shown.
  TEST_ASSERT_NULL(strstr(doc.text(), "broker-password"));
  TEST_ASSERT_FALSE(sys.logHas("broker-password"));
  Json<4096> cfg;
  cfg.w.beginObject();
  sys.kernel().config().describe(cfg.w, Config::kMqtt);
  cfg.w.endObject();
  TEST_ASSERT_NULL(strstr(cfg.text(), "broker-password"));
  TEST_ASSERT_TRUE(cfg.parse()["pswSet"].asBool());
}

void test_topics_and_port_of_the_settings() {
  OnlineSystem sys(true, withOwnTopics);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_STRING("house/cellar", sys.app.mqtt().base());
  TEST_ASSERT_EQUAL_UINT16(1883, broker.port);
  TEST_ASSERT_FALSE(broker.useTls);
  TEST_ASSERT_EQUAL_UINT16(30, broker.keepAliveSec);
  TEST_ASSERT_EQUAL_STRING("house/cellar/status", broker.willTopic.c_str());
  TEST_ASSERT_EQUAL_STRING("house/cellar/cmd/#", broker.subscriptions()[0].c_str());
  TEST_ASSERT_NOT_NULL(lastOn(sys, "house/cellar/status"));
}

void test_no_network_no_broker() {
  OnlineSystem sys(false, withBroker);
  // In access point mode the service does not run at all.
  TEST_ASSERT_FALSE(sys.app.mqtt().running());
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().mqtt.connects());
}

void test_a_broker_that_refuses() {
  OnlineSystem sys(true, withBrokerRefusing);
  sys.advance(5000);
  TEST_ASSERT_FALSE(sys.app.mqtt().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Degraded);
  TEST_ASSERT_TRUE(sys.pc().mqtt.connects() >= 2);   // the driver keeps trying
  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_TRUE(doc.v["error"].stringEquals("not authorized"));

  // Said once, not at every attempt.
  LogEntry entries[LMP_LOG_RING];
  const size_t n = sys.kernel().log().read(0, entries, LMP_LOG_RING);
  int said = 0;
  for (size_t i = 0; i < n; ++i) {
    if (strstr(entries[i].msg, "not connected: not authorized")) ++said;
  }
  TEST_ASSERT_EQUAL_INT(1, said);

  // The broker changes its mind.
  sys.pc().mqtt.setRefusing("");
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Ready);
}

void test_the_connection_is_lost_and_found() {
  OnlineSystem sys(true, withBroker);
  sys.pc().mqtt.clearSent();
  sys.pc().mqtt.drop();
  sys.advance(100);
  TEST_ASSERT_FALSE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::MqttLost));
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Degraded);
  TEST_ASSERT_TRUE(sys.app.mqtt().publish("x/y", StringView("1"), false) == Status::Denied);

  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::MqttConnected));
  // The broker forgot what the device wanted to hear: asked again.
  TEST_ASSERT_EQUAL_UINT(3, sys.pc().mqtt.subscriptions().size());
  TEST_ASSERT_EQUAL_UINT(1, countOn(sys, topic("status")));
  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_EQUAL_INT64(2, doc.v["connections"].asInt());
}

void test_the_broker_follows_the_network() {
  OnlineSystem sys(true, withBroker);
  sys.pc().net.removeNetwork(kHome);
  sys.pc().net.dropSta();
  sys.advance(500);
  TEST_ASSERT_FALSE(sys.app.mqtt().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Degraded);
  const unsigned before = sys.pc().mqtt.connects();
  sys.advance(10000);
  TEST_ASSERT_EQUAL_UINT(before, sys.pc().mqtt.connects());   // nobody to reach it through

  sys.pc().net.addNetwork(kHome, kHomePassword);
  sys.advance(30000);
  TEST_ASSERT_TRUE(sys.app.net().connected());
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
}

void test_the_settings_are_changed_by_command() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "set",
                               "{\"enabled\":true,\"host\":\"broker.test\",\"port\":1883,"
                               "\"tls\":false,\"user\":\"u\",\"psw\":\"broker-password\"}") ==
                   Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_UINT16(1883, sys.pc().mqtt.port);
  TEST_ASSERT_TRUE(sys.kernel().config().dirty() ||
                   sys.kernel().config().mqtt.host == "broker.test");

  // Another broker: the device leaves the first, saying so, and goes to the second.
  sys.pc().mqtt.clearSent();
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "set", "{\"host\":\"other.test\"}") == Status::Ok);
  sys.advance(50);
  const MqttSent* bye = lastOn(sys, topic("status"));
  TEST_ASSERT_NOT_NULL(bye);
  TEST_ASSERT_EQUAL_STRING("offline", bye->payload.c_str());
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_STRING("other.test", sys.pc().mqtt.host.c_str());

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"base\":\"house/#\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("base", field);
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"base\":\"/house\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"base\":\"house/cellar\"}") ==
                   Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_EQUAL_STRING("house/cellar", sys.app.mqtt().base());
  TEST_ASSERT_EQUAL_STRING("house/cellar/cmd/#", sys.pc().mqtt.subscriptions()[0].c_str());
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "set", "{}") == Status::Invalid);

  // Turned off: the device leaves and nothing is wrong.
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "set", "{\"enabled\":false}") == Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_FALSE(sys.app.mqtt().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Mqtt) == ServiceState::Ready);
}

void test_the_name_of_the_device_is_in_the_topics() {
  OnlineSystem sys(true, withBroker);
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{\"hostname\":\"cellar\"}") == Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  TEST_ASSERT_EQUAL_STRING("lmp/cellar", sys.app.mqtt().base());
  TEST_ASSERT_EQUAL_STRING("lmp-cellar", sys.pc().mqtt.clientId.c_str());
}

void test_a_board_without_broker() {
  TestBoard tb;
  tb.board.mqtt = nullptr;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.board.kv->begin();
  {
    Config config(tb.board.kv, tb.board.rng, tb.board.sys);
    config.load();
    config.net.staSsid.assign(kHome);
    config.net.staPsw.assign(kHomePassword);
    config.sys.mode = 2;
    config.touch(Config::kNet);
    config.touch(Config::kSys);
    config.save();
  }
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);
  TEST_ASSERT_FALSE(app.mqtt().running());
  TEST_ASSERT_EQUAL_STRING("no MQTT on this board", app.mqtt().reason());
  TEST_ASSERT_TRUE(app.net().connected());
  TEST_ASSERT_TRUE(app.web().running());
}

// ---------------------------------------------------------------- commands and documents

void test_a_command_arrives_from_the_broker() {
  OnlineSystem sys(true, withBrokerNoEvents);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  broker.clearSent();

  TEST_ASSERT_TRUE(broker.deliver(topic("cmd/sys.config/set").c_str(),
                                  "{\"lang\":\"it\",\"deviceName\":\"Office Plugs\"}"));
  // Nothing happens in the context of the driver.
  TEST_ASSERT_EQUAL_STRING("en", sys.kernel().config().sys.lang.c_str());
  sys.advance(200);
  TEST_ASSERT_EQUAL_STRING("it", sys.kernel().config().sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Office Plugs", sys.kernel().config().sys.deviceName.c_str());
  TEST_ASSERT_TRUE(sys.logHas("sys.config set from mqtt: ok"));

  const MqttSent* ack = lastOn(sys, topic("ack/sys.config/set"));
  TEST_ASSERT_NOT_NULL(ack);
  const JsonValue a = JsonValue::parse(StringView(ack->payload.data(), ack->payload.size()));
  TEST_ASSERT_TRUE(a["ok"].asBool());
  TEST_ASSERT_TRUE(a["status"].stringEquals("ok"));
  TEST_ASSERT_FALSE(ack->retain);

  // A value that is refused names its field, as from the console.
  broker.deliver(topic("cmd/sys.config/set").c_str(), "{\"hostname\":\"-no\"}");
  sys.advance(200);
  ack = lastOn(sys, topic("ack/sys.config/set"));
  const JsonValue b = JsonValue::parse(StringView(ack->payload.data(), ack->payload.size()));
  TEST_ASSERT_FALSE(b["ok"].asBool(true));
  TEST_ASSERT_TRUE(b["status"].stringEquals("invalid"));
  TEST_ASSERT_TRUE(b["field"].stringEquals("hostname"));

  // Text that is not JSON, a command that does not exist, a command without arguments.
  broker.deliver(topic("cmd/sys.config/set").c_str(), "lang=it");
  sys.advance(200);
  ack = lastOn(sys, topic("ack/sys.config/set"));
  TEST_ASSERT_NOT_NULL(strstr(ack->payload.c_str(), "\"status\":\"invalid\""));
  broker.deliver(topic("cmd/sys.none/do").c_str(), "{}");
  sys.advance(200);
  ack = lastOn(sys, topic("ack/sys.none/do"));
  TEST_ASSERT_NOT_NULL(ack);
  TEST_ASSERT_NOT_NULL(strstr(ack->payload.c_str(), "\"status\":\"not_found\""));
  broker.deliver(topic("cmd/sys.log/clear").c_str(), "");
  sys.advance(200);
  ack = lastOn(sys, topic("ack/sys.log/clear"));
  TEST_ASSERT_NOT_NULL(ack);
  TEST_ASSERT_NOT_NULL(strstr(ack->payload.c_str(), "\"ok\":true"));

  // A command that ends later gives its reference.
  broker.deliver(topic("cmd/sys.net/scan").c_str(), "{}");
  sys.advance(200);
  ack = lastOn(sys, topic("ack/sys.net/scan"));
  const JsonValue c = JsonValue::parse(StringView(ack->payload.data(), ack->payload.size()));
  TEST_ASSERT_TRUE(c["status"].stringEquals("pending"));
  TEST_ASSERT_TRUE(c["ref"].asInt() > 0);

  // Topics that are not well formed are dropped without an answer.
  const size_t before = broker.sent().size();
  broker.deliver(topic("cmd/sys.config").c_str(), "{}");
  broker.deliver(topic("cmd/sys.config/set/more").c_str(), "{}");
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT(before, broker.sent().size());
}

void test_a_secret_never_leaves_with_an_answer() {
  OnlineSystem sys(true, withBrokerNoEvents);
  sys.pc().mqtt.clearSent();
  sys.pc().mqtt.deliver(topic("cmd/sys.mqtt/set").c_str(), "{\"psw\":\"another-secret\"}");
  sys.pc().mqtt.deliver(topic("get/sys.mqtt").c_str(), "");
  sys.pc().mqtt.deliver(topic("get/report").c_str(), "");
  sys.advance(2000);
  for (const MqttSent& m : sys.pc().mqtt.sent()) {
    TEST_ASSERT_NULL_MESSAGE(strstr(m.payload.c_str(), "another-secret"), m.topic.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(m.payload.c_str(), "broker-password"), m.topic.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(m.payload.c_str(), kHomePassword), m.topic.c_str());
  }
  TEST_ASSERT_FALSE(sys.logHas("another-secret"));
}

void test_documents_are_asked_through_the_broker() {
  OnlineSystem sys(true, withBrokerNoEvents);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  broker.clearSent();

  broker.deliver(topic("get/sys.net").c_str(), "");
  sys.advance(200);
  const MqttSent* doc = lastOn(sys, topic("doc/sys.net"));
  TEST_ASSERT_NOT_NULL(doc);
  JsonValue v = JsonValue::parse(StringView(doc->payload.data(), doc->payload.size()));
  TEST_ASSERT_TRUE(v["state"].stringEquals("connected"));
  TEST_ASSERT_TRUE(v["sta"]["ssid"].stringEquals(kHome));

  // The report, whole or by parts.
  broker.deliver(topic("get/report").c_str(), "system,clock");
  sys.advance(200);
  doc = lastOn(sys, topic("doc/report"));
  TEST_ASSERT_NOT_NULL(doc);
  v = JsonValue::parse(StringView(doc->payload.data(), doc->payload.size()));
  TEST_ASSERT_TRUE(v["system"].isObject());
  TEST_ASSERT_TRUE(v["clock"].isObject());
  TEST_ASSERT_FALSE(v["net"].valid());
  broker.deliver(topic("get/report").c_str(), "");
  sys.advance(200);
  doc = lastOn(sys, topic("doc/report"));
  v = JsonValue::parse(StringView(doc->payload.data(), doc->payload.size()));
  TEST_ASSERT_TRUE(v["services"].isArray());
  TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, v["device"]["key"].stringLength());

  broker.deliver(topic("get/sys.none").c_str(), "");
  sys.advance(200);
  doc = lastOn(sys, topic("doc/sys.none"));
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_NOT_NULL(strstr(doc->payload.c_str(), "not_found"));
}

void test_events_go_out_through_the_broker() {
  OnlineSystem sys(true, withBroker);
  sys.pc().mqtt.clearSent();
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{\"lang\":\"it\"}") == Status::Ok);
  sys.advance(200);
  bool told = false;
  for (const MqttSent& m : sys.pc().mqtt.sent()) {
    if (m.topic != topic("evt")) continue;
    const JsonValue v = JsonValue::parse(StringView(m.payload.data(), m.payload.size()));
    TEST_ASSERT_TRUE(v.valid());
    if (v["id"].stringEquals("config")) told = true;
    TEST_ASSERT_FALSE(m.retain);
  }
  TEST_ASSERT_TRUE(told);
  // A change of the language is not a reason to leave the broker.
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::MqttConnected));

  // Turned off by the settings.
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "setTopics", "{\"events\":false}") == Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.mqtt().connected());
  sys.pc().mqtt.clearSent();
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{\"lang\":\"en\"}") == Status::Ok);
  sys.advance(200);
  TEST_ASSERT_EQUAL_UINT(0, countOn(sys, topic("evt")));
}

void test_messages_of_others_are_not_for_this_device() {
  OnlineSystem sys(true, withBrokerNoEvents);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  broker.clearSent();
  // The broker hands over only what was asked for.
  TEST_ASSERT_FALSE(broker.deliver("lmp/loopmax-0002/cmd/sys.config/set", "{\"lang\":\"it\"}"));
  TEST_ASSERT_FALSE(broker.deliver(topic("ack/sys.config/set").c_str(), "{}"));
  sys.advance(500);
  TEST_ASSERT_EQUAL_STRING("en", sys.kernel().config().sys.lang.c_str());
  TEST_ASSERT_EQUAL_UINT(0, broker.sent().size());
}

void test_messages_wait_for_the_system() {
  OnlineSystem sys(true, withBrokerNoEvents);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  broker.clearSent();
  // Six in a row, before the system has a round: four wait, two are lost and counted.
  for (int i = 0; i < 6; ++i) broker.deliver(topic("get/sys.clock").c_str(), "");
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(LMP_MQTT_INBOX, countOn(sys, topic("doc/sys.clock")));
  Doc doc(sys.kernel(), "sys.mqtt");
  TEST_ASSERT_EQUAL_INT64(6 - LMP_MQTT_INBOX, doc.v["dropped"].asInt());
  TEST_ASSERT_EQUAL_INT64(LMP_MQTT_INBOX, doc.v["received"].asInt());

  // A command longer than what the system reads is not run by halves.
  broker.clearSent();
  const std::string longName(LMP_MQTT_PAYLOAD_MAX, 'x');
  broker.deliver(topic("cmd/sys.config/set").c_str(), "{\"deviceName\":\"" + longName + "\"}");
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT(0, broker.sent().size());
  TEST_ASSERT_TRUE(sys.logHas("command too long"));
}

// ---------------------------------------------------------------- for the other parts of the system

void test_other_parts_publish_and_listen() {
  OnlineSystem sys(true, withBrokerNoEvents);
  hal::native::Mqtt& broker = sys.pc().mqtt;
  IMqttService* mqtt = sys.kernel().api<IMqttService>();
  TEST_ASSERT_NOT_NULL(mqtt);
  broker.clearSent();

  TEST_ASSERT_TRUE(mqtt->publish("meteo/temp", StringView("21.5"), true) == Status::Ok);
  const MqttSent* sent = lastOn(sys, topic("meteo/temp"));
  TEST_ASSERT_NOT_NULL(sent);
  TEST_ASSERT_EQUAL_STRING("21.5", sent->payload.c_str());
  TEST_ASSERT_TRUE(sent->retain);

  // The names of the system are not for the others, and a topic is a topic.
  const char* refused[] = {"cmd/sys.config/set", "ack/x", "status", "evt", "doc/sys.net", "get/x",
                           "", "/meteo", "meteo/", "meteo//temp", "meteo/#", "meteo/+/temp"};
  for (const char* name : refused) {
    TEST_ASSERT_TRUE_MESSAGE(mqtt->publish(name, StringView("1"), false) == Status::Invalid, name);
  }
  TEST_ASSERT_TRUE(mqtt->publish(nullptr, StringView("1"), false) == Status::Invalid);

  static std::string heardTopic, heardPayload;
  static int heard;
  heard = 0;
  TEST_ASSERT_TRUE(mqtt->listen("meteo", [](StringView t, StringView p) {
    ++heard;
    heardTopic.assign(t.data, t.size);
    heardPayload.assign(p.data, p.size);
  }) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING(topic("meteo/#").c_str(), broker.subscriptions().back().c_str());
  TEST_ASSERT_TRUE(mqtt->listen("cmd", [](StringView, StringView) {}) == Status::Invalid);
  TEST_ASSERT_TRUE(mqtt->listen("me/#", [](StringView, StringView) {}) == Status::Invalid);

  TEST_ASSERT_TRUE(broker.deliver(topic("meteo/set/unit").c_str(), "celsius"));
  TEST_ASSERT_EQUAL_INT(0, heard);   // on the application task, not before
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, heard);
  TEST_ASSERT_EQUAL_STRING("meteo/set/unit", heardTopic.c_str());
  TEST_ASSERT_EQUAL_STRING("celsius", heardPayload.c_str());

  // After a new connection the device asks again also for who listens.
  broker.drop();
  sys.advance(1000);
  TEST_ASSERT_TRUE(mqtt->connected());
  TEST_ASSERT_EQUAL_UINT(4, broker.subscriptions().size());
  TEST_ASSERT_TRUE(broker.deliver(topic("meteo/x").c_str(), "1"));
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(2, heard);

  // One place is of the bridge towards Home Assistant, one of the listener of above.
  for (int i = 2; i < LMP_MQTT_LISTENERS; ++i) {
    static const char* const names[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    TEST_ASSERT_TRUE(mqtt->listen(names[i], [](StringView, StringView) {}) == Status::Ok);
  }
  TEST_ASSERT_TRUE(mqtt->listen("zz", [](StringView, StringView) {}) == Status::NoMem);
}

void test_a_message_sent_by_command() {
  OnlineSystem sys(true, withBrokerNoEvents);
  sys.pc().mqtt.clearSent();
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "publish",
                               "{\"topic\":\"test/one\",\"payload\":\"hello\"}") == Status::Ok);
  const MqttSent* sent = lastOn(sys, topic("test/one"));
  TEST_ASSERT_NOT_NULL(sent);
  TEST_ASSERT_EQUAL_STRING("hello", sent->payload.c_str());
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.mqtt", "publish", "{\"topic\":\"cmd/x/y\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("topic", field);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_settings_nobody_is_called);
  RUN_TEST(test_the_device_connects_to_the_broker);
  RUN_TEST(test_topics_and_port_of_the_settings);
  RUN_TEST(test_no_network_no_broker);
  RUN_TEST(test_a_broker_that_refuses);
  RUN_TEST(test_the_connection_is_lost_and_found);
  RUN_TEST(test_the_broker_follows_the_network);
  RUN_TEST(test_the_settings_are_changed_by_command);
  RUN_TEST(test_the_name_of_the_device_is_in_the_topics);
  RUN_TEST(test_a_board_without_broker);
  RUN_TEST(test_a_command_arrives_from_the_broker);
  RUN_TEST(test_a_secret_never_leaves_with_an_answer);
  RUN_TEST(test_documents_are_asked_through_the_broker);
  RUN_TEST(test_events_go_out_through_the_broker);
  RUN_TEST(test_messages_of_others_are_not_for_this_device);
  RUN_TEST(test_messages_wait_for_the_system);
  RUN_TEST(test_other_parts_publish_and_listen);
  RUN_TEST(test_a_message_sent_by_command);
  return UNITY_END();
}
