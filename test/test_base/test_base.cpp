// Tests of the base library: containers, views, delegate.
#include <unity.h>

#include "base/delegate.h"
#include "base/fixed_string.h"
#include "base/result.h"
#include "base/ring_buffer.h"
#include "base/span.h"
#include "base/static_vector.h"
#include "base/status.h"
#include "base/string_view.h"
#include "base/version.h"

using namespace lmp;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- StringView

void test_string_view_basics() {
  StringView empty;
  TEST_ASSERT_TRUE(empty.empty());
  TEST_ASSERT_TRUE(empty == StringView(""));

  StringView s("hello world");
  TEST_ASSERT_EQUAL_UINT(11, s.size);
  TEST_ASSERT_TRUE(s.startsWith("hello"));
  TEST_ASSERT_FALSE(s.startsWith("world"));
  TEST_ASSERT_TRUE(s.endsWith("world"));
  TEST_ASSERT_TRUE(s.substr(6) == StringView("world"));
  TEST_ASSERT_TRUE(s.substr(0, 5) == StringView("hello"));
  TEST_ASSERT_TRUE(s.substr(50).empty());
  TEST_ASSERT_EQUAL_UINT(5, s.find(' '));
  TEST_ASSERT_EQUAL_UINT(StringView::npos, s.find('z'));
}

void test_string_view_copy_truncates() {
  char buf[6];
  const size_t n = StringView("hello world").copyTo(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(5, n);
  TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_string_view_null_pointer() {
  StringView s(static_cast<const char*>(nullptr));
  TEST_ASSERT_TRUE(s.empty());
}

// ---------------------------------------------------------------- FixedString

void test_fixed_string_assign() {
  FixedString<8> s;
  TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_TRUE(s.assign("abc"));
  TEST_ASSERT_EQUAL_STRING("abc", s.c_str());
  TEST_ASSERT_EQUAL_UINT(3, s.size());
  TEST_ASSERT_TRUE(s == "abc");
  TEST_ASSERT_TRUE(s != "abd");
}

void test_fixed_string_truncation_is_reported() {
  FixedString<4> s;
  TEST_ASSERT_FALSE(s.assign("abcdef"));
  TEST_ASSERT_EQUAL_STRING("abcd", s.c_str());
  TEST_ASSERT_TRUE(s.assign("abcd"));  // exactly the capacity
}

void test_fixed_string_append_and_format() {
  FixedString<10> s("ab");
  TEST_ASSERT_TRUE(s.append("cd"));
  TEST_ASSERT_TRUE(s.append('e'));
  TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
  TEST_ASSERT_FALSE(s.append("1234567"));
  TEST_ASSERT_EQUAL_STRING("abcde12345", s.c_str());

  TEST_ASSERT_TRUE(s.format("%d-%s", 42, "x"));
  TEST_ASSERT_EQUAL_STRING("42-x", s.c_str());
  TEST_ASSERT_FALSE(s.format("%s", "this text is too long"));
  TEST_ASSERT_EQUAL_UINT(10, s.size());
}

// ---------------------------------------------------------------- StaticVector

namespace {
int g_alive = 0;
struct Counted {
  int value;
  explicit Counted(int v) : value(v) { ++g_alive; }
  Counted(const Counted& o) : value(o.value) { ++g_alive; }
  Counted& operator=(const Counted&) = default;
  ~Counted() { --g_alive; }
};
}  // namespace

void test_static_vector_capacity() {
  StaticVector<int, 3> v;
  TEST_ASSERT_TRUE(v.empty());
  TEST_ASSERT_TRUE(v.push_back(1));
  TEST_ASSERT_TRUE(v.push_back(2));
  TEST_ASSERT_TRUE(v.push_back(3));
  TEST_ASSERT_TRUE(v.full());
  TEST_ASSERT_FALSE(v.push_back(4));
  TEST_ASSERT_EQUAL_UINT(3, v.size());
  TEST_ASSERT_EQUAL_INT(3, v.back());
}

void test_static_vector_erase_keeps_order() {
  StaticVector<int, 4> v;
  for (int i = 1; i <= 4; ++i) v.push_back(i * 10);
  TEST_ASSERT_TRUE(v.erase(1));
  TEST_ASSERT_EQUAL_UINT(3, v.size());
  TEST_ASSERT_EQUAL_INT(10, v[0]);
  TEST_ASSERT_EQUAL_INT(30, v[1]);
  TEST_ASSERT_EQUAL_INT(40, v[2]);
  TEST_ASSERT_FALSE(v.erase(9));

  int sum = 0;
  for (int x : v) sum += x;
  TEST_ASSERT_EQUAL_INT(80, sum);
}

void test_static_vector_destroys_elements() {
  g_alive = 0;
  {
    StaticVector<Counted, 4> v;
    v.emplace_back(1);
    v.emplace_back(2);
    v.emplace_back(3);
    TEST_ASSERT_EQUAL_INT(3, g_alive);
    v.pop_back();
    TEST_ASSERT_EQUAL_INT(2, g_alive);

    StaticVector<Counted, 4> copy(v);
    TEST_ASSERT_EQUAL_INT(4, g_alive);
    TEST_ASSERT_EQUAL_INT(2, copy[1].value);
  }
  TEST_ASSERT_EQUAL_INT(0, g_alive);
}

// ---------------------------------------------------------------- RingBuffer

void test_ring_buffer_overwrites_oldest() {
  RingBuffer<int, 3> r;
  TEST_ASSERT_TRUE(r.empty());
  r.push(1);
  r.push(2);
  r.push(3);
  TEST_ASSERT_TRUE(r.full());
  TEST_ASSERT_EQUAL_UINT(0, r.dropped());

  r.push(4);
  r.push(5);
  TEST_ASSERT_EQUAL_UINT(3, r.size());
  TEST_ASSERT_EQUAL_UINT(2, r.dropped());
  TEST_ASSERT_EQUAL_INT(3, r.at(0));
  TEST_ASSERT_EQUAL_INT(4, r.at(1));
  TEST_ASSERT_EQUAL_INT(5, r.at(2));
  TEST_ASSERT_EQUAL_INT(5, r.newest());

  r.clear();
  TEST_ASSERT_TRUE(r.empty());
  TEST_ASSERT_EQUAL_UINT(0, r.dropped());
  r.push(9);
  TEST_ASSERT_EQUAL_INT(9, r.at(0));
}

// ---------------------------------------------------------------- Delegate

namespace {
int twice(int x) { return x * 2; }
}  // namespace

void test_delegate_function_pointer() {
  Delegate<int(int)> d = twice;
  TEST_ASSERT_TRUE(static_cast<bool>(d));
  TEST_ASSERT_EQUAL_INT(14, d(7));
}

void test_delegate_lambda_with_capture() {
  int total = 0;
  int step = 5;
  Delegate<void(int)> add = [&total, step](int n) { total += n * step; };
  add(2);
  add(3);
  TEST_ASSERT_EQUAL_INT(25, total);

  Delegate<void(int)> copy = add;
  copy(1);
  TEST_ASSERT_EQUAL_INT(30, total);
}

void test_delegate_empty_and_reset() {
  Delegate<void()> d;
  TEST_ASSERT_FALSE(static_cast<bool>(d));
  int hits = 0;
  d = [&hits]() { ++hits; };
  TEST_ASSERT_TRUE(static_cast<bool>(d));
  d();
  d.reset();
  TEST_ASSERT_FALSE(static_cast<bool>(d));
  TEST_ASSERT_EQUAL_INT(1, hits);

  Delegate<void()> none = nullptr;
  TEST_ASSERT_FALSE(static_cast<bool>(none));
}

void test_delegate_size_is_fixed() {
  // Two pointers of storage plus the call pointer: nothing on the heap.
  TEST_ASSERT_EQUAL_UINT(3 * sizeof(void*), sizeof(Delegate<void()>));
}

// ---------------------------------------------------------------- Span, Result, Status

void test_span() {
  int data[] = {1, 2, 3, 4, 5};
  Span<int> s(data);
  TEST_ASSERT_EQUAL_UINT(5, s.size());
  TEST_ASSERT_EQUAL_INT(3, s[2]);
  Span<int> sub = s.subspan(3, 10);
  TEST_ASSERT_EQUAL_UINT(2, sub.size());
  TEST_ASSERT_EQUAL_INT(4, sub[0]);
  TEST_ASSERT_TRUE(s.subspan(5, 1).empty());
  TEST_ASSERT_TRUE(Span<int>().empty());
}

namespace {
Status failing() { return Status::Timeout; }
Status chain(bool fail, int& reached) {
  if (fail) LMP_TRY(failing());
  reached = 1;
  return Status::Ok;
}
}  // namespace

void test_result_and_status() {
  Result<int> good(12);
  TEST_ASSERT_TRUE(good.ok());
  TEST_ASSERT_EQUAL_INT(12, good.value());

  Result<int> bad(Status::NotFound);
  TEST_ASSERT_FALSE(bad.ok());
  TEST_ASSERT_TRUE(bad.status() == Status::NotFound);
  TEST_ASSERT_EQUAL_INT(7, bad.valueOr(7));

  int reached = 0;
  TEST_ASSERT_TRUE(chain(true, reached) == Status::Timeout);
  TEST_ASSERT_EQUAL_INT(0, reached);
  TEST_ASSERT_TRUE(chain(false, reached) == Status::Ok);
  TEST_ASSERT_EQUAL_INT(1, reached);

  TEST_ASSERT_EQUAL_STRING("timeout", statusName(Status::Timeout));
}

// The versions of LoopMaxPro and of the modules, compared the same way on the device and on
// the server (PHP version_compare): an update is offered when either is newer.
void test_versions_are_compared() {
  TEST_ASSERT_EQUAL_INT(0, versionCompare("1.0.0", "1.0.0"));
  TEST_ASSERT_TRUE(versionNewer("1.0.1", "1.0.0"));
  TEST_ASSERT_FALSE(versionNewer("1.0.0", "1.0.1"));
  TEST_ASSERT_FALSE(versionNewer("1.0.0", "1.0.0"));
  TEST_ASSERT_TRUE(versionNewer("1.1.0", "1.0.9"));
  TEST_ASSERT_TRUE(versionNewer("2.0.0", "1.9.9"));
  // Numbers, not text: 10 comes after 9.
  TEST_ASSERT_TRUE(versionNewer("1.0.10", "1.0.9"));
  TEST_ASSERT_TRUE(versionNewer("1.10.0", "1.9.0"));
  // A version of development comes before the release with the same numbers.
  TEST_ASSERT_TRUE(versionNewer("1.0.0", "1.0.0-dev"));
  TEST_ASSERT_TRUE(versionNewer("2.0.1-dev", "2.0.0-dev"));
  TEST_ASSERT_TRUE(versionNewer("1.0.1-dev", "1.0.0"));
  TEST_ASSERT_FALSE(versionNewer("1.0.0-dev", "1.0.0"));
  // Fewer numbers: what is missing is zero.
  TEST_ASSERT_EQUAL_INT(0, versionCompare("1.0", "1.0.0"));
  TEST_ASSERT_TRUE(versionNewer("1.0.1", "1.0"));
  // Nothing, or not a version: the oldest of all.
  TEST_ASSERT_TRUE(versionNewer("1.0.0", ""));
  TEST_ASSERT_TRUE(versionNewer("0.0.1", nullptr));
  TEST_ASSERT_FALSE(versionNewer("", "1.0.0"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_versions_are_compared);
  RUN_TEST(test_string_view_basics);
  RUN_TEST(test_string_view_copy_truncates);
  RUN_TEST(test_string_view_null_pointer);
  RUN_TEST(test_fixed_string_assign);
  RUN_TEST(test_fixed_string_truncation_is_reported);
  RUN_TEST(test_fixed_string_append_and_format);
  RUN_TEST(test_static_vector_capacity);
  RUN_TEST(test_static_vector_erase_keeps_order);
  RUN_TEST(test_static_vector_destroys_elements);
  RUN_TEST(test_ring_buffer_overwrites_oldest);
  RUN_TEST(test_delegate_function_pointer);
  RUN_TEST(test_delegate_lambda_with_capture);
  RUN_TEST(test_delegate_empty_and_reset);
  RUN_TEST(test_delegate_size_is_fixed);
  RUN_TEST(test_span);
  RUN_TEST(test_result_and_status);
  return UNITY_END();
}
