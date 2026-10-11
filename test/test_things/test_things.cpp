// Tests on the PC of the things of a module as data (sdk/things.h): the member of its document,
// what the things are, the values of its parts a page at a time, the one command that sets a
// thing. It is what the server reads through the bridge: the document and the answers here are
// the bodies of its envelopes.
#include <stdlib.h>

#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;

// The modules of the products are compiled in the build of the PC too.
lmp::sdk::Module& lmp_module_zappy2();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

// A garden: things of its own of every kind, and beds that are devices of their own.
class Garden : public sdk::Module {
  LMP_MODULE("garden", "Garden", "G", "1.0.0", sdk::ModuleKind::Hardware, 0)

 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }

  void things(sdk::ThingSink& out) const override {
    out.toggle("pump", {"Pump", "Pompa"}, pump, "outlet");
    out.number("level", {"Level", "Livello"}, level, "%", nullptr, 0, levelKnown);
    out.number("temp", {"Temperature", "Temperatura"}, temp, "\xC2\xB0" "C", "temperature", 1);
    out.flag("alarm", {"Alarm", "Allarme"}, alarm, "problem");
    out.technical();
    out.flag("fault", {"Sensor fault", "Sensore guasto"}, fault, "problem");
    out.text("mode", {"Mode", "Modo"}, mode);
    // A name the owner gave: one text.
    out.light("lamp", lampName.c_str(), lampOn, lampLevel, r, g, b, true);
    out.button("water", {"Water now", "Innaffia ora"});
    char id[8];
    for (int i = 1; i <= beds; ++i) {
      snprintf(id, sizeof(id), "t%d", i);
      out.part(id, names[i].c_str());
      out.number("moisture", {"Moisture", "Umidit\xC3\xA0"}, moisture[i], "%", "humidity");
      out.flag("dry", {"Dry", "Asciutto"}, moisture[i] < 20, "problem");
      out.technical();
      out.number("battery", {"Battery", "Batteria"}, 90, "%", "battery");
      out.button("refresh", {"Refresh", "Aggiorna"});
    }
    out.part(nullptr, nullptr);
  }

  Status thingSet(const char* id, const sdk::ThingAsk& ask) override {
    if (strcmp(id, "pump") == 0) {
      if (!ask.hasOn) return Status::Invalid;
      if (locked) return Status::Denied;
      pump = ask.on;
      markChanged();
      return Status::Ok;
    }
    if (strcmp(id, "water") == 0) {
      if (!ask.press) return Status::Invalid;
      ++watered;
      return Status::Ok;
    }
    if (strcmp(id, "lamp") == 0) {
      if (ask.hasOn) lampOn = ask.on;
      if (ask.hasBrightness) lampLevel = ask.brightness;
      if (ask.hasColour) {
        r = ask.r;
        g = ask.g;
        b = ask.b;
      }
      markChanged();
      return Status::Ok;
    }
    const char* dot = strchr(id, '.');
    if (id[0] == 't' && dot && strcmp(dot, ".refresh") == 0 && ask.press) {
      refreshed = atoi(id + 1);
      return refreshed >= 1 && refreshed <= beds ? Status::Ok : Status::NotFound;
    }
    return Status::NotFound;
  }

  void tell() { markChanged(); }

  Garden() {
    for (int i = 0; i < 80; ++i) {
      names[i] = "Bed " + std::to_string(i);
      moisture[i] = 50;
    }
  }

  bool pump = false, locked = false, alarm = false, fault = false;
  double level = 0, temp = 21.46;
  bool levelKnown = false;
  const char* mode = "auto";
  std::string lampName = "Greenhouse lamp";
  bool lampOn = false;
  uint8_t lampLevel = 40, r = 10, g = 20, b = 30;
  int beds = 3;
  std::string names[80];
  int moisture[80];
  int watered = 0, refreshed = 0;
};

// A module that has nothing to show.
class Plain : public sdk::Module {
  LMP_MODULE("plain", "Plain", "P", "1.0.0", sdk::ModuleKind::Headless, 0)

 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }
};

// The answer of a command, as the API and the bridge give it.
struct Answer {
  Json<8192> out;
  JsonValue v;
  Status status;

  Answer(OnlineSystem& sys, const char* target, const char* name, const char* args) {
    status = sys.kernel().commands().answer(target, name, args, ArgsForm::Json, CmdSource::Remote,
                                            out.w);
    TEST_ASSERT_TRUE_MESSAGE(out.w.complete(), name);
    v = out.parse()["result"];
  }
};

uint32_t revOf(OnlineSystem& sys) {
  Doc doc(sys.kernel(), "mod.garden");
  return static_cast<uint32_t>(doc.v["things"]["rev"].asInt());
}

uint32_t genOf(OnlineSystem& sys) {
  Doc doc(sys.kernel(), "mod.garden");
  return static_cast<uint32_t>(doc.v["things"]["gen"].asInt());
}

}  // namespace

void test_the_document_of_a_module_tells_its_things() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});

  Doc doc(sys.kernel(), "mod.garden");
  const JsonValue things = doc.v["things"];
  TEST_ASSERT_TRUE_MESSAGE(things.valid(), doc.text());
  TEST_ASSERT_TRUE(things["gen"].asInt() > 0);
  TEST_ASSERT_EQUAL_INT64(1, things["rev"].asInt());
  TEST_ASSERT_EQUAL_INT64(3, things["parts"].asInt());
  const JsonValue values = things["values"];
  // On or off, yes or no: true and false.
  TEST_ASSERT_TRUE(values["pump"].isBool());
  TEST_ASSERT_FALSE(values["pump"].asBool(true));
  TEST_ASSERT_FALSE(values["alarm"].asBool(true));
  TEST_ASSERT_FALSE(values["fault"].asBool(true));
  // Not known is not zero.
  TEST_ASSERT_TRUE(values["level"].valid());
  TEST_ASSERT_TRUE(values["level"].isNull());
  // A number as it is shown: one decimal.
  TEST_ASSERT_NOT_NULL(strstr(doc.text(), "\"temp\":21.5"));
  TEST_ASSERT_TRUE(values["mode"].stringEquals("auto"));
  // A light: all it is, in one object.
  TEST_ASSERT_FALSE(values["lamp"]["on"].asBool(true));
  TEST_ASSERT_EQUAL_INT64(40, values["lamp"]["brightness"].asInt());
  TEST_ASSERT_EQUAL_INT64(10, values["lamp"]["r"].asInt());
  TEST_ASSERT_EQUAL_INT64(30, values["lamp"]["b"].asInt());
  // A button has no value; the values of the parts are not here.
  TEST_ASSERT_FALSE(values["water"].valid());
  TEST_ASSERT_FALSE(values["moisture"].valid());

  // The level gets known: the value is there, and the rev says that something changed.
  garden.level = 73;
  garden.levelKnown = true;
  garden.tell();
  sys.advance(500);
  Doc after(sys.kernel(), "mod.garden");
  TEST_ASSERT_EQUAL_INT64(73, after.v["things"]["values"]["level"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(2, after.v["things"]["rev"].asInt());
  TEST_ASSERT_EQUAL_INT64(things["gen"].asInt(), after.v["things"]["gen"].asInt());

  // Told again with nothing new: the rev stays.
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT32(2, revOf(sys));
}

void test_a_module_without_things_says_nothing() {
  Plain plain;
  OnlineSystem sys(false, nullptr, 1500, {&plain});
  Doc doc(sys.kernel(), "mod.plain");
  TEST_ASSERT_FALSE(doc.v["things"].valid());
  // The commands are there for every module: who asks gets an empty list.
  Answer what(sys, "mod.plain", "things", "{}");
  TEST_ASSERT_TRUE(what.status == Status::Ok);
  TEST_ASSERT_EQUAL_INT64(0, what.v["gen"].asInt(-1));
  TEST_ASSERT_EQUAL_UINT(0, what.v["things"].size());
  TEST_ASSERT_FALSE(what.v["parts"].valid());
  TEST_ASSERT_TRUE(sys.command("mod.plain", "thing.set", "{\"thing\":\"x\",\"value\":\"ON\"}") ==
                   Status::NotFound);
}

void test_what_the_things_are() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  Answer what(sys, "mod.garden", "things", "{}");
  TEST_ASSERT_TRUE(what.status == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(genOf(sys), static_cast<uint32_t>(what.v["gen"].asInt()));

  const JsonValue things = what.v["things"];
  TEST_ASSERT_EQUAL_UINT(8, things.size());
  // A socket, with its name in the languages of the firmware.
  TEST_ASSERT_TRUE(things.at(0)["id"].stringEquals("pump"));
  TEST_ASSERT_TRUE(things.at(0)["kind"].stringEquals("toggle"));
  TEST_ASSERT_TRUE(things.at(0)["type"].stringEquals("outlet"));
  TEST_ASSERT_TRUE(things.at(0)["name"]["en"].stringEquals("Pump"));
  TEST_ASSERT_TRUE(things.at(0)["name"]["it"].stringEquals("Pompa"));
  TEST_ASSERT_FALSE(things.at(0)["technical"].valid());
  // Numbers: unit, decimals when it has some, what it measures.
  TEST_ASSERT_TRUE(things.at(1)["kind"].stringEquals("number"));
  TEST_ASSERT_TRUE(things.at(1)["unit"].stringEquals("%"));
  TEST_ASSERT_FALSE(things.at(1)["decimals"].valid());
  TEST_ASSERT_TRUE(things.at(2)["type"].stringEquals("temperature"));
  TEST_ASSERT_EQUAL_INT64(1, things.at(2)["decimals"].asInt());
  // Yes or no; and the one for who looks after the device, and only that one.
  TEST_ASSERT_TRUE(things.at(3)["kind"].stringEquals("flag"));
  TEST_ASSERT_TRUE(things.at(3)["type"].stringEquals("problem"));
  TEST_ASSERT_FALSE(things.at(3)["technical"].valid());
  TEST_ASSERT_TRUE(things.at(4)["id"].stringEquals("fault"));
  TEST_ASSERT_TRUE(things.at(4)["technical"].asBool(false));
  TEST_ASSERT_FALSE(things.at(5)["technical"].valid());
  TEST_ASSERT_TRUE(things.at(5)["kind"].stringEquals("text"));
  // A light with colours, with the name its owner gave: one text.
  TEST_ASSERT_TRUE(things.at(6)["kind"].stringEquals("light"));
  TEST_ASSERT_TRUE(things.at(6)["type"].stringEquals("rgb"));
  TEST_ASSERT_TRUE(things.at(6)["name"].stringEquals("Greenhouse lamp"));
  TEST_ASSERT_TRUE(things.at(7)["kind"].stringEquals("button"));

  // The things of a part, once for all the parts.
  const JsonValue part = what.v["partThings"];
  TEST_ASSERT_EQUAL_UINT(4, part.size());
  TEST_ASSERT_TRUE(part.at(0)["id"].stringEquals("moisture"));
  TEST_ASSERT_TRUE(part.at(0)["type"].stringEquals("humidity"));
  TEST_ASSERT_TRUE(part.at(1)["id"].stringEquals("dry"));
  TEST_ASSERT_TRUE(part.at(2)["id"].stringEquals("battery"));
  TEST_ASSERT_TRUE(part.at(2)["technical"].asBool(false));
  TEST_ASSERT_TRUE(part.at(3)["kind"].stringEquals("button"));
  TEST_ASSERT_FALSE(part.at(3)["technical"].valid());

  // And the parts, by id and name.
  const JsonValue parts = what.v["parts"];
  TEST_ASSERT_EQUAL_UINT(3, parts.size());
  TEST_ASSERT_TRUE(parts.at(0)["id"].stringEquals("t1"));
  TEST_ASSERT_TRUE(parts.at(0)["name"].stringEquals("Bed 1"));
  TEST_ASSERT_TRUE(parts.at(2)["id"].stringEquals("t3"));
  TEST_ASSERT_FALSE(what.v["next"].valid());
}

void test_many_parts_are_told_a_page_at_a_time() {
  Garden garden;
  garden.beds = 40;
  OnlineSystem sys(false, nullptr, 1500, {&garden});

  Answer first(sys, "mod.garden", "things", "{}");
  TEST_ASSERT_EQUAL_UINT(sdk::kThingPartsPage, first.v["parts"].size());
  TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(sdk::kThingPartsPage), first.v["next"].asInt());
  TEST_ASSERT_EQUAL_UINT(8, first.v["things"].size());
  // The next page has the parts only: what the things are was said.
  Answer second(sys, "mod.garden", "things", "{\"from\":32}");
  TEST_ASSERT_EQUAL_UINT(8, second.v["parts"].size());
  TEST_ASSERT_TRUE(second.v["parts"].at(0)["id"].stringEquals("t33"));
  TEST_ASSERT_TRUE(second.v["parts"].at(7)["id"].stringEquals("t40"));
  TEST_ASSERT_FALSE(second.v["next"].valid());
  TEST_ASSERT_FALSE(second.v["things"].valid());
  TEST_ASSERT_FALSE(second.v["partThings"].valid());
  TEST_ASSERT_EQUAL_INT64(first.v["gen"].asInt(), second.v["gen"].asInt());

  // The values of all the parts: since 0, a page after the other.
  int seen = 0;
  uint32_t from = 0;
  for (int page = 0; page < 6; ++page) {
    char args[32];
    snprintf(args, sizeof(args), "{\"since\":0,\"from\":%u}", static_cast<unsigned>(from));
    Answer values(sys, "mod.garden", "things.values", args);
    TEST_ASSERT_TRUE(values.status == Status::Ok);
    TEST_ASSERT_EQUAL_INT64(1, values.v["rev"].asInt());
    const JsonValue parts = values.v["parts"];
    TEST_ASSERT_TRUE(parts.size() <= sdk::kThingValuesPage);
    if (page == 0) {
      TEST_ASSERT_TRUE(parts.at(0)["id"].stringEquals("t1"));
      TEST_ASSERT_EQUAL_INT64(50, parts.at(0)["values"]["moisture"].asInt());
      TEST_ASSERT_FALSE(parts.at(0)["values"]["dry"].asBool(true));
      TEST_ASSERT_EQUAL_INT64(90, parts.at(0)["values"]["battery"].asInt());
      TEST_ASSERT_FALSE(parts.at(0)["values"]["refresh"].valid());
    }
    seen += static_cast<int>(parts.size());
    if (!values.v["next"].valid()) break;
    from = static_cast<uint32_t>(values.v["next"].asInt());
  }
  TEST_ASSERT_EQUAL_INT(40, seen);
}

void test_only_what_changed_is_read_again() {
  Garden garden;
  garden.beds = 40;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  const uint32_t gen = genOf(sys);
  TEST_ASSERT_EQUAL_UINT32(1, revOf(sys));
  // Nothing after the rev the reader has.
  {
    Answer none(sys, "mod.garden", "things.values", "{\"since\":1}");
    TEST_ASSERT_EQUAL_UINT(0, none.v["parts"].size());
  }
  // Two beds dry up.
  garden.moisture[7] = 12;
  garden.moisture[31] = 5;
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT32(2, revOf(sys));
  TEST_ASSERT_EQUAL_UINT32(gen, genOf(sys));   // the same things: nothing to describe again
  {
    Answer changed(sys, "mod.garden", "things.values", "{\"since\":1}");
    TEST_ASSERT_EQUAL_INT64(2, changed.v["rev"].asInt());
    TEST_ASSERT_EQUAL_UINT(2, changed.v["parts"].size());
    TEST_ASSERT_TRUE(changed.v["parts"].at(0)["id"].stringEquals("t7"));
    TEST_ASSERT_EQUAL_INT64(12, changed.v["parts"].at(0)["values"]["moisture"].asInt());
    TEST_ASSERT_TRUE(changed.v["parts"].at(0)["values"]["dry"].asBool(false));
    TEST_ASSERT_TRUE(changed.v["parts"].at(1)["id"].stringEquals("t31"));
  }
  // One of the two again: who has the rev 2 reads that one only.
  garden.moisture[31] = 60;
  garden.tell();
  sys.advance(500);
  {
    Answer changed(sys, "mod.garden", "things.values", "{\"since\":2}");
    TEST_ASSERT_EQUAL_INT64(3, changed.v["rev"].asInt());
    TEST_ASSERT_EQUAL_UINT(1, changed.v["parts"].size());
    TEST_ASSERT_TRUE(changed.v["parts"].at(0)["id"].stringEquals("t31"));
  }
}

void test_the_description_has_its_own_number() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  const uint32_t gen = genOf(sys);
  const uint32_t rev = revOf(sys);

  // A bed more: the description changed, and so did the rev.
  garden.beds = 4;
  garden.tell();
  sys.advance(500);
  const uint32_t more = genOf(sys);
  TEST_ASSERT_TRUE(more != gen);
  TEST_ASSERT_EQUAL_UINT32(rev + 1, revOf(sys));
  {
    Doc doc(sys.kernel(), "mod.garden");
    TEST_ASSERT_EQUAL_INT64(4, doc.v["things"]["parts"].asInt());
    // The new one is among what changed.
    Answer changed(sys, "mod.garden", "things.values", "{\"since\":1}");
    TEST_ASSERT_EQUAL_UINT(1, changed.v["parts"].size());
    TEST_ASSERT_TRUE(changed.v["parts"].at(0)["id"].stringEquals("t4"));
  }
  // A bed renamed, a light renamed: described again.
  garden.names[2] = "Tomatoes";
  garden.tell();
  sys.advance(500);
  const uint32_t renamed = genOf(sys);
  TEST_ASSERT_TRUE(renamed != more);
  garden.lampName = "Light";
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_TRUE(genOf(sys) != renamed);
  // A bed less: gone from the parts.
  garden.beds = 3;
  garden.tell();
  sys.advance(500);
  Answer what(sys, "mod.garden", "things", "{}");
  TEST_ASSERT_EQUAL_UINT(3, what.v["parts"].size());
  TEST_ASSERT_TRUE(what.v["parts"].at(1)["name"].stringEquals("Tomatoes"));
  // The number is of the description, not of the moment: the same after a restart.
  Garden again;
  again.lampName = "Light";
  again.names[2] = "Tomatoes";
  OnlineSystem other(false, nullptr, 1500, {&again});
  Doc doc(other.kernel(), "mod.garden");
  TEST_ASSERT_EQUAL_UINT32(genOf(sys), static_cast<uint32_t>(doc.v["things"]["gen"].asInt()));
}

void test_a_thing_is_set_with_one_command() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});

  // On, off: the same words for every module.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\",\"value\":\"ON\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(garden.pump);
  sys.advance(500);
  {
    Doc doc(sys.kernel(), "mod.garden");
    TEST_ASSERT_TRUE(doc.v["things"]["values"]["pump"].asBool(false));
    TEST_ASSERT_EQUAL_INT64(2, doc.v["things"]["rev"].asInt());
  }
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\",\"value\":\"OFF\"}") ==
                   Status::Ok);
  TEST_ASSERT_FALSE(garden.pump);
  // What the module refuses is refused: a channel a schedule holds.
  garden.locked = true;
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\",\"value\":\"ON\"}") ==
                   Status::Denied);
  // A button, of the module and of a part.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"water\",\"value\":\"PRESS\"}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_INT(1, garden.watered);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"t3.refresh\",\"value\":\"PRESS\"}") == Status::Ok);
  TEST_ASSERT_EQUAL_INT(3, garden.refreshed);
  // A light: on, how bright, which colour.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"lamp\",\"value\":\"ON\",\"brightness\":80,"
                               "\"r\":255,\"g\":0,\"b\":64}") == Status::Ok);
  TEST_ASSERT_TRUE(garden.lampOn);
  TEST_ASSERT_EQUAL_UINT8(80, garden.lampLevel);
  TEST_ASSERT_EQUAL_UINT8(255, garden.r);
  TEST_ASSERT_EQUAL_UINT8(64, garden.b);
  // Only the brightness: the rest stays.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"lamp\",\"brightness\":15}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(garden.lampOn);
  TEST_ASSERT_EQUAL_UINT8(15, garden.lampLevel);

  // What is wrong is told as for every command.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"door\",\"value\":\"ON\"}",
                               &field) == Status::NotFound);
  TEST_ASSERT_EQUAL_STRING("thing", field);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\"}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\",\"value\":\"MAYBE\"}") ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"value\":\"ON\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"lamp\",\"brightness\":0}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("brightness", field);
}

void test_more_parts_than_the_book_holds() {
  Garden garden;
  garden.beds = LMP_THINGS_PARTS + 6;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  const uint32_t rev = revOf(sys);
  // Who has the rev of now reads nothing.
  {
    char args[32];
    snprintf(args, sizeof(args), "{\"since\":%u}", static_cast<unsigned>(rev));
    Answer none(sys, "mod.garden", "things.values", args);
    TEST_ASSERT_EQUAL_UINT(0, none.v["parts"].size());
  }
  // One of the beds the book has no room for changes: the rev goes up all the same, and who
  // asks gets it, with the others left out.
  garden.moisture[LMP_THINGS_PARTS + 6] = 3;
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT32(rev + 1, revOf(sys));
  char args[32];
  snprintf(args, sizeof(args), "{\"since\":%u}", static_cast<unsigned>(rev));
  Answer changed(sys, "mod.garden", "things.values", args);
  TEST_ASSERT_EQUAL_UINT(6, changed.v["parts"].size());
  char last[8];
  snprintf(last, sizeof(last), "t%d", LMP_THINGS_PARTS + 6);
  TEST_ASSERT_TRUE(changed.v["parts"].at(5)["id"].stringEquals(last));
  TEST_ASSERT_EQUAL_INT64(3, changed.v["parts"].at(5)["values"]["moisture"].asInt());
}

void test_the_modules_of_the_products_say_their_things() {
  // A power strip: its lines are sockets, with the name the owner gives.
  OnlineSystem sys(false, nullptr, 1500, {&lmp_module_zappy2()});
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "name", "{\"channel\":0,\"name\":\"Lamp\"}") ==
                   Status::Ok);
  sys.advance(500);
  Answer what(sys, "mod.zappy2", "things", "{}");
  TEST_ASSERT_EQUAL_UINT(2, what.v["things"].size());
  TEST_ASSERT_TRUE(what.v["things"].at(0)["id"].stringEquals("ch0"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["kind"].stringEquals("toggle"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["type"].stringEquals("outlet"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["name"].stringEquals("Lamp"));
  TEST_ASSERT_FALSE(what.v["partThings"].valid());
  // Turned on as the server does it, and seen in the document.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "thing.set", "{\"thing\":\"ch1\",\"value\":\"ON\"}") ==
                   Status::Ok);
  sys.advance(500);
  Doc doc(sys.kernel(), "mod.zappy2");
  TEST_ASSERT_FALSE(doc.v["things"]["values"]["ch0"].asBool(true));
  TEST_ASSERT_TRUE(doc.v["things"]["values"]["ch1"].asBool(false));
  TEST_ASSERT_FALSE(doc.v["things"]["parts"].valid());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_document_of_a_module_tells_its_things);
  RUN_TEST(test_a_module_without_things_says_nothing);
  RUN_TEST(test_what_the_things_are);
  RUN_TEST(test_many_parts_are_told_a_page_at_a_time);
  RUN_TEST(test_only_what_changed_is_read_again);
  RUN_TEST(test_the_description_has_its_own_number);
  RUN_TEST(test_a_thing_is_set_with_one_command);
  RUN_TEST(test_more_parts_than_the_book_holds);
  RUN_TEST(test_the_modules_of_the_products_say_their_things);
  return UNITY_END();
}
