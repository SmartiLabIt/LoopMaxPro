// LoopMaxPro HAL - memory for the large buffers on the ESP8266.
// With the second heap of the core (16 KB cache + 48 KB IRAM, boards/esp8266.ini) the large
// buffers go to the IRAM the code leaves free, and the main heap keeps its memory for the
// network. Reading and writing the IRAM one byte at a time works, through an exception
// handler of the core: slower, fine for buffers of text. Without it, the main heap.
#include "hal/api/memory.h"

#if defined(LMP_MCU_ESP8266)

#include <stdlib.h>

#if defined(MMU_IRAM_HEAP)
#include <umm_malloc/umm_heap_select.h>
#endif

namespace lmp::hal {

void* allocLarge(size_t bytes) {
  if (!bytes) return nullptr;
#if defined(MMU_IRAM_HEAP)
  {
    HeapSelectIram iram;
    void* p = calloc(1, bytes);
    if (p) return p;
  }
#endif
  return calloc(1, bytes);
}

// free() knows which heap a block comes from.
void freeLarge(void* memory) { free(memory); }

ExternalRam externalRam() { return ExternalRam(); }

// No secure connections on this board.
bool roomForSecure() { return true; }

}  // namespace lmp::hal

#endif
