// Tests on the PC of EspRadio: what the device keeps for the radio of the page, the
// favourite stations with their categories. The sound and the directories of the stations are of
// the browser, and are tried there.
#include "../support/online_system.h"

#include <cstdio>
#include <string>

using namespace lmp;
using namespace lmp::test;

lmp::sdk::Module& lmp_module_espradio();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kTarget = "mod.espradio";

struct Box {
  OnlineSystem sys{true, nullptr, 1500, {&lmp_module_espradio()}};
  Status command(const char* name, const char* args, const char** field = nullptr) {
    return sys.command(kTarget, name, args, field);
  }
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
};

bool has(const std::string& text, const char* piece) { return text.find(piece) != std::string::npos; }

}  // namespace

// A favourite arrives in two commands, as much text as a command carries: name and address, then
// logo and info. It is there after a start too.
void test_a_favourite_is_kept() {
  Box box;
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"r6b0c9901ae104f5e\",\"name\":\"Rai Radio 2\","
                                          "\"url\":\"https://icestreaming.rai.it/2.mp3\",\"cat\":\"Italy\"}") == Status::Ok);
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"r6b0c9901ae104f5e\",\"logo\":\"https://example.com/rai2.png\","
                                          "\"info\":\"IT \xC2\xB7 pop \xC2\xB7 MP3 256\"}") == Status::Ok);
  std::string r = box.result("favs", "{}");
  TEST_ASSERT_TRUE(has(r, "\"count\":1"));
  TEST_ASSERT_TRUE(has(r, "{\"i\":\"r6b0c9901ae104f5e\",\"n\":\"Rai Radio 2\",\"u\":\"https://icestreaming.rai.it/2.mp3\","
                          "\"c\":\"Italy\",\"l\":\"https://example.com/rai2.png\",\"t\":\"IT \xC2\xB7 pop \xC2\xB7 MP3 256\"}"));
  TEST_ASSERT_FALSE(has(r, "\"next\""));
  TEST_ASSERT_TRUE(box.sys.command("sys.modules", "restart", "{\"module\":\"espradio\"}") == Status::Ok);
  box.sys.advance(500);
  Doc doc(box.sys.kernel(), kTarget);
  TEST_ASSERT_EQUAL_INT(1, doc.v["favs"].asInt());
  TEST_ASSERT_TRUE(doc.v["max"].asInt() >= 30);
  TEST_ASSERT_TRUE(has(box.result("favs", "{}"), "Rai Radio 2"));
  // Who reads the device without knowing the module finds how many stations it keeps, and
  // how many it can keep.
  TEST_ASSERT_EQUAL_INT64(1, doc.v["things"]["values"]["favs"].asInt(-1));
  const std::string things = box.result("things", "{}");
  TEST_ASSERT_TRUE(has(things, "\"id\":\"favs\",\"kind\":\"number\""));
  TEST_ASSERT_TRUE(has(things, ("\"min\":0,\"max\":" + std::to_string(doc.v["max"].asInt())).c_str()));
}

void test_a_new_favourite_wants_a_name_and_an_address() {
  Box box;
  const char* field = nullptr;
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"sgroovesalad\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("name", field);
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"sgroovesalad\",\"name\":\"Groove Salad\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("url", field);
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"sgroovesalad\",\"name\":\"Groove Salad\",\"url\":\"ftp://somafm.com/x\"}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("url", field);
  // An id is letters, digits and . _ : - only.
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a\\\"b\",\"name\":\"X\",\"url\":\"http://x.example/s\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("id", field);
  TEST_ASSERT_TRUE(has(box.result("favs", "{}"), "\"count\":0"));
}

void test_categories_and_removing() {
  Box box;
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a1\",\"name\":\"One\",\"url\":\"http://a.example/1\",\"cat\":\"Rock\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a2\",\"name\":\"Two \\\"live\\\"\",\"url\":\"http://a.example/2\",\"cat\":\"Rock\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a3\",\"name\":\"Three\",\"url\":\"http://a.example/3\",\"cat\":\"Jazz\"}") ==
                   Status::Ok);
  // Another category for one, by its id alone; another name for a category, for all of it.
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a3\",\"cat\":\"Evening\"}") == Status::Ok);
  TEST_ASSERT_TRUE(box.command("cat.rename", "{\"from\":\"Rock\",\"to\":\"Rock hits\"}") == Status::Ok);
  std::string r = box.result("favs", "{}");
  TEST_ASSERT_TRUE(has(r, "\"n\":\"One\",\"u\":\"http://a.example/1\",\"c\":\"Rock hits\""));
  TEST_ASSERT_TRUE(has(r, "\"n\":\"Two \\\"live\\\"\",\"u\":\"http://a.example/2\",\"c\":\"Rock hits\""));
  TEST_ASSERT_TRUE(has(r, "\"n\":\"Three\",\"u\":\"http://a.example/3\",\"c\":\"Evening\""));
  // A category taken away leaves its stations without one.
  TEST_ASSERT_TRUE(box.command("cat.rename", "{\"from\":\"Evening\",\"to\":\"\"}") == Status::Ok);
  TEST_ASSERT_TRUE(has(box.result("favs", "{}"), "\"n\":\"Three\",\"u\":\"http://a.example/3\"}"));
  TEST_ASSERT_TRUE(box.command("fav.remove", "{\"id\":\"a2\"}") == Status::Ok);
  TEST_ASSERT_TRUE(box.command("fav.remove", "{\"id\":\"a2\"}") == Status::NotFound);
  r = box.result("favs", "{}");
  TEST_ASSERT_TRUE(has(r, "\"count\":2"));
  TEST_ASSERT_FALSE(has(r, "Two"));
  // An id that begins as another is another.
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"a10\",\"name\":\"Ten\",\"url\":\"http://a.example/10\"}") == Status::Ok);
  TEST_ASSERT_TRUE(box.command("fav.remove", "{\"id\":\"a1\"}") == Status::Ok);
  r = box.result("favs", "{}");
  TEST_ASSERT_TRUE(has(r, "Ten"));
  TEST_ASSERT_FALSE(has(r, "One"));
}

// As many as the board keeps, and not one more; the list comes a page at a time, each once.
void test_the_list_in_pages_and_its_limit() {
  Box box;
  Doc doc(box.sys.kernel(), kTarget);
  const int most = static_cast<int>(doc.v["max"].asInt());
  char args[400];
  for (int i = 0; i < most; ++i) {
    snprintf(args, sizeof(args),
             "{\"id\":\"n%d\",\"name\":\"Station number %d\","
             "\"url\":\"https://stream.example.org/a/long/address/of/a/station/that/goes/on/and/on/%d/stream.mp3\"}",
             i, i, i);
    TEST_ASSERT_TRUE(box.command("fav.set", args) == Status::Ok);
  }
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"more\",\"name\":\"One too many\",\"url\":\"http://a.example/x\"}") ==
                   Status::NoMem);
  // One that is there changes also when the list is full.
  TEST_ASSERT_TRUE(box.command("fav.set", "{\"id\":\"n0\",\"cat\":\"First\"}") == Status::Ok);
  int seen = 0, pages = 0;
  int64_t from = 0;
  while (from >= 0 && pages < 200) {
    snprintf(args, sizeof(args), "{\"from\":%d}", static_cast<int>(from));
    const std::string r = box.result("favs", args);
    const JsonValue v = JsonValue::parse(StringView(r.c_str(), r.size()));
    TEST_ASSERT_EQUAL_INT(most, v["count"].asInt());
    const JsonValue list = v["favs"];
    for (size_t i = 0; i < list.size(); ++i) {
      char id[24] = "";
      list.at(i)["i"].getString(id, sizeof(id));
      char want[24];
      snprintf(want, sizeof(want), "n%d", seen);
      TEST_ASSERT_EQUAL_STRING(want, id);
      ++seen;
    }
    from = v["next"].asInt(-1);
    ++pages;
  }
  TEST_ASSERT_EQUAL_INT(most, seen);
  TEST_ASSERT_TRUE(pages > 1);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_favourite_is_kept);
  RUN_TEST(test_a_new_favourite_wants_a_name_and_an_address);
  RUN_TEST(test_categories_and_removing);
  RUN_TEST(test_the_list_in_pages_and_its_limit);
  return UNITY_END();
}
