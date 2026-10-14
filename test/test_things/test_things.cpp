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
lmp::sdk::Module& lmp_module_rgb();
lmp::sdk::Module& lmp_module_ganjaleaf();
lmp::sdk::Module& lmp_module_meteowifi();
lmp::sdk::Module& lmp_module_espradio();
lmp::sdk::Module& lmp_module_expnet();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

// The programs of the garden, to choose among; and what it is doing, only to read.
constexpr sdk::ThingOption kPrograms[] = {
    {"eco", {"Eco", "Risparmio"}},
    {"full", {"Full", "Completo"}},
    {"night", {"Night", "Notturno"}},
};
constexpr sdk::ThingOption kPhases[] = {
    {"idle", {"Idle", "Fermo"}},
    {"watering", {"Watering", "Innaffia"}},
};

// A garden: things of its own of every kind, and beds that are devices of their own.
class Garden : public sdk::Module {
  LMP_MODULE("garden", "Garden", "G", "1.0.0", sdk::ModuleKind::Hardware, 0)

 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }

  void things(sdk::ThingSink& out) const override {
    out.toggle("pump", {"Pump", "Pompa"}, pump, "outlet");
    out.measured(levelAt);
    out.range(0, 100);
    out.number("level", {"Level", "Livello"}, level, "%", nullptr, 0, levelKnown);
    out.number("temp", {"Temperature", "Temperatura"}, temp, "\xC2\xB0" "C", "temperature", 1);
    out.flag("alarm", {"Alarm", "Allarme"}, alarm, "problem");
    out.technical();
    out.flag("fault", {"Sensor fault", "Sensore guasto"}, fault, "problem");
    out.text("mode", {"Mode", "Modo"}, mode);
    // A name the owner gave: one text.
    out.light("lamp", lampName.c_str(), lampOn, lampLevel, r, g, b, true);
    out.button("water", {"Water now", "Innaffia ora"});
    // What can be set: a number with its limits and its step, a text, one among fixed values.
    out.settable(5, targetMax, 0.5);
    out.number("target", {"Target", "Voluta"}, target, "\xC2\xB0" "C", "temperature", 1);
    out.settable(0, 20);
    out.text("label", {"Label", "Etichetta"}, label.c_str());
    out.settable();
    out.choice("program", {"Program", "Programma"}, program.c_str(), kPrograms);
    out.choice("phase", {"Phase", "Fase"}, pump ? "watering" : "idle", kPhases);
    char id[8];
    for (int i = 1; i <= beds; ++i) {
      snprintf(id, sizeof(id), "t%d", i);
      out.part(id, names[i].c_str());
      out.measured(heard[i]);
      out.number("moisture", {"Moisture", "Umidit\xC3\xA0"}, moisture[i], "%", "humidity");
      out.flag("dry", {"Dry", "Asciutto"}, moisture[i] < 20, "problem");
      out.technical();
      out.number("battery", {"Battery", "Batteria"}, 90, "%", "battery");
      out.button("refresh", {"Refresh", "Aggiorna"});
      out.settable(1, 48, 1);
      out.number("every", {"Hours between two waterings", "Ore tra due innaffiature"}, every[i],
                 "h");
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
    // What arrives here was checked: the module takes it as it is.
    if (strcmp(id, "target") == 0) {
      target = ask.number;
      ++set;
      markChanged();
      return Status::Ok;
    }
    if (strcmp(id, "label") == 0) {
      label = ask.text;
      ++set;
      markChanged();
      return Status::Ok;
    }
    if (strcmp(id, "program") == 0) {
      program = ask.text;
      ++set;
      markChanged();
      return Status::Ok;
    }
    const char* dot = strchr(id, '.');
    if (id[0] == 't' && dot && strcmp(dot, ".refresh") == 0 && ask.press) {
      refreshed = atoi(id + 1);
      return refreshed >= 1 && refreshed <= beds ? Status::Ok : Status::NotFound;
    }
    if (id[0] == 't' && dot && strcmp(dot, ".every") == 0) {
      every[atoi(id + 1)] = static_cast<int>(ask.number);
      ++set;
      markChanged();
      return Status::Ok;
    }
    return Status::NotFound;
  }

  void tell() { markChanged(); }

  Garden() {
    for (int i = 0; i < 80; ++i) {
      names[i] = "Bed " + std::to_string(i);
      moisture[i] = 50;
      every[i] = 24;
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
  int every[80];
  int64_t levelAt = 0;       // when the level was measured, Unix seconds; 0: not known
  int64_t heard[80] = {};    // when each bed told its moisture
  int watered = 0, refreshed = 0;
  double target = 21.5, targetMax = 30;
  std::string label;
  std::string program = "eco";
  int set = 0;   // values that reached the module
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
  TEST_ASSERT_EQUAL_UINT(12, things.size());
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
  TEST_ASSERT_EQUAL_UINT(5, part.size());
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
  TEST_ASSERT_EQUAL_UINT(12, first.v["things"].size());
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

void test_a_number_says_its_limits_and_what_can_be_set_says_so() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  Answer what(sys, "mod.garden", "things", "{}");
  const JsonValue things = what.v["things"];

  // A number that is only read: from where to where it goes, and nothing else.
  TEST_ASSERT_TRUE(things.at(1)["id"].stringEquals("level"));
  TEST_ASSERT_EQUAL_INT64(0, things.at(1)["min"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(100, things.at(1)["max"].asInt(-1));
  TEST_ASSERT_FALSE(things.at(1)["step"].valid());
  TEST_ASSERT_FALSE(things.at(1)["writable"].valid());
  // One that says nothing has nothing more than before.
  TEST_ASSERT_FALSE(things.at(2)["min"].valid());
  TEST_ASSERT_FALSE(things.at(2)["writable"].valid());
  // What is turned or pressed was always so: it does not say it.
  TEST_ASSERT_FALSE(things.at(0)["writable"].valid());

  // A number that can be set: its limits and its step.
  TEST_ASSERT_TRUE(things.at(8)["id"].stringEquals("target"));
  TEST_ASSERT_TRUE(things.at(8)["kind"].stringEquals("number"));
  TEST_ASSERT_TRUE(things.at(8)["writable"].asBool(false));
  TEST_ASSERT_EQUAL_INT64(5, things.at(8)["min"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(30, things.at(8)["max"].asInt(-1));
  TEST_ASSERT_NOT_NULL(strstr(what.out.text(), "\"step\":0.5"));
  // A text: how long it can be.
  TEST_ASSERT_TRUE(things.at(9)["id"].stringEquals("label"));
  TEST_ASSERT_TRUE(things.at(9)["kind"].stringEquals("text"));
  TEST_ASSERT_TRUE(things.at(9)["writable"].asBool(false));
  TEST_ASSERT_EQUAL_INT64(20, things.at(9)["max"].asInt(-1));
  // One among fixed values: each with its id and its name.
  TEST_ASSERT_TRUE(things.at(10)["id"].stringEquals("program"));
  TEST_ASSERT_TRUE(things.at(10)["kind"].stringEquals("select"));
  TEST_ASSERT_TRUE(things.at(10)["writable"].asBool(false));
  TEST_ASSERT_EQUAL_UINT(3, things.at(10)["options"].size());
  TEST_ASSERT_TRUE(things.at(10)["options"].at(0)["id"].stringEquals("eco"));
  TEST_ASSERT_TRUE(things.at(10)["options"].at(0)["name"]["it"].stringEquals("Risparmio"));
  TEST_ASSERT_TRUE(things.at(10)["options"].at(2)["id"].stringEquals("night"));
  // The same kind, only to read: a state with its values.
  TEST_ASSERT_TRUE(things.at(11)["id"].stringEquals("phase"));
  TEST_ASSERT_TRUE(things.at(11)["kind"].stringEquals("select"));
  TEST_ASSERT_FALSE(things.at(11)["writable"].valid());
  TEST_ASSERT_EQUAL_UINT(2, things.at(11)["options"].size());
  // And the one of a part.
  const JsonValue part = what.v["partThings"];
  TEST_ASSERT_TRUE(part.at(4)["id"].stringEquals("every"));
  TEST_ASSERT_TRUE(part.at(4)["writable"].asBool(false));
  TEST_ASSERT_EQUAL_INT64(48, part.at(4)["max"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(1, part.at(4)["step"].asInt(-1));

  // Their values are among the others.
  Doc doc(sys.kernel(), "mod.garden");
  const JsonValue values = doc.v["things"]["values"];
  TEST_ASSERT_NOT_NULL(strstr(doc.text(), "\"target\":21.5"));
  TEST_ASSERT_TRUE(values["label"].stringEquals(""));
  TEST_ASSERT_TRUE(values["program"].stringEquals("eco"));
  TEST_ASSERT_TRUE(values["phase"].stringEquals("idle"));

  // Other limits are another description.
  const uint32_t gen = genOf(sys);
  garden.targetMax = 28;
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_TRUE(genOf(sys) != gen);
}

void test_a_number_says_when_it_was_measured() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  const uint32_t gen = genOf(sys);
  // No time said: nothing beside the values.
  {
    Doc doc(sys.kernel(), "mod.garden");
    TEST_ASSERT_FALSE(doc.v["things"]["at"].valid());
    TEST_ASSERT_FALSE(doc.v["things"]["kept"].valid());
  }
  // Measured: its time is beside its value, as Unix seconds.
  garden.level = 73;
  garden.levelKnown = true;
  garden.levelAt = 1790000000;
  garden.tell();
  sys.advance(500);
  {
    Doc doc(sys.kernel(), "mod.garden");
    TEST_ASSERT_EQUAL_INT64(73, doc.v["things"]["values"]["level"].asInt(-1));
    TEST_ASSERT_EQUAL_INT64(1790000000, doc.v["things"]["at"]["level"].asInt());
    // Only the numbers that say it.
    TEST_ASSERT_FALSE(doc.v["things"]["at"]["temp"].valid());
    TEST_ASSERT_EQUAL_UINT(1, doc.v["things"]["at"].size());
    TEST_ASSERT_EQUAL_INT64(2, doc.v["things"]["rev"].asInt());
  }
  // Measured again and found the same: the time is another, and the rev says that there is
  // something to read. A value that stands still is not a sensor that is silent.
  garden.levelAt = 1790000600;
  garden.tell();
  sys.advance(500);
  {
    Doc doc(sys.kernel(), "mod.garden");
    TEST_ASSERT_EQUAL_INT64(73, doc.v["things"]["values"]["level"].asInt(-1));
    TEST_ASSERT_EQUAL_INT64(1790000600, doc.v["things"]["at"]["level"].asInt());
    TEST_ASSERT_EQUAL_INT64(3, doc.v["things"]["rev"].asInt());
  }
  // Told again without a new measure: nothing to read.
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT32(3, revOf(sys));

  // A part heard again with the same values: that part, and only that one, is read again,
  // with its time beside its values.
  garden.heard[2] = 1790000700;
  garden.tell();
  sys.advance(500);
  TEST_ASSERT_EQUAL_UINT32(4, revOf(sys));
  {
    Answer changed(sys, "mod.garden", "things.values", "{\"since\":3}");
    TEST_ASSERT_EQUAL_UINT(1, changed.v["parts"].size());
    TEST_ASSERT_TRUE(changed.v["parts"].at(0)["id"].stringEquals("t2"));
    TEST_ASSERT_EQUAL_INT64(50, changed.v["parts"].at(0)["values"]["moisture"].asInt());
    TEST_ASSERT_EQUAL_INT64(1790000700, changed.v["parts"].at(0)["at"]["moisture"].asInt());
    TEST_ASSERT_FALSE(changed.v["parts"].at(0)["at"]["battery"].valid());
  }
  // A part that said no time has none.
  {
    Answer all(sys, "mod.garden", "things.values", "{}");
    TEST_ASSERT_EQUAL_UINT(3, all.v["parts"].size());
    TEST_ASSERT_FALSE(all.v["parts"].at(0)["at"].valid());
    TEST_ASSERT_TRUE(all.v["parts"].at(1)["at"].valid());
  }
  // When a number was measured is a value, not what the number is.
  TEST_ASSERT_EQUAL_UINT32(gen, genOf(sys));
}

void test_a_value_is_set_within_what_the_thing_takes() {
  Garden garden;
  OnlineSystem sys(false, nullptr, 1500, {&garden});
  const char* field = nullptr;

  // A number, as it is.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"number\":24}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(24.0, garden.target);
  // Between two steps: the nearest one.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"target\",\"number\":22.3}") == Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(22.5, garden.target);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"target\",\"number\":22.2}") == Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(22.0, garden.target);
  // The limits themselves are good.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"number\":5}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"number\":30}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(30.0, garden.target);
  // Out of them: refused before the module sees it, and the answer says what.
  const int before = garden.set;
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"number\":31}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("number", field);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"number\":4.4}") ==
                   Status::Invalid);
  // A number is not a text.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"target\",\"value\":\"24\"}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("value", field);
  TEST_ASSERT_EQUAL_INT(before, garden.set);
  TEST_ASSERT_EQUAL_DOUBLE(30.0, garden.target);

  // A text, up to its length.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"label\",\"value\":\"Tomatoes\"}") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Tomatoes", garden.label.c_str());
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"label\",\"value\":\"123456789012345678901\"}") ==
                   Status::Invalid);
  // The word that turns a switch on is, for a text, a text like another.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"label\",\"value\":\"ON\"}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_STRING("ON", garden.label.c_str());
  // Emptied: an empty text is a text.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"label\",\"value\":\"\"}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_STRING("", garden.label.c_str());

  // One of the values, by its id; another word is not taken.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"program\",\"value\":\"night\"}") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("night", garden.program.c_str());
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"program\",\"value\":\"Night\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("value", field);
  TEST_ASSERT_EQUAL_STRING("night", garden.program.c_str());

  // What is only read is not set: a state, a number that is measured.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"phase\",\"value\":\"watering\"}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"level\",\"number\":50}") ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"temp\",\"number\":20}") ==
                   Status::Invalid);
  // A number means nothing to a switch; with ON beside it, the switch is turned.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"pump\",\"number\":1}") ==
                   Status::Invalid);
  TEST_ASSERT_FALSE(garden.pump);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"pump\",\"value\":\"ON\",\"number\":1}") == Status::Ok);
  TEST_ASSERT_TRUE(garden.pump);

  // The one of a part; a part that is not there.
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set",
                               "{\"thing\":\"t2.every\",\"number\":12}") == Status::Ok);
  TEST_ASSERT_EQUAL_INT(12, garden.every[2]);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"t2.every\",\"number\":49}") ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"t9.every\",\"number\":12}",
                               &field) == Status::NotFound);
  TEST_ASSERT_EQUAL_STRING("thing", field);
  TEST_ASSERT_TRUE(sys.command("mod.garden", "thing.set", "{\"thing\":\"door\",\"number\":1}") ==
                   Status::NotFound);

  // From the command line the same: the words of every interface.
  CmdContext ctx;
  ctx.source = CmdSource::Serial;
  TEST_ASSERT_TRUE(sys.kernel().commands().executeText("mod.garden", "thing.set",
                                                       "thing=target number=18.5", ctx) ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(18.5, garden.target);
  TEST_ASSERT_TRUE(sys.kernel().commands().executeText("mod.garden", "thing.set",
                                                       "thing=program value=full", ctx) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("full", garden.program.c_str());

  // Who reads finds what was set, and that something changed.
  sys.advance(500);
  Doc doc(sys.kernel(), "mod.garden");
  TEST_ASSERT_NOT_NULL(strstr(doc.text(), "\"target\":18.5"));
  TEST_ASSERT_TRUE(doc.v["things"]["values"]["program"].stringEquals("full"));
  TEST_ASSERT_TRUE(doc.v["things"]["values"]["phase"].stringEquals("watering"));
  TEST_ASSERT_TRUE(doc.v["things"]["rev"].asInt() > 1);
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
  TEST_ASSERT_EQUAL_UINT(3, what.v["things"].size());
  TEST_ASSERT_TRUE(what.v["things"].at(0)["id"].stringEquals("ch0"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["kind"].stringEquals("toggle"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["type"].stringEquals("outlet"));
  TEST_ASSERT_TRUE(what.v["things"].at(0)["name"].stringEquals("Lamp"));
  TEST_ASSERT_FALSE(what.v["partThings"].valid());
  // And a button for all of them: what the command "flip" does, to press.
  TEST_ASSERT_TRUE(what.v["things"].at(2)["id"].stringEquals("flip"));
  TEST_ASSERT_TRUE(what.v["things"].at(2)["kind"].stringEquals("button"));
  TEST_ASSERT_TRUE(what.v["things"].at(2)["name"]["it"].stringEquals("Inverti"));
  // Turned on as the server does it, and seen in the document.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "thing.set", "{\"thing\":\"ch1\",\"value\":\"ON\"}") ==
                   Status::Ok);
  sys.advance(500);
  {
    Doc doc(sys.kernel(), "mod.zappy2");
    TEST_ASSERT_FALSE(doc.v["things"]["values"]["ch0"].asBool(true));
    TEST_ASSERT_TRUE(doc.v["things"]["values"]["ch1"].asBool(false));
    TEST_ASSERT_FALSE(doc.v["things"]["parts"].valid());
  }
  // The button, pressed: each line becomes the opposite of what it was.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "thing.set", "{\"thing\":\"flip\",\"value\":\"PRESS\"}") ==
                   Status::Ok);
  sys.advance(500);
  Doc doc(sys.kernel(), "mod.zappy2");
  TEST_ASSERT_TRUE(doc.v["things"]["values"]["ch0"].asBool(false));
  TEST_ASSERT_FALSE(doc.v["things"]["values"]["ch1"].asBool(true));
  // It is pressed, not turned.
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "thing.set", "{\"thing\":\"flip\",\"value\":\"ON\"}") ==
                   Status::Invalid);
}

// The rule: every module tells its things, what who uses it reads and commands. Without them
// a home system and a server see nothing of the module but its name. A new module is added
// to this list.
void test_every_module_tells_its_things() {
  sdk::Module* const modules[] = {&lmp_module_zappy2(),    &lmp_module_rgb(),      &lmp_module_ganjaleaf(),
                                  &lmp_module_meteowifi(), &lmp_module_espradio(), &lmp_module_expnet()};
  for (sdk::Module* module : modules) {
    const char* name = module->manifest().name;
    OnlineSystem sys(true, nullptr, 1500, {module});
    sys.advance(1000);
    char target[40];
    snprintf(target, sizeof(target), "mod.%s", name);
    Answer what(sys, target, "things", "{}");
    TEST_ASSERT_TRUE_MESSAGE(what.status == Status::Ok, name);
    TEST_ASSERT_TRUE_MESSAGE(what.v["gen"].asInt() > 0, name);
    TEST_ASSERT_TRUE_MESSAGE(what.v["things"].size() > 0, name);
    // And every thing has an id and a name to show.
    for (size_t i = 0; i < what.v["things"].size(); ++i) {
      TEST_ASSERT_TRUE_MESSAGE(what.v["things"].at(i)["id"].stringLength() > 0, name);
      TEST_ASSERT_TRUE_MESSAGE(what.v["things"].at(i)["name"].valid(), name);
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_module_tells_its_things);
  RUN_TEST(test_the_document_of_a_module_tells_its_things);
  RUN_TEST(test_a_module_without_things_says_nothing);
  RUN_TEST(test_what_the_things_are);
  RUN_TEST(test_many_parts_are_told_a_page_at_a_time);
  RUN_TEST(test_only_what_changed_is_read_again);
  RUN_TEST(test_the_description_has_its_own_number);
  RUN_TEST(test_a_thing_is_set_with_one_command);
  RUN_TEST(test_a_number_says_its_limits_and_what_can_be_set_says_so);
  RUN_TEST(test_a_number_says_when_it_was_measured);
  RUN_TEST(test_a_value_is_set_within_what_the_thing_takes);
  RUN_TEST(test_more_parts_than_the_book_holds);
  RUN_TEST(test_the_modules_of_the_products_say_their_things);
  return UNITY_END();
}
