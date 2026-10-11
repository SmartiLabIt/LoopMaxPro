// LoopMaxPro - circular buffer with fixed capacity. When full, the oldest item is overwritten.
#pragma once
#include <stddef.h>

namespace lmp {

template <class T, size_t N>
class RingBuffer {
 public:
  static constexpr size_t capacity = N;

  // Returns the slot to fill. When the buffer is full the oldest item is recycled.
  T& pushSlot() {
    const size_t index = (head_ + size_) % N;
    if (size_ < N) {
      ++size_;
    } else {
      head_ = (head_ + 1) % N;
      ++dropped_;
    }
    return items_[index];
  }

  void push(const T& value) { pushSlot() = value; }

  // Index 0 is the oldest item.
  T& at(size_t i) { return items_[(head_ + i) % N]; }
  const T& at(size_t i) const { return items_[(head_ + i) % N]; }
  T& newest() { return at(size_ - 1); }
  const T& newest() const { return at(size_ - 1); }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool full() const { return size_ == N; }
  // Number of items overwritten since the last clear.
  size_t dropped() const { return dropped_; }

  void clear() {
    head_ = 0;
    size_ = 0;
    dropped_ = 0;
  }

 private:
  T items_[N];
  size_t head_ = 0;
  size_t size_ = 0;
  size_t dropped_ = 0;
};

// The same ring on storage given at run time: memory taken once at start, which can be the
// external RAM. Without storage it holds nothing and forgets every push.
template <class T>
class RingSpan {
 public:
  void attach(T* items, size_t capacity) {
    items_ = items;
    capacity_ = items ? capacity : 0;
    clear();
  }
  size_t capacity() const { return capacity_; }

  void push(const T& value) {
    if (!capacity_) {
      ++dropped_;
      return;
    }
    const size_t index = (head_ + size_) % capacity_;
    if (size_ < capacity_) {
      ++size_;
    } else {
      head_ = (head_ + 1) % capacity_;
      ++dropped_;
    }
    items_[index] = value;
  }

  // Index 0 is the oldest item.
  const T& at(size_t i) const { return items_[(head_ + i) % capacity_]; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  size_t dropped() const { return dropped_; }

  void clear() {
    head_ = 0;
    size_ = 0;
    dropped_ = 0;
  }

 private:
  T* items_ = nullptr;
  size_t capacity_ = 0;
  size_t head_ = 0;
  size_t size_ = 0;
  size_t dropped_ = 0;
};

}  // namespace lmp
