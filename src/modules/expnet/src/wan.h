// ExpNet - the traffic of the house from the devices that do not say it with UPnP: the dish
// of Starlink (gRPC-Web), a 4G or 5G router of Huawei (HiLink), a router with SNMP. Only how
// each one is asked and how its answer is read: who asks, and when, is the module.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace expnet {
namespace wan {

// ---------------------------------------------------------------- Starlink
//
// The dish answers at 192.168.100.1 from the network of the house, without a password: a POST
// with one message, field 1004 (get_status) empty, and the status comes back in field 2004.
// One frame each way: a byte of flags, the length in four bytes, the message.

constexpr uint16_t kStarlinkPort = 9201;
constexpr const char* kStarlinkDish = "192.168.100.1";
constexpr const char* kStarlinkPath = "/SpaceX.API.Device.Device/Handle";
constexpr const char* kStarlinkType = "application/grpc-web+proto";
constexpr char kStarlinkAsk[] = {0, 0, 0, 0, 3, static_cast<char>(0xE2), 0x3E, 0};

// The fields read, by their numbers in the messages of the dish.
constexpr uint32_t kDishStatus = 2004;        // in the answer: the status of a dish
constexpr uint32_t kDishState = 2;            // in the status: a message, its field 1 the seconds on
constexpr uint32_t kDishDrop = 1003;          // share of the pings to the ground lost
constexpr uint32_t kDishObstruction = 1004;   // a message, its field 1 the share of time obstructed
constexpr uint32_t kDishDown = 1007;          // bit/s now
constexpr uint32_t kDishUp = 1008;
constexpr uint32_t kDishPing = 1009;          // ms to the ground

struct Starlink {
  bool has = false;       // the answer was the status of a dish
  float downBps = 0, upBps = 0;
  float pingMs = 0;       // 0: not said
  float drop = 0;         // 0 to 1
  float obstructed = 0;   // 0 to 1
  uint32_t uptimeS = 0;
};

struct Pb {
  const uint8_t* at;
  const uint8_t* end;
};

inline bool pbVarint(Pb& p, uint64_t& out) {
  out = 0;
  for (uint8_t shift = 0; shift < 64 && p.at < p.end; shift += 7) {
    const uint8_t b = *p.at++;
    out |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

// The next field: its number, and its value as a number or as bytes. A message the buffer cut
// gives the bytes that are there. False at the end, or where the bytes are not a message.
inline bool pbNext(Pb& p, uint32_t& field, uint8_t& type, uint64_t& number, Pb& bytes) {
  uint64_t key = 0;
  if (p.at >= p.end || !pbVarint(p, key)) return false;
  field = static_cast<uint32_t>(key >> 3);
  type = static_cast<uint8_t>(key & 7);
  number = 0;
  bytes.at = bytes.end = p.at;
  if (type == 0) return pbVarint(p, number);
  if (type == 1 || type == 5) {
    const size_t n = type == 1 ? 8 : 4;
    if (static_cast<size_t>(p.end - p.at) < n) return false;
    memcpy(&number, p.at, n);   // little endian, as the boards and the PC are
    p.at += n;
    return true;
  }
  if (type != 2) return false;
  uint64_t length = 0;
  if (!pbVarint(p, length)) return false;
  const size_t left = static_cast<size_t>(p.end - p.at);
  const size_t n = length < left ? static_cast<size_t>(length) : left;
  bytes.at = p.at;
  bytes.end = p.at + n;
  p.at += n;
  return true;
}

// A number of the dish: never negative, never "not a number".
inline float pbShare(uint64_t number) {
  const uint32_t raw = static_cast<uint32_t>(number);
  float f = 0;
  memcpy(&f, &raw, sizeof(f));
  return f == f && f > 0 && f < 1e12f ? f : 0;
}

inline bool starlinkRead(const uint8_t* body, size_t len, Starlink& out) {
  out = Starlink();
  // The first frame is the message; one with the top bit set is only the end of an answer
  // without one.
  if (!body || len < 6 || (body[0] & 0x80)) return false;
  const size_t said = (static_cast<size_t>(body[1]) << 24) | (static_cast<size_t>(body[2]) << 16) |
                      (static_cast<size_t>(body[3]) << 8) | body[4];
  const size_t n = said < len - 5 ? said : len - 5;
  Pb answer{body + 5, body + 5 + n}, status{nullptr, nullptr}, inner{nullptr, nullptr};
  uint32_t field = 0;
  uint8_t type = 0;
  uint64_t number = 0;
  Pb bytes{nullptr, nullptr};
  while (pbNext(answer, field, type, number, bytes)) {
    if (field == kDishStatus && type == 2) {
      status = bytes;
      out.has = bytes.end > bytes.at;
    }
  }
  if (!out.has) return false;
  while (pbNext(status, field, type, number, bytes)) {
    if (field == kDishDown && type == 5) out.downBps = pbShare(number);
    else if (field == kDishUp && type == 5) out.upBps = pbShare(number);
    else if (field == kDishPing && type == 5) out.pingMs = pbShare(number);
    else if (field == kDishDrop && type == 5) out.drop = pbShare(number);
    else if ((field == kDishObstruction || field == kDishState) && type == 2) {
      const bool state = field == kDishState;
      inner = bytes;
      uint32_t f = 0;
      uint8_t t = 0;
      uint64_t v = 0;
      Pb skip{nullptr, nullptr};
      while (pbNext(inner, f, t, v, skip)) {
        if (f != 1) continue;
        if (state && t == 0) out.uptimeS = static_cast<uint32_t>(v);
        if (!state && t == 5) out.obstructed = pbShare(v);
      }
    }
  }
  if (out.drop > 1) out.drop = 1;
  if (out.obstructed > 1) out.obstructed = 1;
  return true;
}

// ---------------------------------------------------------------- Huawei HiLink
//
// The 4G and 5G routers of Huawei answer with XML at the address of the router. The traffic of
// this moment is in bytes a second. The newer ones want a session first: its cookie is asked
// at kHuaweiSession and sent back with every question.

constexpr const char* kHuaweiTraffic = "/api/monitoring/traffic-statistics";
constexpr const char* kHuaweiSession = "/api/webserver/SesTokInfo";

// The text between <tag> and </tag>. False when the tag is not there or its text does not fit.
inline bool xmlText(const char* xml, const char* tag, char* out, size_t cap) {
  const size_t tagLen = strlen(tag);
  for (const char* at = xml; (at = strchr(at, '<')) != nullptr; ++at) {
    if (strncmp(at + 1, tag, tagLen) != 0 || at[1 + tagLen] != '>') continue;
    const char* begin = at + tagLen + 2;
    const char* end = strchr(begin, '<');
    if (!end || static_cast<size_t>(end - begin) >= cap) return false;
    memcpy(out, begin, static_cast<size_t>(end - begin));
    out[end - begin] = '\0';
    return true;
  }
  return false;
}

inline bool xmlNumber(const char* xml, const char* tag, uint32_t& out) {
  char text[24];
  if (!xmlText(xml, tag, text, sizeof(text)) || text[0] < '0' || text[0] > '9') return false;
  const unsigned long long v = strtoull(text, nullptr, 10);
  out = v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
  return true;
}

// The traffic of this moment, bytes a second. refused: the router answered that it wants a
// session, or a password.
inline bool huaweiRead(const char* xml, uint32_t& down, uint32_t& up, bool& refused) {
  refused = strstr(xml, "<error>") != nullptr;
  return !refused && xmlNumber(xml, "CurrentDownloadRate", down) && xmlNumber(xml, "CurrentUploadRate", up);
}

// The cookie of a session, as it goes in the header: "SessionID=…".
inline bool huaweiSession(const char* xml, char* cookie, size_t cap) {
  char text[200];
  if (!xmlText(xml, "SesInfo", text, sizeof(text)) || !text[0]) return false;
  const bool whole = strncmp(text, "SessionID=", 10) == 0;
  const size_t need = strlen(text) + (whole ? 0 : 10);
  if (need >= cap) return false;
  cookie[0] = '\0';
  if (!whole) strcpy(cookie, "SessionID=");
  strcat(cookie, text);
  return true;
}

// ---------------------------------------------------------------- SNMP v2c
//
// One datagram asks the value of some objects of a router, one answers. The bytes a port
// received and sent are counters: the traffic is how much they grew.

constexpr uint16_t kSnmpPort = 161;
constexpr uint8_t kBerInt = 0x02, kBerOctets = 0x04, kBerNull = 0x05, kBerOid = 0x06, kBerSeq = 0x30;
constexpr uint8_t kSnmpGet = 0xA0, kSnmpNext = 0xA1, kSnmpAnswer = 0xA2;

// The name of an object: numbers, the last one often the port.
struct Oid {
  uint32_t ids[20];
  uint8_t n;
};

// name: the numbers of an object without its last one; then, when it has one, the last.
inline Oid oidOf(const uint16_t* name, uint8_t count, bool more = false, uint32_t last = 0) {
  Oid o{};
  for (uint8_t i = 0; i < count && o.n < 19; ++i) o.ids[o.n++] = name[i];
  if (more) o.ids[o.n++] = last;
  return o;
}
template <size_t N>
inline Oid oidOf(const uint16_t (&name)[N]) {
  return oidOf(name, static_cast<uint8_t>(N));
}
template <size_t N>
inline Oid oidOf(const uint16_t (&name)[N], uint32_t last) {
  return oidOf(name, static_cast<uint8_t>(N), true, last);
}

// The objects asked. With a port after them: bytes in and out (64 bit, and the old ones of
// 32), the speed of the port in Mbit/s and in bit/s.
constexpr uint16_t kOidInHc[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 6};
constexpr uint16_t kOidOutHc[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 10};
constexpr uint16_t kOidMbit[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15};
constexpr uint16_t kOidIn[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 10};
constexpr uint16_t kOidOut[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 16};
constexpr uint16_t kOidBit[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 5};
// The port the router sends to Internet from: the one of its route to everywhere, in the three
// tables a router may keep. The first is asked as it is, the others for what follows them.
constexpr uint16_t kOidRoute[] = {1, 3, 6, 1, 2, 1, 4, 21, 1, 2, 0, 0, 0, 0};
constexpr uint16_t kOidRouteCidr[] = {1, 3, 6, 1, 2, 1, 4, 24, 4, 1, 5, 0, 0, 0, 0};
constexpr uint16_t kOidRouteInet[] = {1, 3, 6, 1, 2, 1, 4, 24, 7, 1, 7, 1, 4, 0, 0, 0, 0};

// Written from the end of the buffer towards its start: the length of a piece is known when
// what is inside it has been written.
struct BerOut {
  uint8_t* buf;
  size_t at;
  bool ok = true;

  void byte(uint8_t b) {
    if (at == 0) {
      ok = false;
      return;
    }
    buf[--at] = b;
  }
  // Closes under a tag what was written since the buffer was at end.
  void wrap(uint8_t tag, size_t end) {
    const size_t n = end - at;
    if (n < 128) {
      byte(static_cast<uint8_t>(n));
    } else {
      byte(static_cast<uint8_t>(n));
      byte(static_cast<uint8_t>(n >> 8));
      byte(0x82);
    }
    byte(tag);
  }
  void integer(uint32_t v) {
    const size_t end = at;
    do {
      byte(static_cast<uint8_t>(v));
      v >>= 8;
    } while (v);
    if (ok && (buf[at] & 0x80)) byte(0);   // never a negative number
    wrap(kBerInt, end);
  }
  void oid(const Oid& o) {
    const size_t end = at;
    for (uint8_t i = o.n; i-- > 2;) {
      uint32_t v = o.ids[i];
      byte(static_cast<uint8_t>(v & 0x7F));
      for (v >>= 7; v; v >>= 7) byte(static_cast<uint8_t>(0x80 | (v & 0x7F)));
    }
    byte(static_cast<uint8_t>(o.ids[0] * 40 + o.ids[1]));
    wrap(kBerOid, end);
  }
};

// A question for some objects, written at the end of buf: where it begins, and its length.
// Null when it does not fit. kind: kSnmpGet for the objects named, kSnmpNext for the ones that
// follow them.
inline const uint8_t* snmpAsk(uint8_t* buf, size_t cap, const char* community, uint32_t id, uint8_t kind,
                              const Oid* oids, uint8_t count, size_t& len) {
  BerOut o{buf, cap};
  for (uint8_t i = count; i-- > 0;) {
    const size_t end = o.at;
    o.byte(0);
    o.byte(kBerNull);
    o.oid(oids[i]);
    o.wrap(kBerSeq, end);
  }
  o.wrap(kBerSeq, cap);
  o.integer(0);   // no error, twice: its place and its kind
  o.integer(0);
  o.integer(id & 0x7FFFFFFFu);
  o.wrap(kind, cap);
  const size_t end = o.at;
  for (size_t i = strlen(community); i-- > 0;) o.byte(static_cast<uint8_t>(community[i]));
  o.wrap(kBerOctets, end);
  o.integer(1);   // version 2c
  o.wrap(kBerSeq, cap);
  if (!o.ok) return nullptr;
  len = cap - o.at;
  return buf + o.at;
}

struct BerIn {
  const uint8_t* at;
  const uint8_t* end;
};

inline bool berNext(BerIn& in, uint8_t& tag, BerIn& body) {
  if (in.end - in.at < 2) return false;
  tag = *in.at++;
  size_t len = *in.at++;
  if (len & 0x80) {
    uint8_t n = static_cast<uint8_t>(len & 0x7F);
    if (n == 0 || n > 3 || in.end - in.at < n) return false;
    for (len = 0; n; --n) len = (len << 8) | *in.at++;
  }
  if (static_cast<size_t>(in.end - in.at) < len) return false;
  body.at = in.at;
  body.end = in.at + len;
  in.at += len;
  return true;
}

inline uint64_t berNumber(BerIn b) {
  uint64_t v = 0;
  while (b.at < b.end) v = (v << 8) | *b.at++;
  return v;
}

struct SnmpValue {
  bool has = false;              // a number: an object that is not there has none
  uint64_t number = 0;
  const uint8_t* name = nullptr; // the object it is of, as it travels
  size_t nameLen = 0;
};

// Reads the answer to question id: one value for each object, in the order of the answer.
// False when it is not that answer, or it tells an error.
inline bool snmpRead(const uint8_t* data, size_t len, uint32_t id, SnmpValue* values, uint8_t cap, uint8_t& count) {
  count = 0;
  BerIn in{data, data + len}, message{}, part{}, pdu{}, list{}, bind{}, value{};
  uint8_t tag = 0;
  if (!berNext(in, tag, message) || tag != kBerSeq) return false;
  if (!berNext(message, tag, part) || tag != kBerInt) return false;
  if (!berNext(message, tag, part) || tag != kBerOctets) return false;
  if (!berNext(message, tag, pdu) || tag != kSnmpAnswer) return false;
  if (!berNext(pdu, tag, part) || tag != kBerInt || berNumber(part) != (id & 0x7FFFFFFFu)) return false;
  if (!berNext(pdu, tag, part) || tag != kBerInt || berNumber(part) != 0) return false;
  if (!berNext(pdu, tag, part) || tag != kBerInt) return false;
  if (!berNext(pdu, tag, list) || tag != kBerSeq) return false;
  while (count < cap && berNext(list, tag, bind) && tag == kBerSeq) {
    SnmpValue& v = values[count];
    v = SnmpValue();
    if (!berNext(bind, tag, part) || tag != kBerOid) return false;
    v.name = part.at;
    v.nameLen = static_cast<size_t>(part.end - part.at);
    if (!berNext(bind, tag, value)) return false;
    // A number, a counter of 32 or 64 bit, a gauge, a time.
    if (tag == kBerInt || tag == 0x41 || tag == 0x42 || tag == 0x43 || tag == 0x46) {
      v.has = true;
      v.number = berNumber(value);
    }
    ++count;
  }
  return true;
}

// True when the object of a value is under the one asked: what "the next" of a table gave is
// still a row of that table.
inline bool snmpUnder(const SnmpValue& v, const Oid& asked) {
  uint8_t text[80];
  BerOut o{text, sizeof(text)};
  o.oid(asked);
  if (!o.ok) return false;
  const size_t skip = 2;   // its tag and its length
  const size_t n = sizeof(text) - o.at - skip;
  return v.name && v.nameLen >= n && memcmp(v.name, text + o.at + skip, n) == 0;
}

}  // namespace wan
}  // namespace expnet
