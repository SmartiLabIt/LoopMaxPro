// LoopMaxPro - uniform status codes. No exceptions are used anywhere.
#pragma once
#include <stdint.h>

namespace lmp {

enum class Status : uint8_t {
  Ok = 0,
  Pending,      // accepted, result will arrive later
  Busy,         // another operation is in progress
  Invalid,      // bad argument or malformed input
  NotFound,
  NoMem,        // fixed capacity exhausted
  Io,           // hardware or storage failure
  Timeout,
  Unsupported,  // capability missing on this board or build
  Denied,       // not allowed
  Error         // anything else
};

constexpr bool ok(Status s) { return s == Status::Ok; }

inline const char* statusName(Status s) {
  switch (s) {
    case Status::Ok:          return "ok";
    case Status::Pending:     return "pending";
    case Status::Busy:        return "busy";
    case Status::Invalid:     return "invalid";
    case Status::NotFound:    return "not_found";
    case Status::NoMem:       return "no_mem";
    case Status::Io:          return "io";
    case Status::Timeout:     return "timeout";
    case Status::Unsupported: return "unsupported";
    case Status::Denied:      return "denied";
    case Status::Error:       return "error";
  }
  return "error";
}

}  // namespace lmp

// Returns from the current function when the expression is not Status::Ok.
#define LMP_TRY(expr)                                   \
  do {                                                  \
    const ::lmp::Status lmp_try_status_ = (expr);       \
    if (lmp_try_status_ != ::lmp::Status::Ok) return lmp_try_status_; \
  } while (0)
