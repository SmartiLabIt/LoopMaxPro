#include "services/web/web_auth.h"

#include <string.h>

#include "base/base64url.h"

namespace lmp {
namespace {

// A guard with no failures for this long is forgotten.
constexpr uint64_t kGuardMemoryMs = 3600000;

}  // namespace

bool sameSecret(StringView a, StringView b) {
  // Every character of the longer text is visited, whatever the content.
  const size_t n = a.size > b.size ? a.size : b.size;
  unsigned char diff = a.size == b.size ? 0 : 1;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char x = i < a.size ? static_cast<unsigned char>(a.data[i]) : 0;
    const unsigned char y = i < b.size ? static_cast<unsigned char>(b.data[i]) : 0;
    diff = static_cast<unsigned char>(diff | (x ^ y));
  }
  return diff == 0;
}

WebAuth::Guard& WebAuth::guardOf(hal::Ip4 ip, uint64_t nowMs) {
  Guard* free = nullptr;
  Guard* oldest = &guards_[0];
  for (Guard& g : guards_) {
    if (g.used && nowMs - g.lastMs > kGuardMemoryMs && nowMs >= g.lockedUntilMs) g = Guard();
    if (g.used && g.ip == ip) return g;
    if (!g.used && !free) free = &g;
    if (g.lastMs < oldest->lastMs) oldest = &g;
  }
  Guard& g = free ? *free : *oldest;
  g = Guard();
  g.used = true;
  g.ip = ip;
  g.lastMs = nowMs;
  return g;
}

Status WebAuth::guardCheck(hal::Ip4 from, uint64_t nowMs, uint32_t& waitSec) {
  waitSec = 0;
  Guard& guard = guardOf(from, nowMs);
  if (nowMs < guard.lockedUntilMs) {
    // Not even looked at: a locked address learns nothing from its attempts.
    waitSec = static_cast<uint32_t>((guard.lockedUntilMs - nowMs + 999) / 1000);
    return Status::Busy;
  }
  guard.lastMs = nowMs;
  return Status::Ok;
}

void WebAuth::guardFail(hal::Ip4 from, uint64_t nowMs, uint32_t& waitSec) {
  waitSec = 0;
  Guard& guard = guardOf(from, nowMs);
  guard.lastMs = nowMs;
  ++failures_;
  if (++guard.failures >= LMP_WEB_LOGIN_ATTEMPTS) {
    uint32_t lockSec = LMP_WEB_LOGIN_LOCK_SEC;
    for (uint8_t i = 0; i < guard.locks && lockSec < kLongestLockSec; ++i) lockSec *= 2;
    if (lockSec > kLongestLockSec) lockSec = kLongestLockSec;
    guard.lockedUntilMs = nowMs + static_cast<uint64_t>(lockSec) * 1000;
    guard.failures = 0;
    if (guard.locks < 255) ++guard.locks;
    waitSec = lockSec;
  }
}

void WebAuth::guardClear(hal::Ip4 from, uint64_t nowMs) { guardOf(from, nowMs) = Guard(); }

Status WebAuth::login(StringView password, StringView expected, hal::Ip4 from, uint64_t nowMs,
                      uint32_t ttlSec, char* token, uint32_t& waitSec) {
  waitSec = 0;
  if (token) token[0] = '\0';
  if (!rng_ || !token) return Status::Unsupported;
  LMP_TRY(guardCheck(from, nowMs, waitSec));

  // An empty password is never accepted, not even when none is configured.
  if (expected.empty() || !sameSecret(password, expected)) {
    guardFail(from, nowMs, waitSec);
    return Status::Denied;
  }
  guardClear(from, nowMs);
  place(nowMs, ttlSec, token);
  return Status::Ok;
}

Status WebAuth::open(CmdRole role, StringView user, int64_t issued, uint64_t nowMs,
                     uint32_t ttlSec, char* token) {
  if (token) token[0] = '\0';
  if (!rng_ || !token) return Status::Unsupported;
  Session* s = place(nowMs, ttlSec, token);
  s->role = role;
  s->pass = true;
  s->issued = issued;
  user.copyTo(s->user, sizeof(s->user));
  return Status::Ok;
}

WebAuth::Session* WebAuth::place(uint64_t nowMs, uint32_t ttlSec, char* token) {
  // A free place, or the one of the session that has been silent the longest.
  Session* place = nullptr;
  for (Session& s : sessions_) {
    if (s.used && nowMs >= s.expiresMs) s = Session();
    if (!s.used) {
      if (!place || place->used) place = &s;
    } else if (!place || (place->used && s.seenMs < place->seenMs)) {
      place = &s;
    }
  }

  *place = Session();
  static const char digits[] = "0123456789abcdef";
  uint8_t random[kTokenLength / 2];
  rng_->bytes(random, sizeof(random));
  for (size_t i = 0; i < sizeof(random); ++i) {
    place->token[2 * i] = digits[random[i] >> 4];
    place->token[2 * i + 1] = digits[random[i] & 0x0F];
  }
  place->token[kTokenLength] = '\0';
  place->expiresMs = nowMs + static_cast<uint64_t>(ttlSec) * 1000;
  place->seenMs = nowMs;
  place->used = true;
  memcpy(token, place->token, kTokenLength + 1);
  return place;
}

size_t WebAuth::closePasses(int64_t issuedBefore) {
  size_t closed = 0;
  for (Session& s : sessions_) {
    if (s.used && s.pass && s.issued < issuedBefore) {
      s = Session();
      ++closed;
    }
  }
  return closed;
}

Status WebAuth::challenge(uint64_t nowMs, char* nonce) {
  if (nonce) nonce[0] = '\0';
  if (!rng_ || !nonce) return Status::Unsupported;
  // A free place, or the question that ends first.
  Nonce* place = &nonces_[0];
  for (Nonce& n : nonces_) {
    if (!n.used || nowMs >= n.expiresMs) {
      place = &n;
      break;
    }
    if (n.expiresMs < place->expiresMs) place = &n;
  }
  uint8_t random[16];
  rng_->bytes(random, sizeof(random));
  base64UrlEncode(random, sizeof(random), place->text, sizeof(place->text));
  place->expiresMs = nowMs + static_cast<uint64_t>(kNonceTtlSec) * 1000;
  place->used = true;
  memcpy(nonce, place->text, kNonceLength + 1);
  return Status::Ok;
}

bool WebAuth::takeNonce(StringView nonce, uint64_t nowMs) {
  if (nonce.size != kNonceLength) return false;
  bool found = false;
  for (Nonce& n : nonces_) {
    const bool live = n.used && nowMs < n.expiresMs;
    if (live && sameSecret(nonce, StringView(n.text, kNonceLength))) {
      n = Nonce();
      found = true;
    }
  }
  return found;
}

const WebAuth::Session* WebAuth::find(StringView token, uint64_t nowMs) const {
  if (token.size != kTokenLength) return nullptr;
  const Session* found = nullptr;
  // Every session is compared, so the time does not tell which one matched.
  for (const Session& s : sessions_) {
    const bool live = s.used && nowMs < s.expiresMs;
    if (sameSecret(token, StringView(s.token, kTokenLength)) && live) found = &s;
  }
  return found;
}

bool WebAuth::check(StringView token, uint64_t nowMs, Who* who) {
  const Session* s = find(token, nowMs);
  if (!s) return false;
  const_cast<Session*>(s)->seenMs = nowMs;
  if (who) {
    who->role = s->role;
    who->pass = s->pass;
    who->user = s->user;
    who->leftSec = static_cast<uint32_t>((s->expiresMs - nowMs) / 1000);
  }
  return true;
}

uint32_t WebAuth::leftSec(StringView token, uint64_t nowMs) const {
  const Session* s = find(token, nowMs);
  return s ? static_cast<uint32_t>((s->expiresMs - nowMs) / 1000) : 0;
}

bool WebAuth::logout(StringView token) {
  if (token.size != kTokenLength) return false;
  for (Session& s : sessions_) {
    if (s.used && sameSecret(token, StringView(s.token, kTokenLength))) {
      s = Session();
      return true;
    }
  }
  return false;
}

void WebAuth::logoutAll() {
  for (Session& s : sessions_) s = Session();
  for (Nonce& n : nonces_) n = Nonce();
}

size_t WebAuth::sessions(uint64_t nowMs) const {
  size_t n = 0;
  for (const Session& s : sessions_) {
    if (s.used && nowMs < s.expiresMs) ++n;
  }
  return n;
}

}  // namespace lmp
