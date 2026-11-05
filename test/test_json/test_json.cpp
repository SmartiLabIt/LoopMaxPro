// Tests of the JSON writer and reader.
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "kernel/json/json_reader.h"
#include "kernel/json/json_writer.h"

using namespace lmp;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- writer

void test_writer_object_and_array() {
  FixedSink<256> out;
  JsonWriter w(out);
  w.beginObject();
  w.str("name", "relay");
  w.u32("pin", 22);
  w.boolean("on", true);
  w.null("none");
  w.beginArray("list").i64(1).i64(-2).str("x").endArray();
  w.beginObject("sub").f64("t", 21.5).endObject();
  w.endObject();

  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_TRUE(w.complete());
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"relay\",\"pin\":22,\"on\":true,\"none\":null,"
      "\"list\":[1,-2,\"x\"],\"sub\":{\"t\":21.5}}",
      out.c_str());
}

void test_writer_empty_containers() {
  FixedSink<64> out;
  JsonWriter w(out);
  w.beginObject().beginArray("a").endArray().beginObject("o").endObject().endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":[],\"o\":{}}", out.c_str());
  TEST_ASSERT_TRUE(w.complete());
}

void test_writer_escapes_strings() {
  FixedSink<128> out;
  JsonWriter w(out);
  w.beginObject().str("m", "a\"b\\c\nd\te\x01" "f").endObject();
  TEST_ASSERT_EQUAL_STRING("{\"m\":\"a\\\"b\\\\c\\nd\\te\\u0001f\"}", out.c_str());
}

void test_writer_keeps_utf8() {
  FixedSink<64> out;
  JsonWriter w(out);
  w.beginArray().str("saut\xC3\xA9 \xF0\x9F\x94\x8C").endArray();
  TEST_ASSERT_EQUAL_STRING("[\"saut\xC3\xA9 \xF0\x9F\x94\x8C\"]", out.c_str());
}

void test_writer_numbers() {
  FixedSink<200> out;
  JsonWriter w(out);
  w.beginArray();
  w.i64(INT64_MIN).i64(INT64_MAX).u64(UINT64_MAX).i64(0);
  w.f64(1.0).f64(-0.0).f64(3.14159, 3).f64(2.5, 0).f64(0.1 + 0.2, 6);
  w.endArray();
  TEST_ASSERT_EQUAL_STRING(
      "[-9223372036854775808,9223372036854775807,18446744073709551615,0,"
      "1,0,3.142,2,0.3]",
      out.c_str());
}

void test_writer_nan_becomes_null() {
  FixedSink<32> out;
  JsonWriter w(out);
  const double zero = 0.0;
  w.beginArray().f64(zero / zero).f64(1.0 / zero).endArray();
  TEST_ASSERT_EQUAL_STRING("[null,null]", out.c_str());
}

void test_writer_overflow_is_reported() {
  FixedSink<16> out;
  JsonWriter w(out);
  w.beginObject().str("key", "a value that does not fit").endObject();
  TEST_ASSERT_FALSE(w.ok());
  TEST_ASSERT_TRUE(out.overflow());
  TEST_ASSERT_EQUAL_UINT(15, out.size());
}

void test_writer_detects_misuse() {
  {
    FixedSink<32> out;
    JsonWriter w(out);
    w.key("k");  // key outside an object
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    FixedSink<32> out;
    JsonWriter w(out);
    w.beginObject().key("a").key("b");
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    FixedSink<32> out;
    JsonWriter w(out);
    w.endObject();
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    FixedSink<32> out;
    JsonWriter w(out);
    w.beginObject();
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_FALSE(w.complete());
  }
}

void test_writer_raw() {
  FixedSink<64> out;
  JsonWriter w(out);
  w.beginObject().raw("cfg", "{\"a\":1}").raw("none", "").endObject();
  TEST_ASSERT_EQUAL_STRING("{\"cfg\":{\"a\":1},\"none\":null}", out.c_str());
}

// ---------------------------------------------------------------- reader

void test_reader_scalars() {
  TEST_ASSERT_TRUE(JsonValue::parse("true").asBool());
  TEST_ASSERT_FALSE(JsonValue::parse("false").asBool(true));
  TEST_ASSERT_TRUE(JsonValue::parse("null").isNull());
  TEST_ASSERT_EQUAL_INT64(42, JsonValue::parse(" 42 ").asInt());
  TEST_ASSERT_EQUAL_INT64(-7, JsonValue::parse("-7").asInt());
  TEST_ASSERT_EQUAL_DOUBLE(2.5, JsonValue::parse("2.5").asDouble());
  TEST_ASSERT_EQUAL_DOUBLE(1500.0, JsonValue::parse("1.5e3").asDouble());
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, JsonValue::parse("-9223372036854775808").asInt());
}

void test_reader_integer_rules() {
  int64_t n = 0;
  TEST_ASSERT_TRUE(JsonValue::parse("3.0").getInt(n));
  TEST_ASSERT_EQUAL_INT64(3, n);
  TEST_ASSERT_TRUE(JsonValue::parse("2e2").getInt(n));
  TEST_ASSERT_EQUAL_INT64(200, n);
  TEST_ASSERT_FALSE(JsonValue::parse("3.5").getInt(n));
  TEST_ASSERT_FALSE(JsonValue::parse("99999999999999999999").getInt(n));
  TEST_ASSERT_FALSE(JsonValue::parse("\"12\"").getInt(n));
  TEST_ASSERT_EQUAL_INT64(-1, JsonValue::parse("true").asInt(-1));
}

void test_reader_rejects_malformed() {
  const char* bad[] = {
      "",          "   ",        "{",          "}",         "[1,2",      "[1,]",
      "{\"a\":}",  "{\"a\" 1}",  "{a:1}",      "{\"a\":1,}", "01",       "1.",
      ".5",        "+1",         "1e",         "tru",       "nul",       "\"abc",
      "\"a\\x\"",  "\"a\\u12\"", "\"tab\there\"", "1 2",     "[1] x",     "{\"a\":1}}",
      "'a'",       "[,1]",
  };
  for (const char* text : bad) {
    TEST_ASSERT_FALSE_MESSAGE(JsonValue::parse(text).valid(), text);
  }
  TEST_ASSERT_FALSE(JsonValue::parse(StringView()).valid());
}

void test_reader_accepts_valid() {
  const char* good[] = {
      "{}", "[]", "[[]]", "{\"a\":{}}", " { \"a\" : [ 1 , 2 ] } ", "\"\"", "0", "-0", "1E+2",
      "[true,false,null]", "\"\\u00e8\\n\\/\"", "{\"a\":1,\"a\":2}",
  };
  for (const char* text : good) {
    TEST_ASSERT_TRUE_MESSAGE(JsonValue::parse(text).valid(), text);
  }
}

void test_reader_depth_limit() {
  char deep[80];
  memset(deep, '[', 30);
  memset(deep + 30, ']', 30);
  deep[60] = '\0';
  TEST_ASSERT_FALSE(JsonValue::parse(deep).valid());

  char shallow[32];
  memset(shallow, '[', 10);
  memset(shallow + 10, ']', 10);
  shallow[20] = '\0';
  TEST_ASSERT_TRUE(JsonValue::parse(shallow).valid());
}

void test_reader_object_lookup() {
  const JsonValue doc = JsonValue::parse(
      "{\"name\":\"zappy\",\"pins\":[22,23],\"net\":{\"ip\":\"192.168.1.5\",\"rssi\":-58},"
      "\"on\":true}");
  TEST_ASSERT_TRUE(doc.isObject());
  TEST_ASSERT_EQUAL_UINT(4, doc.size());

  char buf[32];
  TEST_ASSERT_TRUE(doc["name"].getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("zappy", buf);
  TEST_ASSERT_TRUE(doc["net"]["ip"].getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("192.168.1.5", buf);
  TEST_ASSERT_EQUAL_INT64(-58, doc["net"]["rssi"].asInt());
  TEST_ASSERT_TRUE(doc["on"].asBool());

  TEST_ASSERT_FALSE(doc["missing"].valid());
  TEST_ASSERT_FALSE(doc["missing"]["deeper"].valid());
  TEST_ASSERT_FALSE(doc["name"]["x"].valid());
  TEST_ASSERT_EQUAL_INT64(9, doc["missing"].asInt(9));
}

void test_reader_array_access() {
  const JsonValue doc = JsonValue::parse("[10, \"b\", [1,2,3], {\"k\":7}]");
  TEST_ASSERT_TRUE(doc.isArray());
  TEST_ASSERT_EQUAL_UINT(4, doc.size());
  TEST_ASSERT_EQUAL_INT64(10, doc.at(0).asInt());
  TEST_ASSERT_TRUE(doc.at(1).stringEquals("b"));
  TEST_ASSERT_EQUAL_UINT(3, doc.at(2).size());
  TEST_ASSERT_EQUAL_INT64(3, doc.at(2).at(2).asInt());
  TEST_ASSERT_EQUAL_INT64(7, doc.at(3)["k"].asInt());
  TEST_ASSERT_FALSE(doc.at(4).valid());
  TEST_ASSERT_EQUAL_UINT(0, JsonValue::parse("[]").size());
}

void test_reader_iteration() {
  const JsonValue obj = JsonValue::parse("{\"a\":1,\"b\":2,\"c\":3}");
  int64_t sum = 0;
  char keys[8] = {0};
  size_t n = 0;
  for (const JsonMember m : obj) {
    char k[4];
    TEST_ASSERT_TRUE(m.key.getString(k, sizeof(k)));
    keys[n++] = k[0];
    sum += m.value.asInt();
  }
  TEST_ASSERT_EQUAL_INT64(6, sum);
  TEST_ASSERT_EQUAL_STRING("abc", keys);

  int64_t total = 0;
  for (const JsonMember m : JsonValue::parse("[5,6,7]")) {
    TEST_ASSERT_FALSE(m.key.valid());
    total += m.value.asInt();
  }
  TEST_ASSERT_EQUAL_INT64(18, total);

  int loops = 0;
  for (const JsonMember m : JsonValue::parse("42")) {
    (void)m;
    ++loops;
  }
  TEST_ASSERT_EQUAL_INT(0, loops);
}

void test_reader_string_decoding() {
  char buf[64];
  TEST_ASSERT_TRUE(JsonValue::parse("\"a\\\"b\\\\c\\nd\\t\\/\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("a\"b\\c\nd\t/", buf);

  TEST_ASSERT_TRUE(JsonValue::parse("\"saut\\u00e9\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("saut\xC3\xA9", buf);

  TEST_ASSERT_TRUE(JsonValue::parse("\"\\u20ac\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC", buf);

  // Surrogate pair: U+1F50C
  TEST_ASSERT_TRUE(JsonValue::parse("\"\\ud83d\\udd0c\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x94\x8C", buf);

  const JsonValue v = JsonValue::parse("\"saut\\u00e9\"");
  TEST_ASSERT_EQUAL_UINT(6, v.stringLength());
  TEST_ASSERT_TRUE(v.stringEquals("saut\xC3\xA9"));
  TEST_ASSERT_FALSE(v.stringEquals("saute"));
}

void test_reader_string_buffer_too_small() {
  char buf[4];
  TEST_ASSERT_FALSE(JsonValue::parse("\"abcdef\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);
  TEST_ASSERT_TRUE(JsonValue::parse("\"abc\"").getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("abc", buf);

  FixedString<5> fixed;
  TEST_ASSERT_TRUE(JsonValue::parse("\"hello\"").getString(fixed));
  TEST_ASSERT_EQUAL_STRING("hello", fixed.c_str());
  TEST_ASSERT_FALSE(JsonValue::parse("\"hello!\"").getString(fixed));
}

void test_reader_escaped_key() {
  const JsonValue doc = JsonValue::parse("{\"a\\\"b\":1,\"caf\\u00e9\":2}");
  TEST_ASSERT_EQUAL_INT64(1, doc["a\"b"].asInt());
  TEST_ASSERT_EQUAL_INT64(2, doc["caf\xC3\xA9"].asInt());
}

void test_reader_not_nul_terminated() {
  const char text[] = "{\"a\":12}garbage";
  const JsonValue doc = JsonValue::parse(StringView(text, 8));
  TEST_ASSERT_TRUE(doc.valid());
  TEST_ASSERT_EQUAL_INT64(12, doc["a"].asInt());
}

// ---------------------------------------------------------------- both

void test_roundtrip() {
  FixedSink<256> out;
  JsonWriter w(out);
  w.beginObject();
  w.str("text", "line1\nline2 \"quoted\" \xF0\x9F\x94\x8C");
  w.i64("neg", -123456789012345LL);
  w.f64("real", -12.375, 3);
  w.beginArray("flags").boolean(true).boolean(false).endArray();
  w.endObject();
  TEST_ASSERT_TRUE(w.complete());

  const JsonValue doc = JsonValue::parse(out.view());
  TEST_ASSERT_TRUE(doc.valid());
  char buf[64];
  TEST_ASSERT_TRUE(doc["text"].getString(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("line1\nline2 \"quoted\" \xF0\x9F\x94\x8C", buf);
  TEST_ASSERT_EQUAL_INT64(-123456789012345LL, doc["neg"].asInt());
  TEST_ASSERT_EQUAL_DOUBLE(-12.375, doc["real"].asDouble());
  TEST_ASSERT_TRUE(doc["flags"].at(0).asBool());
  TEST_ASSERT_FALSE(doc["flags"].at(1).asBool(true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_writer_object_and_array);
  RUN_TEST(test_writer_empty_containers);
  RUN_TEST(test_writer_escapes_strings);
  RUN_TEST(test_writer_keeps_utf8);
  RUN_TEST(test_writer_numbers);
  RUN_TEST(test_writer_nan_becomes_null);
  RUN_TEST(test_writer_overflow_is_reported);
  RUN_TEST(test_writer_detects_misuse);
  RUN_TEST(test_writer_raw);
  RUN_TEST(test_reader_scalars);
  RUN_TEST(test_reader_integer_rules);
  RUN_TEST(test_reader_rejects_malformed);
  RUN_TEST(test_reader_accepts_valid);
  RUN_TEST(test_reader_depth_limit);
  RUN_TEST(test_reader_object_lookup);
  RUN_TEST(test_reader_array_access);
  RUN_TEST(test_reader_iteration);
  RUN_TEST(test_reader_string_decoding);
  RUN_TEST(test_reader_string_buffer_too_small);
  RUN_TEST(test_reader_escaped_key);
  RUN_TEST(test_reader_not_nul_terminated);
  RUN_TEST(test_roundtrip);
  return UNITY_END();
}
