// MeteoWifi - the 0.96" screen of the board: SSD1306, 128x64, on the I2C bus of the system.
// The system gives the bus; the screen belongs to the module that uses it. The image
// is drawn in memory and sent whole: 1 KB, taken only when the screen answers.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "art.h"
#include "base/status.h"
#include "sdk/devices.h"

namespace meteowifi {

class Oled {
 public:
  static constexpr uint8_t kWidth = 128;
  static constexpr uint8_t kHeight = 64;

  // NotFound when nothing answers at the address; NoMem without room for the image.
  lmp::Status begin(lmp::hal::II2c* bus, uint8_t address);
  void end();
  bool ready() const { return image_ != nullptr && chip_.present(); }

  void clear();
  void pixel(int x, int y, bool on = true);
  void draw(const Picture& picture, int x, int y);
  // Text in one of the fonts of art.h; 'o' is the degree sign. Returns the width drawn.
  int text(const Picture* font, const char* chars, int x, int y, const char* s, int gap);
  static int textWidth(const Picture* font, const char* chars, const char* s, int gap);
  // Sends the image to the screen.
  lmp::Status show();
  lmp::Status power(bool on);

 private:
  lmp::Status commands(const uint8_t* list, size_t len);

  lmp::sdk::I2cDevice chip_{0x3C};
  uint8_t* image_ = nullptr;   // one bit for each pixel, in the pages of the chip
};

}  // namespace meteowifi
