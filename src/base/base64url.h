// LoopMaxPro - base64url (RFC 4648 chapter 5), without padding: the form of signed tickets.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/string_view.h"

namespace lmp {

// Decodes into out. False when a character is not of the alphabet, when the length is not
// possible, or when the bytes do not fit. length receives the bytes written.
bool base64UrlDecode(StringView text, uint8_t* out, size_t cap, size_t& length);

// Encodes, without padding and with the terminator. False when the text does not fit.
bool base64UrlEncode(const uint8_t* data, size_t length, char* out, size_t cap);

}  // namespace lmp
