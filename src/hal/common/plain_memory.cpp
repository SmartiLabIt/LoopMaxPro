// LoopMaxPro HAL - memory for the large buffers, boards without external RAM: the PC. The
// ESP32 family has its own, in hal/esp32/esp32_memory.cpp, and so does the ESP8266.
#include "hal/api/memory.h"

#if !defined(LMP_MCU_ESP32) && !defined(LMP_MCU_ESP8266)

#include <stdlib.h>

namespace lmp::hal {

void* allocLarge(size_t bytes) { return bytes ? calloc(1, bytes) : nullptr; }

void freeLarge(void* memory) { free(memory); }

ExternalRam externalRam() { return ExternalRam(); }

bool roomForSecure() { return true; }

}  // namespace lmp::hal

#endif
