// LoopMaxPro - vector with fixed capacity, no heap.
#pragma once
#include <stddef.h>
#include <new>
#include <utility>

namespace lmp {

template <class T, size_t N>
class StaticVector {
 public:
  static constexpr size_t capacity = N;

  StaticVector() = default;
  StaticVector(const StaticVector& o) { copyFrom(o); }
  StaticVector& operator=(const StaticVector& o) {
    if (this != &o) {
      clear();
      copyFrom(o);
    }
    return *this;
  }
  ~StaticVector() { clear(); }

  bool push_back(const T& v) { return emplace_back(v) != nullptr; }

  template <class... Args>
  T* emplace_back(Args&&... args) {
    if (size_ >= N) return nullptr;
    T* slot = new (ptr(size_)) T(std::forward<Args>(args)...);
    ++size_;
    return slot;
  }

  void pop_back() {
    if (size_ == 0) return;
    --size_;
    ptr(size_)->~T();
  }

  // Removes the element at index, keeping the order of the others.
  bool erase(size_t index) {
    if (index >= size_) return false;
    for (size_t i = index; i + 1 < size_; ++i) {
      *ptr(i) = std::move(*ptr(i + 1));
    }
    pop_back();
    return true;
  }

  void clear() {
    while (size_) pop_back();
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool full() const { return size_ >= N; }

  T& operator[](size_t i) { return *ptr(i); }
  const T& operator[](size_t i) const { return *ptr(i); }
  T* begin() { return ptr(0); }
  T* end() { return ptr(size_); }
  const T* begin() const { return ptr(0); }
  const T* end() const { return ptr(size_); }
  T& back() { return *ptr(size_ - 1); }
  const T& back() const { return *ptr(size_ - 1); }

 private:
  T* ptr(size_t i) { return reinterpret_cast<T*>(storage_) + i; }
  const T* ptr(size_t i) const { return reinterpret_cast<const T*>(storage_) + i; }
  void copyFrom(const StaticVector& o) {
    for (size_t i = 0; i < o.size_; ++i) push_back(o[i]);
  }

  alignas(T) unsigned char storage_[N * sizeof(T)];
  size_t size_ = 0;
};

}  // namespace lmp
