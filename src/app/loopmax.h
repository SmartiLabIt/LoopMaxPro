// LoopMaxPro - composition of the system: the kernel and the services of this build.
// This is the only place that knows the concrete services.
#pragma once
#include "kernel/kernel.h"
#include "kernel/service/product.h"
#include "services/clock/clock_service.h"
#include "services/config/config_service.h"
#include "services/gpio/gpio_service.h"
#include "services/log/log_service.h"
#include "services/reset/reset_service.h"
#include "services/serial/serial_service.h"
#include "services/storage/storage_service.h"
#include "services/system/system_service.h"
#if LMP_FEATURE_BUS
#include "services/bus/bus_service.h"
#endif
#if LMP_FEATURE_LORA
#include "services/lora/lora_service.h"
#endif
#if LMP_FEATURE_NET
#include "services/net/net_service.h"
#include "services/web/web_service.h"
#endif
#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif
#if LMP_FEATURE_PROBE
#include "services/probe/probe_service.h"
#endif
#if LMP_FEATURE_MQTT
#include "services/mqtt/mqtt_service.h"
#endif
#if LMP_FEATURE_OTA
#include "services/ota/ota_service.h"
#endif
#if LMP_FEATURE_GEO
#include "services/geo/geo_service.h"
#endif
#if LMP_FEATURE_CLOUD
#include "services/cloud/cloud_service.h"
#endif
#if LMP_FEATURE_MODULES
#include "services/modules/modules_service.h"
#endif

namespace lmp {

class LoopMax {
 public:
  explicit LoopMax(const hal::Board& board);

  // The service of the product, in a firmware that has one (kernel/service/product.h): given
  // before begin(), it starts before the network and the modules.
  void setProduct(Service& product);
  Service* product() { return product_; }

  // Registers the services and prepares the kernel. Logs the startup banner.
  Status begin();

  Kernel& kernel() { return kernel_; }
  SerialService& serial() { return serial_; }
  ClockService& clock() { return clock_; }
#if LMP_FEATURE_BUS
  BusService& bus() { return bus_; }
#endif
#if LMP_FEATURE_LORA
  LoraService& lora() { return lora_; }
#endif
#if LMP_FEATURE_NET
  NetService& net() { return net_; }
  WebService& web() { return web_; }
#endif
#if LMP_FEATURE_HTTPCLIENT
  HttpClientService& httpClient() { return httpClient_; }
#endif
#if LMP_FEATURE_PROBE
  ProbeService& probe() { return probe_; }
#endif
#if LMP_FEATURE_MQTT
  MqttService& mqtt() { return mqtt_; }
#endif
#if LMP_FEATURE_OTA
  OtaService& ota() { return ota_; }
#endif
#if LMP_FEATURE_GEO
  GeoService& geo() { return geo_; }
#endif
#if LMP_FEATURE_CLOUD
  CloudService& cloud() { return cloud_; }
#endif
#if LMP_FEATURE_MODULES
  ModulesService& modules() { return modules_; }
#endif

 private:
  Kernel kernel_;
  SystemService system_;
  LogService log_;
  SerialService serial_;
  StorageService storage_;
  ConfigService config_;
  GpioService gpio_;
  ClockService clock_;
  ResetService reset_;
#if LMP_FEATURE_BUS
  BusService bus_;
#endif
#if LMP_FEATURE_LORA
  LoraService lora_;
#endif
  Service* product_ = nullptr;
#if LMP_FEATURE_NET
  NetService net_;
  WebService web_;
#endif
#if LMP_FEATURE_HTTPCLIENT
  HttpClientService httpClient_;
#endif
#if LMP_FEATURE_PROBE
  ProbeService probe_;
#endif
#if LMP_FEATURE_MQTT
  MqttService mqtt_;
#endif
#if LMP_FEATURE_OTA
  OtaService ota_;
#endif
#if LMP_FEATURE_GEO
  GeoService geo_;
#endif
#if LMP_FEATURE_CLOUD
  CloudService cloud_;
#endif
#if LMP_FEATURE_MODULES
  ModulesService modules_;
#endif
  bool bootLogged_ = false;
};

}  // namespace lmp
