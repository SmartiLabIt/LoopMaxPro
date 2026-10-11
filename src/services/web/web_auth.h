// LoopMaxPro - access to the local console: sessions and limit to the wrong passwords.
// Plain logic over fixed tables: no network, no storage. Sessions live in memory and end with
// a restart.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "base/string_view.h"
#include "hal/api/icrypto.h"
#include "hal/api/types.h"
#include "lmp_config.h"

namespace lmp {

// Compares two texts in a time that does not depend on where they differ.
bool sameSecret(StringView a, StringView b);

class WebAuth {
 public:
  static constexpr size_t kTokenLength = 32;
  // Longest wait after repeated failures, in seconds.
  static constexpr uint32_t kLongestLockSec = 900;

  explicit WebAuth(hal::IRandom* rng = nullptr) : rng_(rng) {}
  void setRandom(hal::IRandom* rng) { rng_ = rng; }

  // Ok: token receives the new session, kTokenLength characters and the terminator.
  // Denied: wrong password. Busy: too many failures from this address, waitSec tells how long.
  // Unsupported: no source of random numbers, so no token can be trusted.
  Status login(StringView password, StringView expected, hal::Ip4 from, uint64_t nowMs,
               uint32_t ttlSec, char* token, uint32_t& waitSec);

  // True for the token of a session still valid.
  bool check(StringView token, uint64_t nowMs);
  // Seconds left to the session, 0 when the token is not valid.
  uint32_t leftSec(StringView token, uint64_t nowMs) const;
  bool logout(StringView token);
  void logoutAll();
  size_t sessions(uint64_t nowMs) const;

  uint32_t failures() const { return failures_; }

 private:
  struct Session {
    char token[kTokenLength + 1] = {0};
    uint64_t expiresMs = 0;
    uint64_t seenMs = 0;
    bool used = false;
  };
  // Who is sending wrong passwords.
  struct Guard {
    hal::Ip4 ip;
    uint8_t failures = 0;
    uint8_t locks = 0;        // how many times it had to wait
    uint64_t lockedUntilMs = 0;
    uint64_t lastMs = 0;
    bool used = false;
  };

  const Session* find(StringView token, uint64_t nowMs) const;
  Guard& guardOf(hal::Ip4 ip, uint64_t nowMs);

  hal::IRandom* rng_;
  Session sessions_[LMP_MAX_SESSIONS];
  Guard guards_[LMP_MAX_SESSIONS];
  uint32_t failures_ = 0;
};

}  // namespace lmp
