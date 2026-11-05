// LoopMaxPro - streaming JSON writer.
// Writes straight into a sink: no document tree, no heap. Strings are always escaped.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/string_view.h"

namespace lmp {

// Destination of the generated text.
class JsonSink {
 public:
  virtual ~JsonSink() = default;
  // Returns false when the text does not fit.
  virtual bool write(const char* data, size_t len) = 0;
};

// Sink over a caller provided buffer. The content is always NUL terminated.
class BufferSink : public JsonSink {
 public:
  BufferSink(char* buffer, size_t capacity) : buf_(buffer), cap_(capacity) {
    if (cap_) buf_[0] = '\0';
  }

  bool write(const char* data, size_t len) override;

  const char* c_str() const { return cap_ ? buf_ : ""; }
  StringView view() const { return StringView(buf_, len_); }
  size_t size() const { return len_; }
  bool overflow() const { return overflow_; }
  void clear() {
    len_ = 0;
    overflow_ = false;
    if (cap_) buf_[0] = '\0';
  }

 private:
  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

template <size_t N>
class FixedSink : public BufferSink {
 public:
  FixedSink() : BufferSink(storage_, N) {}

 private:
  char storage_[N];
};

class JsonWriter {
 public:
  explicit JsonWriter(JsonSink& sink) : sink_(sink) {}

  // ---- structure
  JsonWriter& beginObject();
  JsonWriter& beginObject(const char* key) { return key_(key).beginObject(); }
  JsonWriter& endObject();
  JsonWriter& beginArray();
  JsonWriter& beginArray(const char* key) { return key_(key).beginArray(); }
  JsonWriter& endArray();
  JsonWriter& key(const char* k) { return key_(k); }

  // ---- values, to use after key() or inside an array
  JsonWriter& str(StringView v);
  JsonWriter& i64(int64_t v);
  JsonWriter& u64(uint64_t v);
  JsonWriter& f64(double v, uint8_t decimals = 2);
  JsonWriter& boolean(bool v);
  JsonWriter& null();
  // Inserts text that is already valid JSON.
  JsonWriter& raw(StringView json);

  // ---- key and value together
  JsonWriter& str(const char* k, StringView v) { return key_(k).str(v); }
  JsonWriter& str(const char* k, const char* v) { return key_(k).str(StringView(v)); }
  JsonWriter& i64(const char* k, int64_t v) { return key_(k).i64(v); }
  JsonWriter& u64(const char* k, uint64_t v) { return key_(k).u64(v); }
  JsonWriter& i32(const char* k, int32_t v) { return key_(k).i64(v); }
  JsonWriter& u32(const char* k, uint32_t v) { return key_(k).u64(v); }
  JsonWriter& f64(const char* k, double v, uint8_t decimals = 2) { return key_(k).f64(v, decimals); }
  JsonWriter& boolean(const char* k, bool v) { return key_(k).boolean(v); }
  JsonWriter& null(const char* k) { return key_(k).null(); }
  JsonWriter& raw(const char* k, StringView json) { return key_(k).raw(json); }

  // False after an overflow of the sink or a structural mistake.
  bool ok() const { return ok_; }
  // True when every object and array has been closed.
  bool complete() const { return ok_ && depth_ == 0 && !afterKey_; }

 private:
  static constexpr uint8_t kMaxDepth = 24;

  JsonWriter& key_(const char* k);
  void beforeValue();
  void open(char c);
  void close(char c);
  void put(char c) { put(&c, 1); }
  void put(const char* data, size_t len);
  void putEscaped(StringView s);

  JsonSink& sink_;
  uint32_t needComma_ = 0;  // one bit for each nesting level
  uint8_t depth_ = 0;
  bool afterKey_ = false;
  bool ok_ = true;
};

}  // namespace lmp
