#include "app/loopmax.h"

#include "kernel/service/product.h"
#include "lmp_version.h"

namespace lmp {

LoopMax::LoopMax(const hal::Board& board) : kernel_(board) {}

void LoopMax::setProduct(Service& product) {
  product_ = &product;
  // The event that tells its changes has the name it gives.
  if (const IProduct* asked = static_cast<IProduct*>(product.api())) {
    setProductEventName(asked->reason());
  }
#if LMP_FEATURE_MODULES
  modules_.afterProduct();
#endif
}

Status LoopMax::begin() {
  const hal::Board& board = kernel_.board();
  if (board.sys) {
    const hal::SystemInfo& chip = board.sys->info();
    LMP_LOGI(kLogSys, "LoopMaxPro %s (%s) board %s", LMP_VERSION_FULL, LMP_BUILD_DATE, board.name);
    const char* detail = board.sys->resetDetail();
    LMP_LOGI(kLogSys, "chip %s rev %u, %u core, %u MHz, flash %lu KB, heap %lu KB, reset: %s%s%s%s",
             chip.chipModel, static_cast<unsigned>(chip.revision),
             static_cast<unsigned>(chip.cores), static_cast<unsigned>(chip.cpuMhz),
             static_cast<unsigned long>(chip.flashSize / 1024),
             static_cast<unsigned long>(board.sys->heapFree() / 1024),
             hal::resetReasonName(board.sys->resetReason()), *detail ? " (" : "", detail,
             *detail ? ")" : "");
  }
  kernel_.tellPreviousStart();

  LMP_TRY(kernel_.add(system_));
  LMP_TRY(kernel_.add(log_));
  LMP_TRY(kernel_.add(serial_));
  LMP_TRY(kernel_.add(storage_));
  LMP_TRY(kernel_.add(config_));
  LMP_TRY(kernel_.add(gpio_));
  LMP_TRY(kernel_.add(clock_));
  LMP_TRY(kernel_.add(reset_));
#if LMP_FEATURE_BUS
  LMP_TRY(kernel_.add(bus_));
#endif
#if LMP_FEATURE_LORA
  LMP_TRY(kernel_.add(lora_));
#endif
  if (product_) LMP_TRY(kernel_.add(*product_));
#if LMP_FEATURE_NET
  LMP_TRY(kernel_.add(net_));
  LMP_TRY(kernel_.add(web_));
#endif
#if LMP_FEATURE_HTTPCLIENT
  LMP_TRY(kernel_.add(httpClient_));
#endif
#if LMP_FEATURE_PROBE
  LMP_TRY(kernel_.add(probe_));
#endif
#if LMP_FEATURE_MQTT
  LMP_TRY(kernel_.add(mqtt_));
#endif
#if LMP_FEATURE_OTA
  LMP_TRY(kernel_.add(ota_));
#endif
#if LMP_FEATURE_GEO
  LMP_TRY(kernel_.add(geo_));
#endif
#if LMP_FEATURE_CLOUD
  LMP_TRY(kernel_.add(cloud_));
#endif
#if LMP_FEATURE_MODULES
  LMP_TRY(kernel_.add(modules_));
#endif
  return kernel_.begin();
}

}  // namespace lmp
