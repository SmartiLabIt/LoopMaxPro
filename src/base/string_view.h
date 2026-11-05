// LoopMaxPro - non owning view over characters. Not necessarily NUL terminated.
#pragma once
#include <stddef.h>
#include <string.h>

namespace lmp {

struct StringView {
  static constexpr size_t npos = static_cast<size_t>(-1);

  const char* data = nullptr;
  size_t size = 0;

  constexpr StringView() = default;
  constexpr StringView(const char* d, size_t n) : data(d), size(n) {}
  StringView(const char* cstr) : data(cstr), size(cstr ? strlen(cstr) : 0) {}

  constexpr bool empty() const { return size == 0; }
  constexpr const char* begin() const { return data; }
  constexpr const char* end() const { return data + size; }
  constexpr char operator[](size_t i) const { return data[i]; }

  bool equals(StringView o) const {
    return size == o.size && (size == 0 || memcmp(data, o.data, size) == 0);
  }
  bool startsWith(StringView p) const {
    return size >= p.size && (p.size == 0 || memcmp(data, p.data, p.size) == 0);
  }
  bool endsWith(StringView s) const {
    return size >= s.size && (s.size == 0 || memcmp(data + size - s.size, s.data, s.size) == 0);
  }

  StringView substr(size_t pos, size_t n = npos) const {
    if (pos >= size) return StringView();
    const size_t left = size - pos;
    return StringView(data + pos, n < left ? n : left);
  }

  size_t find(char c, size_t from = 0) const {
    for (size_t i = from; i < size; ++i) {
      if (data[i] == c) return i;
    }
    return npos;
  }

  // Copies into a NUL terminated buffer. Returns the number of characters copied.
  size_t copyTo(char* out, size_t cap) const {
    if (cap == 0) return 0;
    const size_t n = size < cap - 1 ? size : cap - 1;
    if (n) memcpy(out, data, n);
    out[n] = '\0';
    return n;
  }
};

inline bool operator==(StringView a, StringView b) { return a.equals(b); }
inline bool operator!=(StringView a, StringView b) { return !a.equals(b); }

}  // namespace lmp
