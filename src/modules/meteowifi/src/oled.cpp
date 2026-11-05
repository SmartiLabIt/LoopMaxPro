#include "oled.h"

#include <string.h>

#include "hal/api/memory.h"

namespace meteowifi {
namespace {

constexpr size_t kImage = Oled::kWidth * Oled::kHeight / 8;
// The bus of the ESP8266 takes 128 bytes a transfer, and I2cDevice 32: the image goes in
// pieces of 32.
constexpr size_t kPiece = 32;
constexpr uint8_t kCommand = 0x00;
constexpr uint8_t kData = 0x40;

// Start of the SSD1306 for 128x64 with its own charge pump, as the boards have it.
const uint8_t kStart[] = {
    0xAE,         // off while it is set up
    0xD5, 0x80,   // clock
    0xA8, 0x3F,   // 64 lines
    0xD3, 0x00,   // no vertical offset
    0x40,         // first line 0
    0x8D, 0x14,   // charge pump on
    0x20, 0x00,   // addresses move across the columns, then to the next page
    0xA1, 0xC8,   // mirrored both ways: the way the screen is mounted on the boards
    0xDA, 0x12,   // wiring of the rows
    0x81, 0xCF,   // contrast
    0xD9, 0xF1,   // pre-charge
    0xDB, 0x40,   // VCOMH
    0xA4, 0xA6,   // show the memory, not inverted
    0x2E,         // no scrolling
    0xAF,         // on
};

const Picture* glyphOf(const Picture* font, const char* chars, char c) {
  const char* at = strchr(chars, c);
  return at && c ? &font[at - chars] : nullptr;
}

}  // namespace

lmp::Status Oled::begin(lmp::hal::II2c* bus, uint8_t address) {
  end();
  chip_ = lmp::sdk::I2cDevice(address);
  LMP_TRY(chip_.attach(bus));
  image_ = static_cast<uint8_t*>(lmp::hal::allocLarge(kImage));
  if (!image_) return lmp::Status::NoMem;
  const lmp::Status s = commands(kStart, sizeof(kStart));
  if (s != lmp::Status::Ok) {
    end();
    return s;
  }
  clear();
  return show();
}

void Oled::end() {
  if (image_) lmp::hal::freeLarge(image_);
  image_ = nullptr;
}

void Oled::clear() {
  if (image_) memset(image_, 0, kImage);
}

void Oled::pixel(int x, int y, bool on) {
  if (!image_ || x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  uint8_t& cell = image_[(y / 8) * kWidth + x];
  const uint8_t bit = static_cast<uint8_t>(1u << (y & 7));
  cell = on ? (cell | bit) : (cell & ~bit);
}

void Oled::draw(const Picture& picture, int x, int y) {
  const int stride = (picture.w + 7) / 8;
  for (int row = 0; row < picture.h; ++row) {
    for (int col = 0; col < picture.w; ++col) {
      const uint8_t bits = ART_READ(&picture.bits[row * stride + col / 8]);
      if (bits & (0x80 >> (col & 7))) pixel(x + col, y + row);
    }
  }
}

int Oled::text(const Picture* font, const char* chars, int x, int y, const char* s, int gap) {
  const int start = x;
  for (; s && *s; ++s) {
    const Picture* g = glyphOf(font, chars, *s);
    if (!g) continue;
    draw(*g, x, y);
    x += g->w + gap;
  }
  return x > start ? x - start - gap : 0;
}

int Oled::textWidth(const Picture* font, const char* chars, const char* s, int gap) {
  int w = 0;
  for (; s && *s; ++s) {
    const Picture* g = glyphOf(font, chars, *s);
    if (g) w += g->w + gap;
  }
  return w > 0 ? w - gap : 0;
}

lmp::Status Oled::show() {
  if (!image_) return lmp::Status::Denied;
  static const uint8_t kWindow[] = {0x21, 0, kWidth - 1, 0x22, 0, kHeight / 8 - 1};
  LMP_TRY(commands(kWindow, sizeof(kWindow)));
  for (size_t at = 0; at < kImage; at += kPiece) {
    LMP_TRY(chip_.write(kData, image_ + at, kPiece));
  }
  return lmp::Status::Ok;
}

lmp::Status Oled::power(bool on) {
  const uint8_t cmd = on ? 0xAF : 0xAE;
  return commands(&cmd, 1);
}

lmp::Status Oled::commands(const uint8_t* list, size_t len) {
  // A few at a time after the control byte: the chip takes a list of them.
  for (size_t at = 0; at < len; at += kPiece) {
    const size_t n = len - at < kPiece ? len - at : kPiece;
    LMP_TRY(chip_.write(kCommand, list + at, n));
  }
  return lmp::Status::Ok;
}

}  // namespace meteowifi
