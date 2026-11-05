// LoopMaxPro - non owning view over a contiguous sequence.
#pragma once
#include <stddef.h>

namespace lmp {

template <class T>
class Span {
 public:
  constexpr Span() = default;
  constexpr Span(T* ptr, size_t len) : ptr_(ptr), len_(len) {}
  template <size_t N>
  constexpr Span(T (&array)[N]) : ptr_(array), len_(N) {}

  constexpr T* data() const { return ptr_; }
  constexpr size_t size() const { return len_; }
  constexpr bool empty() const { return len_ == 0; }
  constexpr T* begin() const { return ptr_; }
  constexpr T* end() const { return ptr_ + len_; }
  constexpr T& operator[](size_t i) const { return ptr_[i]; }

  constexpr Span subspan(size_t offset, size_t count) const {
    return offset >= len_ ? Span()
                          : Span(ptr_ + offset, count < len_ - offset ? count : len_ - offset);
  }

 private:
  T* ptr_ = nullptr;
  size_t len_ = 0;
};

}  // namespace lmp
