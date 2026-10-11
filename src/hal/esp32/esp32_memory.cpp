// LoopMaxPro HAL - memory for the large buffers, ESP32 family: Arduino and ESP-IDF.
#include "hal/api/memory.h"

#include <stdlib.h>

#include "esp_heap_caps.h"

#include "lmp_config.h"

#if LMP_HEAP_MAP
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#endif

namespace lmp::hal {

void* allocLarge(size_t bytes) {
  if (!bytes) return nullptr;
  // Only when the PSRAM was found and joined to the heap at start.
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void freeLarge(void* memory) { heap_caps_free(memory); }

ExternalRam externalRam() {
  ExternalRam ram;
  ram.total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  ram.free = ram.total ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
  return ram;
}

#if LMP_HEAP_MAP
namespace {

constexpr int kTop = 36;
constexpr uint32_t kData = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
struct Map {
  size_t top[kTop];     // the largest blocks in use, the largest first
  size_t used;
  uint32_t blocks;
  uint32_t small;       // blocks in use under 256 bytes, and what they hold together
  size_t smallBytes;
};
Map g_map;

// With the lock of the heap held: nothing is asked or written here.
bool onBlock(walker_heap_into_t, walker_block_info_t block, void* user) {
  Map& m = *static_cast<Map*>(user);
  if (!block.used) return true;
  m.used += block.size;
  ++m.blocks;
  if (block.size < 256) {
    ++m.small;
    m.smallBytes += block.size;
  }
  for (int i = 0; i < kTop; ++i) {
    if (block.size <= m.top[i]) continue;
    memmove(&m.top[i + 1], &m.top[i], sizeof(size_t) * (kTop - 1 - i));
    m.top[i] = block.size;
    break;
  }
  return true;
}

void heapMap(const char* when) {
  memset(&g_map, 0, sizeof(g_map));
  heap_caps_walk(kData, &onBlock, &g_map);
  ESP_LOGW("heapmap", "%s: %u blocks in use, %u bytes, free %u; %u under 256 bytes hold %u", when,
           static_cast<unsigned>(g_map.blocks), static_cast<unsigned>(g_map.used),
           static_cast<unsigned>(heap_caps_get_free_size(kData)), static_cast<unsigned>(g_map.small),
           static_cast<unsigned>(g_map.smallBytes));
  for (int i = 0; i < kTop; i += 12) {
    char line[160];
    int at = 0;
    for (int j = i; j < i + 12 && j < kTop; ++j) {
      at += snprintf(line + at, sizeof(line) - at, " %u", static_cast<unsigned>(g_map.top[j]));
    }
    ESP_LOGW("heapmap", " %s", line);
  }
}

void heapMapTick(void*) {
  static bool rest = false;
  static bool low = false;
  if (!rest && esp_timer_get_time() > 180ll * 1000000) {
    rest = true;
    heapMap("three minutes after the start");
  }
  if (rest && !low && heap_caps_get_free_size(kData) < 18000) {
    low = true;
    heapMap("memory low");
  }
}

}  // namespace
#endif

void heapMapBegin() {
#if LMP_HEAP_MAP
  static bool begun = false;
  if (begun) return;
  begun = true;
  esp_timer_create_args_t args = {};
  args.callback = &heapMapTick;
  args.name = "lmp_heapmap";
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_periodic(timer, 20000);
#endif
}

bool roomForSecure() {
#if LMP_SECURE_MIN_FREE
  // With external RAM the buffers of a secure connection go there.
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) return true;
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= static_cast<size_t>(LMP_SECURE_MIN_FREE);
#else
  return true;
#endif
}

}  // namespace lmp::hal
