// LoopMaxPro HAL - memory for the large buffers.
// The buffers taken once at start, such as the log or the queue of the events, go to the
// external RAM (PSRAM) when the board has it: the internal RAM stays for the radio, the
// network and the tasks, which cannot use the external one. Without PSRAM they come from
// the internal heap, as any other allocation.
#pragma once
#include <stddef.h>

namespace lmp::hal {

// Zeroed memory, or null when there is no room. Given back with freeLarge.
void* allocLarge(size_t bytes);
void freeLarge(void* memory);

struct ExternalRam {
  size_t total = 0;   // 0: the board has none, or it is not in use
  size_t free = 0;
};

ExternalRam externalRam();

// True when a new secure connection has the memory it takes to be set up (LMP_SECURE_MIN_FREE,
// lmp_config.h). Always on the boards with external RAM, on the ESP8266 and on the PC.
bool roomForSecure();

// For who develops, on the ESP32 family: the map of the memory on the serial port, when the
// firmware is built with LMP_HEAP_MAP (lmp_config.h). Nothing otherwise.
void heapMapBegin();

}  // namespace lmp::hal
