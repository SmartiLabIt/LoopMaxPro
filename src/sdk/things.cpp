#include "sdk/things.h"

#if LMP_FEATURE_THINGS

#include <stdio.h>
#include <string.h>

#include <new>

namespace lmp::sdk {

namespace {

// Texts and numbers into one number: enough to tell that something is no longer what it was.
constexpr uint32_t kSeed = 2166136261u;

uint32_t mixText(uint32_t h, const char* text) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : ""); *p; ++p) {
    h = (h ^ *p) * 16777619u;
  }
  return (h ^ 0x1Fu) * 16777619u;   // where a text ends: "ab" and "c" are not "a" and "bc"
}

uint32_t mixNumber(uint32_t h, uint32_t number) {
  for (int i = 0; i < 4; ++i) {
    h = (h ^ (number & 0xFFu)) * 16777619u;
    number >>= 8;
  }
  return h;
}

uint16_t fold(uint32_t h) { return static_cast<uint16_t>(h ^ (h >> 16)); }

// A number as it is shown: a change below what is shown is no change.
void numberText(char* out, size_t cap, double value, uint8_t decimals, bool known) {
  if (!known) {
    snprintf(out, cap, "?");
    return;
  }
  snprintf(out, cap, "%.*f", static_cast<int>(decimals > 9 ? 9 : decimals), value);
}

}  // namespace

// ---------------------------------------------------------------- the look

// Walks the things of a module and tells the book what they are now.
class ThingsLook final : public ThingSink {
 public:
  ThingsLook(ThingsBook& book, uint32_t rev) : book_(book), rev_(rev) {}

  void part(const char* id, const char* name) override {
    endPart();
    in_ = id && *id;
    if (!in_) return;
    ++parts_;
    partId_ = mixText(kSeed, id);
    // The parts are part of the description: one more, one less, another name.
    desc_ = mixText(mixText(desc_, id), name);
    partValues_ = kSeed;
  }

  void technical() override { technical_ = true; }

  void toggle(const char* id, ThingName name, bool on, const char* kind) override {
    thing("toggle", id, name, nullptr, kind, 0);
    value(id, on ? "1" : "0");
  }

  void light(const char* id, ThingName name, bool on, uint8_t brightness, uint8_t r, uint8_t g,
             uint8_t b, bool colours) override {
    thing("light", id, name, nullptr, colours ? "rgb" : "brightness", 0);
    char text[32];
    snprintf(text, sizeof(text), "%d %u %u %u %u", on ? 1 : 0, static_cast<unsigned>(brightness),
             static_cast<unsigned>(colours ? r : 0), static_cast<unsigned>(colours ? g : 0),
             static_cast<unsigned>(colours ? b : 0));
    value(id, text);
  }

  void number(const char* id, ThingName name, double number, const char* unit, const char* kind,
              uint8_t decimals, bool known) override {
    thing("number", id, name, unit, kind, decimals);
    char text[40];
    numberText(text, sizeof(text), number, decimals, known);
    value(id, text);
  }

  void flag(const char* id, ThingName name, bool on, const char* kind) override {
    thing("flag", id, name, nullptr, kind, 0);
    value(id, on ? "1" : "0");
  }

  void text(const char* id, ThingName name, const char* word) override {
    thing("text", id, name, nullptr, nullptr, 0);
    value(id, word);
  }

  void button(const char* id, ThingName name) override {
    thing("button", id, name, nullptr, nullptr, 0);
  }

  // After the last thing. True when a part is new or changed.
  bool end() {
    endPart();
    return changed_;
  }

  uint32_t desc() const { return desc_; }
  uint32_t ownValues() const { return ownValues_; }
  uint16_t own() const { return own_; }
  uint16_t parts() const { return parts_; }

 private:
  void thing(const char* kind, const char* id, ThingName name, const char* unit, const char* type,
             uint8_t decimals) {
    if (!in_) ++own_;
    uint32_t h = mixText(mixText(desc_, kind), id);
    h = mixText(mixText(h, name.en), name.it);
    h = mixText(mixText(h, unit), type);
    desc_ = mixNumber(h, static_cast<uint32_t>(decimals) | (technical_ ? 0x100u : 0u) |
                             (in_ ? 0x200u : 0u));
    technical_ = false;
  }

  void value(const char* id, const char* text) {
    uint32_t& h = in_ ? partValues_ : ownValues_;
    h = mixText(mixText(h, id), text);
  }

  void endPart() {
    if (!in_) return;
    in_ = false;
    if (book_.take(partId_, fold(partValues_), rev_)) changed_ = true;
  }

  ThingsBook& book_;
  uint32_t rev_;
  uint32_t desc_ = kSeed;
  uint32_t ownValues_ = kSeed;
  uint32_t partValues_ = kSeed;
  uint32_t partId_ = 0;
  uint16_t own_ = 0;
  uint16_t parts_ = 0;
  bool in_ = false;
  bool technical_ = false;
  bool changed_ = false;
};

bool ThingsBook::take(uint32_t id, uint16_t values, uint32_t rev) {
  for (uint16_t i = 0; i < used_; ++i) {
    Part& p = table_[i];
    if (p.id != id) continue;
    p.seen = true;
    if (p.values == values) return false;
    p.values = values;
    p.rev = rev;
    return true;
  }
  if (!table_) table_ = new (std::nothrow) Part[LMP_THINGS_PARTS];
  if (!table_ || used_ >= LMP_THINGS_PARTS) {
    // No room for this one: its values are followed together with the others left out, and
    // who asks takes it as changed every time.
    over_ = static_cast<uint16_t>(over_ * 31u + values + 1u);
    return false;
  }
  table_[used_++] = Part{id, rev, values, true};
  return true;
}

bool ThingsBook::look(const Module& module) {
  for (uint16_t i = 0; i < used_; ++i) table_[i].seen = false;
  const uint16_t overBefore = over_;
  over_ = 0;
  ThingsLook now(*this, rev_ + 1);
  module.thingsAll(now);
  bool changed = now.end();

  // The parts that are gone leave their place.
  uint16_t kept = 0;
  for (uint16_t i = 0; i < used_; ++i) {
    if (!table_[i].seen) continue;
    if (kept != i) table_[kept] = table_[i];
    ++kept;
  }
  used_ = kept;

  if (now.own() == 0 && now.parts() == 0) {
    const bool had = any();
    gen_ = 0;
    own_ = parts_ = 0;
    ownValues_ = over_ = 0;
    if (had) ++rev_;
    return had;
  }
  uint16_t gen = fold(now.desc());
  if (gen == 0) gen = 1;   // 0 is "no things"
  const uint16_t ownValues = fold(now.ownValues());
  if (gen != gen_ || ownValues != ownValues_ || over_ != overBefore) changed = true;
  gen_ = gen;
  own_ = now.own();
  parts_ = now.parts();
  ownValues_ = ownValues;
  if (changed) ++rev_;
  return changed;
}

void ThingsBook::take(ThingsBook& other) {
  gen_ = other.gen_;
  rev_ = other.rev_;
  own_ = other.own_;
  ownValues_ = other.ownValues_;
  parts_ = other.parts_;
  over_ = other.over_;
  table_ = other.table_;
  used_ = other.used_;
  other.table_ = nullptr;
  other.used_ = 0;
}

void ThingsBook::forget() {
  delete[] table_;
  table_ = nullptr;
  used_ = 0;
  gen_ = 0;
  own_ = parts_ = 0;
  ownValues_ = over_ = 0;
  // rev stays: who kept a copy sees that what comes next is not what it has.
}

uint32_t ThingsBook::changedAt(const char* part) const {
  const uint32_t id = mixText(kSeed, part);
  for (uint16_t i = 0; i < used_; ++i) {
    if (table_[i].id == id) return table_[i].rev;
  }
  return rev_;
}

// ---------------------------------------------------------------- what they are

namespace {

// The things of the module itself, or of its first part: every part has the same.
class DescribeSink final : public ThingSink {
 public:
  DescribeSink(JsonWriter& w, bool ofPart) : w_(w), ofPart_(ofPart) {}

  void part(const char* id, const char*) override {
    in_ = id && *id;
    if (in_) ++partNo_;
  }
  void technical() override { technical_ = true; }
  void toggle(const char* id, ThingName name, bool, const char* kind) override {
    thing("toggle", id, name, nullptr, kind, 0);
  }
  void light(const char* id, ThingName name, bool, uint8_t, uint8_t, uint8_t, uint8_t,
             bool colours) override {
    thing("light", id, name, nullptr, colours ? "rgb" : "brightness", 0);
  }
  void number(const char* id, ThingName name, double, const char* unit, const char* kind,
              uint8_t decimals, bool) override {
    thing("number", id, name, unit, kind, decimals);
  }
  void flag(const char* id, ThingName name, bool, const char* kind) override {
    thing("flag", id, name, nullptr, kind, 0);
  }
  void text(const char* id, ThingName name, const char*) override {
    thing("text", id, name, nullptr, nullptr, 0);
  }
  void button(const char* id, ThingName name) override {
    thing("button", id, name, nullptr, nullptr, 0);
  }

 private:
  void thing(const char* kind, const char* id, ThingName name, const char* unit, const char* type,
             uint8_t decimals) {
    const bool apart = technical_;
    technical_ = false;
    if (ofPart_ ? !(in_ && partNo_ == 1) : in_) return;
    w_.beginObject();
    w_.str("id", id);
    w_.str("kind", kind);
    // A text of the firmware has its languages; a name the owner gave is one text.
    const char* en = name.en && *name.en ? name.en : id;
    const char* it = name.it && *name.it ? name.it : en;
    if (en == it || strcmp(en, it) == 0) {
      w_.str("name", en);
    } else {
      w_.beginObject("name");
      w_.str("en", en);
      w_.str("it", it);
      w_.endObject();
    }
    if (type && *type) w_.str("type", type);
    if (unit && *unit) w_.str("unit", unit);
    if (decimals) w_.u32("decimals", decimals);
    if (apart) w_.boolean("technical", true);
    w_.endObject();
  }

  JsonWriter& w_;
  bool ofPart_;
  bool in_ = false;
  bool technical_ = false;
  uint16_t partNo_ = 0;
};

// The parts, by id and name, a page of them.
class PartsSink final : public ThingSink {
 public:
  PartsSink(JsonWriter& w, size_t from, size_t max) : w_(w), from_(from), max_(max) {}

  void part(const char* id, const char* name) override {
    if (!id || !*id) return;
    const size_t at = seen_++;
    if (at < from_) return;
    if (written_ >= max_) {
      if (!next_) next_ = at;
      return;
    }
    w_.beginObject();
    w_.str("id", id);
    w_.str("name", name && *name ? name : id);
    w_.endObject();
    ++written_;
  }
  void toggle(const char*, ThingName, bool, const char*) override {}
  void light(const char*, ThingName, bool, uint8_t, uint8_t, uint8_t, uint8_t, bool) override {}
  void number(const char*, ThingName, double, const char*, const char*, uint8_t, bool) override {}
  void flag(const char*, ThingName, bool, const char*) override {}
  void text(const char*, ThingName, const char*) override {}
  void button(const char*, ThingName) override {}

  // Where the next page starts; 0 when this one was the last.
  size_t next() const { return next_; }

 private:
  JsonWriter& w_;
  size_t from_;
  size_t max_;
  size_t seen_ = 0;
  size_t written_ = 0;
  size_t next_ = 0;
};

// The values. Without a book: the ones of the module itself, as members of the object that is
// open. With a book: one object for each part changed after since, a page of them.
class ValuesSink final : public ThingSink {
 public:
  ValuesSink(JsonWriter& w, const ThingsBook* book, uint32_t since, size_t from, size_t max)
      : w_(w), book_(book), since_(since), from_(from), max_(max) {}

  void part(const char* id, const char*) override {
    closePart();
    in_ = id && *id;
    if (!in_ || !book_) return;
    const size_t at = seen_++;
    if (at < from_ || book_->changedAt(id) <= since_) return;
    if (written_ >= max_) {
      if (!next_) next_ = at;
      return;
    }
    w_.beginObject();
    w_.str("id", id);
    w_.beginObject("values");
    open_ = true;
    ++written_;
  }
  void toggle(const char* id, ThingName, bool on, const char*) override {
    if (writes()) w_.boolean(id, on);
  }
  void light(const char* id, ThingName, bool on, uint8_t brightness, uint8_t r, uint8_t g,
             uint8_t b, bool colours) override {
    if (!writes()) return;
    w_.beginObject(id);
    w_.boolean("on", on);
    w_.u32("brightness", brightness);
    if (colours) {
      w_.u32("r", r);
      w_.u32("g", g);
      w_.u32("b", b);
    }
    w_.endObject();
  }
  void number(const char* id, ThingName, double value, const char*, const char*, uint8_t decimals,
              bool known) override {
    if (!writes()) return;
    // Not known is not zero: who reads must be able to tell.
    if (known) {
      w_.f64(id, value, decimals);
    } else {
      w_.null(id);
    }
  }
  void flag(const char* id, ThingName, bool on, const char*) override {
    if (writes()) w_.boolean(id, on);
  }
  void text(const char* id, ThingName, const char* word) override {
    if (writes()) w_.str(id, word ? word : "");
  }
  void button(const char*, ThingName) override {}

  void end() { closePart(); }
  size_t next() const { return next_; }

 private:
  bool writes() const { return book_ ? open_ : !in_; }
  void closePart() {
    if (!open_) return;
    open_ = false;
    w_.endObject();
    w_.endObject();
  }

  JsonWriter& w_;
  const ThingsBook* book_;
  uint32_t since_;
  size_t from_;
  size_t max_;
  size_t seen_ = 0;
  size_t written_ = 0;
  size_t next_ = 0;
  bool in_ = false;
  bool open_ = false;
};

}  // namespace

void thingsDescribe(const Module& module, const ThingsBook& book, size_t from, JsonWriter& w) {
  w.u32("gen", book.gen());
  if (from == 0) {
    w.beginArray("things");
    DescribeSink own(w, false);
    module.thingsAll(own);
    w.endArray();
    if (book.parts()) {
      w.beginArray("partThings");
      DescribeSink first(w, true);
      module.thingsAll(first);
      w.endArray();
    }
  }
  if (!book.parts()) return;
  w.beginArray("parts");
  PartsSink names(w, from, kThingPartsPage);
  module.thingsAll(names);
  w.endArray();
  if (names.next()) w.u32("next", static_cast<uint32_t>(names.next()));
}

void thingsOwnValues(const Module& module, JsonWriter& w) {
  ValuesSink own(w, nullptr, 0, 0, 0);
  module.thingsAll(own);
}

void thingsPartValues(const Module& module, const ThingsBook& book, uint32_t since, size_t from,
                      JsonWriter& w) {
  w.u32("rev", book.rev());
  w.beginArray("parts");
  ValuesSink parts(w, &book, since, from, kThingValuesPage);
  module.thingsAll(parts);
  parts.end();
  w.endArray();
  if (parts.next()) w.u32("next", static_cast<uint32_t>(parts.next()));
}

Status thingsSet(Module& module, const Args& a) {
  const char* thing = a.str("thing");
  if (!*thing) return Status::Invalid;
  ThingAsk ask;
  const char* value = a.str("value");
  if (strcmp(value, "ON") == 0 || strcmp(value, "OFF") == 0) {
    ask.hasOn = true;
    ask.on = value[1] == 'N';
  } else if (strcmp(value, "PRESS") == 0) {
    ask.press = true;
  } else if (*value) {
    return Status::Invalid;
  }
  if (a.has("brightness")) {
    ask.hasBrightness = true;
    ask.brightness = a.u8("brightness");
  }
  if (a.has("r") || a.has("g") || a.has("b")) {
    ask.hasColour = true;
    ask.r = a.u8("r");
    ask.g = a.u8("g");
    ask.b = a.u8("b");
  }
  if (!ask.hasOn && !ask.press && !ask.hasBrightness && !ask.hasColour) return Status::Invalid;
  return module.thingSetAll(thing, ask);
}

}  // namespace lmp::sdk

#endif  // LMP_FEATURE_THINGS
