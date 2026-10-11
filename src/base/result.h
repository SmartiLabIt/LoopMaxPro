// LoopMaxPro - value or status.
#pragma once
#include "base/status.h"

namespace lmp {

template <class T>
class Result {
 public:
  Result(const T& value) : value_(value), status_(Status::Ok) {}
  Result(Status status) : value_(), status_(status) {}

  bool ok() const { return status_ == Status::Ok; }
  explicit operator bool() const { return ok(); }
  Status status() const { return status_; }

  const T& value() const { return value_; }
  T& value() { return value_; }
  T valueOr(const T& fallback) const { return ok() ? value_ : fallback; }

 private:
  T value_;
  Status status_;
};

}  // namespace lmp
