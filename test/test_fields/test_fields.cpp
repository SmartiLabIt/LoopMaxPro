// Tests of the field tables: defaults, JSON output, JSON input with validation, schema.
#include <string.h>
#include <unity.h>

#include "kernel/fields/fields.h"

using namespace lmp;

struct DemoCfg {
  FixedString<16> name;
  FixedString<32> password;
  uint8_t channel;
  uint16_t delayMs;
  uint32_t counter;
  int8_t trim;
  int16_t offset;
  int32_t position;
  int64_t big;
  float gain;
  double precise;
  bool enabled;
  uint32_t serial;
};

LMP_FIELDS(DemoCfg,
  LMP_F(name,     "name",     Str(1, 16, "demo")),
  LMP_F(password, "password", Str(8, 32, "").secret()),
  LMP_F(channel,  "channel",  U8(0, 5, 1)),
  LMP_F(delayMs,  "delayMs",  U16(0, 60000, 5000)),
  LMP_F(counter,  "counter",  U32()),
  LMP_F(trim,     "trim",     I8(-10, 10, -2)),
  LMP_F(offset,   "offset",   I16()),
  LMP_F(position, "position", I32(-100000, 100000, 0)),
  LMP_F(big,      "big",      I64()),
  LMP_F(gain,     "gain",     F32(0, 10, 1.5)),
  LMP_F(precise,  "precise",  F64()),
  LMP_F(enabled,  "enabled",  Bool(true)),
  LMP_F(serial,   "serial",   U32(0, 4294967295.0, 77).readOnly()))

void setUp() {}
void tearDown() {}

namespace {

DemoCfg fresh() {
  DemoCfg c;
  // Dirty memory: defaults must overwrite everything.
  memset(static_cast<void*>(&c), 0xAA, sizeof(c));
  setDefaults(c);
  return c;
}

Status read(const char* json, DemoCfg& c, bool partial = true, FieldError* err = nullptr) {
  return readFields(JsonValue::parse(json), c, partial, err);
}

}  // namespace

void test_defaults() {
  const DemoCfg c = fresh();
  TEST_ASSERT_EQUAL_STRING("demo", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.password.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, c.channel);
  TEST_ASSERT_EQUAL_UINT16(5000, c.delayMs);
  TEST_ASSERT_EQUAL_UINT32(0, c.counter);
  TEST_ASSERT_EQUAL_INT8(-2, c.trim);
  TEST_ASSERT_EQUAL_INT16(0, c.offset);
  TEST_ASSERT_EQUAL_INT32(0, c.position);
  TEST_ASSERT_EQUAL_INT64(0, c.big);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, c.gain);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.precise);
  TEST_ASSERT_TRUE(c.enabled);
  TEST_ASSERT_EQUAL_UINT32(77, c.serial);
}

void test_table_shape() {
  const Span<const FieldDesc> fields = fieldsOf<DemoCfg>();
  TEST_ASSERT_EQUAL_UINT(13, fields.size());
  TEST_ASSERT_EQUAL_STRING("name", fields[0].key);
  TEST_ASSERT_EQUAL_UINT(17, fields[0].size);  // capacity plus terminator
  TEST_ASSERT_TRUE(fields[0].spec.kind == FieldKind::Str);
  TEST_ASSERT_TRUE(fields[2].spec.kind == FieldKind::U8);
  TEST_ASSERT_EQUAL_UINT(offsetof(DemoCfg, channel), fields[2].offset);
  TEST_ASSERT_TRUE(fields[1].spec.flags & kFieldSecret);
  TEST_ASSERT_TRUE(fields[12].spec.flags & kFieldReadOnly);
}

void test_write_hides_secrets() {
  DemoCfg c = fresh();
  c.password.assign("topsecret");
  c.big = -5000000000LL;
  c.precise = 0.125;

  FixedSink<512> out;
  JsonWriter w(out);
  w.beginObject();
  writeFields(w, c);
  w.endObject();
  TEST_ASSERT_TRUE(w.complete());
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"demo\",\"channel\":1,\"delayMs\":5000,\"counter\":0,\"trim\":-2,"
      "\"offset\":0,\"position\":0,\"big\":-5000000000,\"gain\":1.5,\"precise\":0.125,"
      "\"enabled\":true,\"serial\":77}",
      out.c_str());
  TEST_ASSERT_NULL(strstr(out.c_str(), "topsecret"));

  FixedSink<512> full;
  JsonWriter w2(full);
  w2.beginObject();
  writeFields(w2, c, Secrets::Show);
  w2.endObject();
  TEST_ASSERT_NOT_NULL(strstr(full.c_str(), "\"password\":\"topsecret\""));
}

void test_write_flags_secrets() {
  DemoCfg c = fresh();
  FixedSink<512> out;
  JsonWriter w(out);
  w.beginObject();
  writeFields(w, c, Secrets::Flag);
  w.endObject();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"passwordSet\":false"));

  c.password.assign("topsecret");
  out.clear();
  JsonWriter w2(out);
  w2.beginObject();
  writeFields(w2, c, Secrets::Flag);
  w2.endObject();
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"passwordSet\":true"));
  TEST_ASSERT_NULL(strstr(out.c_str(), "topsecret"));
}

void test_read_partial_keeps_other_values() {
  DemoCfg c = fresh();
  c.counter = 123;
  TEST_ASSERT_TRUE(read("{\"channel\":4,\"name\":\"office\",\"gain\":2.25}", c) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(4, c.channel);
  TEST_ASSERT_EQUAL_STRING("office", c.name.c_str());
  TEST_ASSERT_EQUAL_FLOAT(2.25f, c.gain);
  TEST_ASSERT_EQUAL_UINT32(123, c.counter);
  TEST_ASSERT_EQUAL_UINT16(5000, c.delayMs);
}

void test_read_full_resets_missing_values() {
  DemoCfg c = fresh();
  c.counter = 123;
  c.delayMs = 1;
  TEST_ASSERT_TRUE(read("{\"channel\":2}", c, false) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(2, c.channel);
  TEST_ASSERT_EQUAL_UINT32(0, c.counter);
  TEST_ASSERT_EQUAL_UINT16(5000, c.delayMs);
  TEST_ASSERT_EQUAL_STRING("demo", c.name.c_str());
}

void test_read_all_types() {
  DemoCfg c = fresh();
  const Status s = read(
      "{\"name\":\"n\",\"password\":\"12345678\",\"channel\":5,\"delayMs\":60000,"
      "\"counter\":4294967295,\"trim\":-10,\"offset\":-32768,\"position\":100000,"
      "\"big\":-9007199254740992,\"gain\":10,\"precise\":-1.5e-3,\"enabled\":false}",
      c);
  TEST_ASSERT_TRUE(s == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("12345678", c.password.c_str());
  TEST_ASSERT_EQUAL_UINT8(5, c.channel);
  TEST_ASSERT_EQUAL_UINT16(60000, c.delayMs);
  TEST_ASSERT_EQUAL_UINT32(4294967295u, c.counter);
  TEST_ASSERT_EQUAL_INT8(-10, c.trim);
  TEST_ASSERT_EQUAL_INT16(-32768, c.offset);
  TEST_ASSERT_EQUAL_INT32(100000, c.position);
  TEST_ASSERT_EQUAL_INT64(-9007199254740992LL, c.big);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, c.gain);
  TEST_ASSERT_EQUAL_DOUBLE(-0.0015, c.precise);
  TEST_ASSERT_FALSE(c.enabled);
}

void test_read_is_atomic() {
  DemoCfg c = fresh();
  FieldError err;
  // channel is valid, delayMs is out of range: nothing must change.
  const Status s = read("{\"channel\":3,\"delayMs\":70000}", c, true, &err);
  TEST_ASSERT_TRUE(s == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("delayMs", err.key);
  TEST_ASSERT_TRUE(err.status == Status::Invalid);
  TEST_ASSERT_EQUAL_UINT8(1, c.channel);
  TEST_ASSERT_EQUAL_UINT16(5000, c.delayMs);
}

void test_read_rejects_bad_values() {
  struct Case {
    const char* json;
    const char* key;
  };
  const Case cases[] = {
      {"{\"channel\":6}", "channel"},            // above max
      {"{\"channel\":-1}", "channel"},           // below min
      {"{\"channel\":1.5}", "channel"},          // not whole
      {"{\"channel\":\"2\"}", "channel"},        // wrong type
      {"{\"trim\":11}", "trim"},
      {"{\"gain\":10.5}", "gain"},
      {"{\"gain\":\"x\"}", "gain"},
      {"{\"name\":\"\"}", "name"},               // shorter than min
      {"{\"name\":\"12345678901234567\"}", "name"},  // longer than capacity
      {"{\"name\":12}", "name"},
      {"{\"password\":\"short\"}", "password"},
      {"{\"enabled\":\"yes\"}", "enabled"},
      {"{\"enabled\":2}", "enabled"},
      {"{\"counter\":4294967296}", "counter"},
      {"{\"offset\":40000}", "offset"},
  };
  for (const Case& k : cases) {
    DemoCfg c = fresh();
    FieldError err;
    const Status s = read(k.json, c, true, &err);
    TEST_ASSERT_TRUE_MESSAGE(s == Status::Invalid, k.json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.key, err.key, k.json);
  }
}

void test_read_bool_accepts_zero_and_one() {
  DemoCfg c = fresh();
  TEST_ASSERT_TRUE(read("{\"enabled\":0}", c) == Status::Ok);
  TEST_ASSERT_FALSE(c.enabled);
  TEST_ASSERT_TRUE(read("{\"enabled\":1}", c) == Status::Ok);
  TEST_ASSERT_TRUE(c.enabled);
}

void test_read_ignores_read_only_and_unknown() {
  DemoCfg c = fresh();
  TEST_ASSERT_TRUE(read("{\"serial\":5,\"unknown\":true,\"channel\":2}", c) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(77, c.serial);
  TEST_ASSERT_EQUAL_UINT8(2, c.channel);
}

void test_read_needs_an_object() {
  DemoCfg c = fresh();
  TEST_ASSERT_TRUE(read("[1,2]", c) == Status::Invalid);
  TEST_ASSERT_TRUE(read("not json", c) == Status::Invalid);
  TEST_ASSERT_TRUE(read("{}", c) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, c.channel);
}

void test_string_with_escapes() {
  DemoCfg c = fresh();
  TEST_ASSERT_TRUE(read("{\"name\":\"saut\\u00e9 \\\"1\\\"\"}", c) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("saut\xC3\xA9 \"1\"", c.name.c_str());
}

void test_roundtrip() {
  DemoCfg a = fresh();
  a.name.assign("Office Plugs");
  a.channel = 3;
  a.trim = -7;
  a.big = 1234567890123LL;
  a.gain = 0.75f;
  a.enabled = false;

  FixedSink<512> out;
  JsonWriter w(out);
  w.beginObject();
  writeFields(w, a);
  w.endObject();

  DemoCfg b = fresh();
  TEST_ASSERT_TRUE(readFields(JsonValue::parse(out.view()), b, false) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Office Plugs", b.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(3, b.channel);
  TEST_ASSERT_EQUAL_INT8(-7, b.trim);
  TEST_ASSERT_EQUAL_INT64(1234567890123LL, b.big);
  TEST_ASSERT_EQUAL_FLOAT(0.75f, b.gain);
  TEST_ASSERT_FALSE(b.enabled);
}

void test_schema() {
  FixedSink<1536> out;
  JsonWriter w(out);
  writeSchema<DemoCfg>(w);
  TEST_ASSERT_TRUE(w.complete());

  const JsonValue schema = JsonValue::parse(out.view());
  TEST_ASSERT_TRUE(schema.isArray());
  TEST_ASSERT_EQUAL_UINT(13, schema.size());

  const JsonValue name = schema.at(0);
  TEST_ASSERT_TRUE(name["key"].stringEquals("name"));
  TEST_ASSERT_TRUE(name["type"].stringEquals("str"));
  TEST_ASSERT_EQUAL_INT64(1, name["min"].asInt());
  TEST_ASSERT_EQUAL_INT64(16, name["max"].asInt());
  TEST_ASSERT_TRUE(name["def"].stringEquals("demo"));

  const JsonValue password = schema.at(1);
  TEST_ASSERT_TRUE(password["secret"].asBool());
  TEST_ASSERT_FALSE(password["def"].valid());

  const JsonValue channel = schema.at(2);
  TEST_ASSERT_TRUE(channel["type"].stringEquals("u8"));
  TEST_ASSERT_EQUAL_INT64(5, channel["max"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, channel["def"].asInt());

  const JsonValue trim = schema.at(5);
  TEST_ASSERT_EQUAL_INT64(-10, trim["min"].asInt());
  TEST_ASSERT_EQUAL_INT64(-2, trim["def"].asInt());

  const JsonValue gain = schema.at(9);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, gain["max"].asDouble());
  TEST_ASSERT_EQUAL_DOUBLE(1.5, gain["def"].asDouble());

  const JsonValue precise = schema.at(10);
  TEST_ASSERT_FALSE(precise["min"].valid());  // full range: not reported

  TEST_ASSERT_TRUE(schema.at(11)["def"].asBool());
  TEST_ASSERT_TRUE(schema.at(12)["readOnly"].asBool());
}

// A setting of a fuller version of the module: shown with its default, never taken.
struct LiteCfg {
  bool alerts;
  bool commands;
};

LMP_FIELDS(LiteCfg,
  LMP_F(alerts,   "alerts",   Bool(true)),
  LMP_F(commands, "commands", Bool(false).locked()))

void test_a_locked_field_is_shown_and_never_taken() {
  LiteCfg c;
  setDefaults(c);
  TEST_ASSERT_TRUE(readFields(JsonValue::parse("{\"alerts\":false,\"commands\":true}"), c, true) ==
                   Status::Ok);
  TEST_ASSERT_FALSE(c.alerts);
  TEST_ASSERT_FALSE(c.commands);
  // Not even a whole reading, as the one of the file, gives it a value.
  TEST_ASSERT_TRUE(readFields(JsonValue::parse("{\"commands\":true}"), c, false) == Status::Ok);
  TEST_ASSERT_TRUE(c.alerts);
  TEST_ASSERT_FALSE(c.commands);

  FixedSink<128> out;
  JsonWriter w(out);
  w.beginObject();
  writeFields(w, c);
  w.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"alerts\":true,\"commands\":false}", out.c_str());

  FixedSink<256> schemaText;
  JsonWriter s(schemaText);
  writeSchema<LiteCfg>(s);
  const JsonValue schema = JsonValue::parse(schemaText.view());
  TEST_ASSERT_FALSE(schema.at(0)["locked"].valid());
  TEST_ASSERT_TRUE(schema.at(1)["locked"].asBool());
  TEST_ASSERT_FALSE(schema.at(1)["def"].asBool());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_table_shape);
  RUN_TEST(test_write_hides_secrets);
  RUN_TEST(test_write_flags_secrets);
  RUN_TEST(test_read_partial_keeps_other_values);
  RUN_TEST(test_read_full_resets_missing_values);
  RUN_TEST(test_read_all_types);
  RUN_TEST(test_read_is_atomic);
  RUN_TEST(test_read_rejects_bad_values);
  RUN_TEST(test_read_bool_accepts_zero_and_one);
  RUN_TEST(test_read_ignores_read_only_and_unknown);
  RUN_TEST(test_read_needs_an_object);
  RUN_TEST(test_string_with_escapes);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_schema);
  RUN_TEST(test_a_locked_field_is_shown_and_never_taken);
  return UNITY_END();
}
