// LoopMaxPro - string with fixed capacity, always NUL terminated, no heap.
// Layout is a plain char array: the field tables rely on it.
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "base/string_view.h"

namespace lmp {

template <size_t N>
class FixedString {
 public:
  static constexpr size_t capacity = N;

  FixedString() { buf_[0] = '\0'; }
  FixedString(const char* s) { assign(s); }
  FixedString(StringView s) { assign(s); }

  // All mutators return false when the text did not fit and was truncated.
  bool assign(const char* s) { return assign(StringView(s)); }
  bool assign(StringView s) {
    const size_t n = s.size < N ? s.size : N;
    if (n) memcpy(buf_, s.data, n);
    buf_[n] = '\0';
    return s.size <= N;
  }

  bool append(StringView s) {
    const size_t len = size();
    const size_t room = N - len;
    const size_t n = s.size < room ? s.size : room;
    if (n) memcpy(buf_ + len, s.data, n);
    buf_[len + n] = '\0';
    return s.size <= room;
  }
  bool append(const char* s) { return append(StringView(s)); }
  bool append(char c) { return append(StringView(&c, 1)); }

  bool format(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf_, N + 1, fmt, ap);
    va_end(ap);
    return n >= 0 && static_cast<size_t>(n) <= N;
  }

  void clear() { buf_[0] = '\0'; }
  bool empty() const { return buf_[0] == '\0'; }
  size_t size() const { return strlen(buf_); }
  const char* c_str() const { return buf_; }
  char* data() { return buf_; }
  StringView view() const { return StringView(buf_, size()); }
  operator StringView() const { return view(); }

  bool operator==(const char* s) const { return s && strcmp(buf_, s) == 0; }
  bool operator!=(const char* s) const { return !(*this == s); }
  template <size_t M>
  bool operator==(const FixedString<M>& o) const { return strcmp(buf_, o.c_str()) == 0; }

 private:
  char buf_[N + 1];
};

static_assert(sizeof(FixedString<7>) == 8, "FixedString must be a plain char array");

}  // namespace lmp
