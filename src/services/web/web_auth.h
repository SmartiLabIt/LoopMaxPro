// LoopMaxPro - access to the local console: sessions and limit to the wrong passwords.
// Plain logic over fixed tables: no network, no storage. Sessions live in memory and end with
// a restart.
//
// A session opens with the password, as the owner, or with a pass: a credential issued by
// someone the device trusts, checked by who attached to the web service (web_access.h). A pass
// says the role of who uses it; the question it answers (the nonce) comes from here.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "base/string_view.h"
#include "hal/api/icrypto.h"
#include "hal/api/types.h"
#include "kernel/command/command.h"
#include "lmp_config.h"

namespace lmp {

// Compares two texts in a time that does not depend on where they differ.
bool sameSecret(StringView a, StringView b);

class WebAuth {
 public:
  static constexpr size_t kTokenLength = 32;
  // 16 random bytes, in base64url.
  static constexpr size_t kNonceLength = 22;
  static constexpr uint32_t kNonceTtlSec = 60;
  static constexpr size_t kUserMax = 32;
  // Longest wait after repeated failures, in seconds.
  static constexpr uint32_t kLongestLockSec = 900;

  // What a valid session is.
  struct Who {
    CmdRole role = CmdRole::Full;
    bool pass = false;          // opened with a pass
    const char* user = "";      // stays valid until the session changes
    uint32_t leftSec = 0;
  };

  explicit WebAuth(hal::IRandom* rng = nullptr) : rng_(rng) {}
  void setRandom(hal::IRandom* rng) { rng_ = rng; }

  // Ok: token receives the new session, kTokenLength characters and the terminator.
  // Denied: wrong password. Busy: too many failures from this address, waitSec tells how long.
  // Unsupported: no source of random numbers, so no token can be trusted.
  Status login(StringView password, StringView expected, hal::Ip4 from, uint64_t nowMs,
               uint32_t ttlSec, char* token, uint32_t& waitSec);

  // A session for a pass already checked: its role, who it is, when it was issued (for
  // closePasses). Unsupported without random numbers.
  Status open(CmdRole role, StringView user, int64_t issued, uint64_t nowMs, uint32_t ttlSec,
              char* token);
  // Closes the sessions of the passes issued before that time. How many.
  size_t closePasses(int64_t issuedBefore);

  // A question for a pass, kNonceLength characters and the terminator: valid kNonceTtlSec, once.
  Status challenge(uint64_t nowMs, char* nonce);
  // True for a question asked here, in time and not used: from now on it is used.
  bool takeNonce(StringView nonce, uint64_t nowMs);

  // The limit to the failures, for the other ways in. Busy while the address waits.
  Status guardCheck(hal::Ip4 from, uint64_t nowMs, uint32_t& waitSec);
  // One more failure from the address: waitSec tells the wait it starts, if one starts.
  void guardFail(hal::Ip4 from, uint64_t nowMs, uint32_t& waitSec);
  void guardClear(hal::Ip4 from, uint64_t nowMs);

  // True for the token of a session still valid; who receives what it is.
  bool check(StringView token, uint64_t nowMs, Who* who = nullptr);
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
    CmdRole role = CmdRole::Full;
    bool pass = false;
    int64_t issued = 0;
    char user[kUserMax + 1] = {0};
  };
  struct Nonce {
    char text[kNonceLength + 1] = {0};
    uint64_t expiresMs = 0;
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
  // A free place, or the one of the session that has been silent the longest, with a new token.
  Session* place(uint64_t nowMs, uint32_t ttlSec, char* token);

  hal::IRandom* rng_;
  Session sessions_[LMP_MAX_SESSIONS];
  Guard guards_[LMP_MAX_SESSIONS];
  Nonce nonces_[LMP_MAX_SESSIONS];
  uint32_t failures_ = 0;
};

}  // namespace lmp
