// LoopMaxPro - the other way into the local console: a pass.
// A pass is a credential issued by someone the device trusts, for one device and one person: it
// says who uses it and with which role (CmdRole). The web service asks the question (the nonce),
// opens the session and keeps it; who attached checks the pass and the proof that the one who
// shows it is the one it was given to. Without anybody attached, the console takes no pass.
//
//   GET  /api/auth/challenge  -> {"nonce":"...","ttl":60}
//   POST /api/auth/pass       {"pass":"...","nonce":"...","proof":"..."}
//                             -> {"token":"...","ttl":..,"user":"...","role":"control"}
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "base/string_view.h"
#include "kernel/command/command.h"
#include "kernel/service/service.h"

namespace lmp {

class IPassChecker {
 public:
  // What a pass says, once checked.
  struct Grant {
    CmdRole role = CmdRole::Read;
    char user[33] = "";
    int64_t issued = 0;       // when it was issued, Unix time
    uint32_t maxSec = 0;      // the session lasts at most this long: up to the end of the pass
    uint8_t appKey[128];      // what the proof is checked with
    size_t appKeyLength = 0;
  };

  virtual ~IPassChecker() = default;
  // Ok with what the pass says. Denied with the field that failed: "pass" (form, signature),
  // "device" (another device), "expired", "revoked".
  virtual Status read(StringView pass, Grant& grant, const char*& field) = 0;
  // True when the proof is the signature of the one the pass was given to, on the nonce.
  virtual bool proves(const Grant& grant, StringView nonce, StringView proof) = 0;
};

// Offered by the web service.
class IWebAccess {
 public:
  static constexpr ServiceId kId = ServiceId::Web;
  virtual ~IWebAccess() = default;

  // One at a time: Busy when there is one already. It must live as long as it is attached.
  virtual Status attachPass(IPassChecker& checker) = 0;
  virtual void detachPass(IPassChecker& checker) = 0;
  // Closes the sessions of the passes issued before that time, Unix time. How many.
  virtual size_t closePasses(int64_t issuedBefore) = 0;
};

// The name of a role for who reads it: "owner" for the full one.
inline const char* passRoleName(CmdRole role) {
  switch (role) {
    case CmdRole::Full:    return "owner";
    case CmdRole::Control: return "control";
    case CmdRole::Read:    return "read";
  }
  return "read";
}

}  // namespace lmp
