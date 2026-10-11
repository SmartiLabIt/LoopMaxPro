#include "hal/esp32/esp32_online.h"

#include <esp_idf_version.h>
#include <esp_sntp.h>
#include <string.h>

#if LMP_FEATURE_HTTPCLIENT || LMP_FEATURE_MQTT
// Certificates of the public authorities, part of the framework. Declared here and not
// included: the Arduino core has a library with a header of the same name and other
// functions, and the build would pick that one.
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);
#endif
#if LMP_FEATURE_HTTPCLIENT
#include <esp_http_client.h>
#endif
#if LMP_FEATURE_MQTT
#include <mqtt_client.h>
#endif

namespace lmp::hal::esp32 {

// ---------------------------------------------------------------- time from the network

namespace {

// Written by the task of the network stack, read by the application task.
volatile uint32_t g_answers = 0;

void onTime(struct timeval*) { g_answers = g_answers + 1; }

}  // namespace

uint32_t Ntp::answers() const { return g_answers; }

Status Ntp::start(const char* server) {
  if (!server || !*server) return Status::Invalid;
  if (running_) stop();
  server_.assign(server);
  sntp_set_time_sync_notification_cb(&onTime);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, server_.c_str());
  esp_sntp_init();
#else
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, server_.c_str());
  sntp_init();
#endif
  running_ = true;
  return Status::Ok;
}

void Ntp::stop() {
  if (!running_) return;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  esp_sntp_stop();
#else
  sntp_stop();
#endif
  running_ = false;
}

// ---------------------------------------------------------------- requests to servers

#if LMP_FEATURE_HTTPCLIENT

namespace {

constexpr int kRedirects = 4;
constexpr size_t kPiece = 1024;

esp_http_client_method_t methodOf(HttpMethod method) {
  switch (method) {
    case HttpMethod::Post:    return HTTP_METHOD_POST;
    case HttpMethod::Put:     return HTTP_METHOD_PUT;
    case HttpMethod::Delete:  return HTTP_METHOD_DELETE;
    case HttpMethod::Options: return HTTP_METHOD_OPTIONS;
    case HttpMethod::Get:
    case HttpMethod::Any:     break;
  }
  return HTTP_METHOD_GET;
}

// The headers of the answer, to who asked for them.
esp_err_t onHttpEvent(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->user_data && evt->header_key &&
      evt->header_value) {
    const auto* req = static_cast<const HttpClientRequest*>(evt->user_data);
    if (req->onHeader) req->onHeader(evt->header_key, evt->header_value);
  }
  return ESP_OK;
}

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Why the connection did not open.
Status whyNot(esp_http_client_handle_t client, esp_err_t error) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  int tlsCode = 0;
  int tlsFlags = 0;
  esp_http_client_get_and_clear_last_tls_error(client, &tlsCode, &tlsFlags);
  // Flags are set when the certificate of the server was looked at and not accepted.
  if (tlsFlags != 0) return Status::Denied;
#else
  // The older framework does not tell a certificate that was refused from the rest.
  (void)client;
#endif
  if (error == ESP_ERR_NO_MEM) return Status::NoMem;
  return Status::Io;
}

}  // namespace

Status HttpClient::request(const HttpClientRequest& req, HttpClientResponse& res,
                           HttpChunkHandler onChunk) {
  res = HttpClientResponse();
  if (!req.url || !*req.url) return Status::Invalid;
  // Every server is checked against the certificates of the board, or the one given.
  if (req.tls.insecure) return Status::Unsupported;

  esp_http_client_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = req.url;
  config.method = methodOf(req.method);
  config.timeout_ms = static_cast<int>(req.timeoutMs);
  // The buffers of the client stay at their default, 512 bytes: a secure connection is at the
  // edge of the memory of an ESP32 without PSRAM, and 1 KB more makes its handshake fail.
  config.disable_auto_redirect = true;   // followed here, one step at a time
  config.keep_alive_enable = false;
  if (req.onHeader) {
    config.event_handler = onHttpEvent;
    config.user_data = const_cast<HttpClientRequest*>(&req);
  }
  if (req.tls.caPem) {
    config.cert_pem = req.tls.caPem;
  } else {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return Status::NoMem;
  if (req.contentType) esp_http_client_set_header(client, "Content-Type", req.contentType);
  for (const HttpHeader& h : req.headers) {
    if (h.name && h.value) esp_http_client_set_header(client, h.name, h.value);
  }

  Status result = Status::Ok;
  int status = 0;
  int64_t declared = 0;
  bool open = false;
  for (int hop = 0; hop <= kRedirects; ++hop) {
    const esp_err_t opened = esp_http_client_open(client, static_cast<int>(req.body.size));
    if (opened != ESP_OK) {
      result = whyNot(client, opened);
      break;
    }
    open = true;
    if (req.body.size &&
        esp_http_client_write(client, req.body.data, static_cast<int>(req.body.size)) !=
            static_cast<int>(req.body.size)) {
      result = Status::Io;
      break;
    }
    declared = esp_http_client_fetch_headers(client);
    if (declared < 0) {
      result = Status::Timeout;
      break;
    }
    status = esp_http_client_get_status_code(client);
    if (!isRedirect(status) || hop == kRedirects) break;

    // Somewhere else: what this answer carries is thrown away.
    int thrown = 0;
    esp_http_client_flush_response(client, &thrown);
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
    open = false;
  }

  if (result == Status::Ok) {
    res.status = status;
    res.contentLength = declared > 0 ? static_cast<size_t>(declared) : 0;
    static char piece[kPiece];   // one request at a time: the service asks them in a row
    for (;;) {
      const int n = esp_http_client_read(client, piece, sizeof(piece));
      if (n < 0) {
        result = Status::Io;
        break;
      }
      if (n == 0) {
        // The end, or the server stopped talking before it.
        if (!esp_http_client_is_complete_data_received(client)) result = Status::Timeout;
        break;
      }
      if (onChunk && !onChunk(Span<const uint8_t>(reinterpret_cast<const uint8_t*>(piece),
                                                 static_cast<size_t>(n)))) {
        result = Status::Error;   // who was taking the pieces asked to stop
        break;
      }
    }
  }

  if (open) esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return result;
}

#endif  // LMP_FEATURE_HTTPCLIENT

// ---------------------------------------------------------------- MQTT

#if LMP_FEATURE_MQTT

namespace {

const char* refusal(int code) {
  switch (code) {
    case MQTT_CONNECTION_REFUSE_PROTOCOL:     return "protocol not accepted";
    case MQTT_CONNECTION_REFUSE_ID_REJECTED:  return "name not accepted";
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: return "broker not available";
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME: return "wrong name or password";
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: return "not authorized";
    default:                                  break;
  }
  return "refused";
}

}  // namespace

Status Mqtt::configure(const MqttConfig& cfg) {
  if (!cfg.host || !*cfg.host) return Status::Invalid;
  host_.assign(cfg.host);
  port_ = cfg.port;
  clientId_.assign(cfg.clientId ? cfg.clientId : "");
  username_.assign(cfg.username ? cfg.username : "");
  password_.assign(cfg.password ? cfg.password : "");
  willTopic_.assign(cfg.willTopic ? cfg.willTopic : "");
  willMessage_.assign(cfg.willMessage ? cfg.willMessage : "");
  keepAliveSec_ = cfg.keepAliveSec;
  useTls_ = cfg.useTls;
  caPem_ = cfg.tls.caPem;
  // Every broker is checked against the certificates of the board, or the one given.
  if (cfg.useTls && cfg.tls.insecure) return Status::Unsupported;
  configured_ = true;
  return Status::Ok;
}

Status Mqtt::connect() {
  if (!configured_) return Status::Denied;
  if (client_) disconnect();

  esp_mqtt_client_config_t config;
  memset(&config, 0, sizeof(config));
  const esp_mqtt_transport_t transport =
      useTls_ ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  config.broker.address.hostname = host_.c_str();
  config.broker.address.port = port_;
  config.broker.address.transport = transport;
  if (useTls_) {
    if (caPem_) {
      config.broker.verification.certificate = caPem_;
    } else {
      config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
  }
  if (!clientId_.empty()) config.credentials.client_id = clientId_.c_str();
  if (!username_.empty()) config.credentials.username = username_.c_str();
  if (!password_.empty()) config.credentials.authentication.password = password_.c_str();
  if (!willTopic_.empty()) {
    config.session.last_will.topic = willTopic_.c_str();
    config.session.last_will.msg = willMessage_.c_str();
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;
  }
  config.session.keepalive = keepAliveSec_;
  config.buffer.size = 1024;
  config.buffer.out_size = 2048;
#if LMP_MQTT_PLAIN_STACK
  if (!useTls_) config.task.stack_size = LMP_MQTT_PLAIN_STACK;
#endif
#else
  config.host = host_.c_str();
  config.port = port_;
  config.transport = transport;
  if (useTls_) {
    if (caPem_) {
      config.cert_pem = caPem_;
    } else {
      config.crt_bundle_attach = esp_crt_bundle_attach;
    }
  }
  if (!clientId_.empty()) config.client_id = clientId_.c_str();
  if (!username_.empty()) config.username = username_.c_str();
  if (!password_.empty()) config.password = password_.c_str();
  if (!willTopic_.empty()) {
    config.lwt_topic = willTopic_.c_str();
    config.lwt_msg = willMessage_.c_str();
    config.lwt_qos = 1;
    config.lwt_retain = 1;
  }
  config.keepalive = keepAliveSec_;
  config.buffer_size = 1024;
  config.out_buffer_size = 2048;
#endif

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
  if (!client) return Status::NoMem;
  stopping_ = false;
  esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &Mqtt::onEvent, this);
  if (esp_mqtt_client_start(client) != ESP_OK) {
    esp_mqtt_client_destroy(client);
    return Status::Io;
  }
  client_ = client;
  error_ = "";
  return Status::Ok;
}

void Mqtt::disconnect() {
  if (!client_) return;
  esp_mqtt_client_handle_t client = static_cast<esp_mqtt_client_handle_t>(client_);
  client_ = nullptr;
  connected_ = false;
  stopping_ = true;   // an attempt waiting for memory gives up: the stop waits for its task
  esp_mqtt_client_stop(client);
  esp_mqtt_client_destroy(client);
}

void Mqtt::onEvent(void* self, const char*, int32_t id, void* data) {
  static_cast<Mqtt*>(self)->handle(id, data);
}

// Context of the task of the driver.
void Mqtt::handle(int32_t id, void* data) {
  const esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(data);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_BEFORE_CONNECT:
      // A secure connection takes memory to be set up: with too little of it free the attempt
      // waits here, in the task of the driver, until there is or the client is stopped. Trying
      // anyway would take the memory to zero, and with it the Wi-Fi of a first ESP32.
      if (useTls_ && !roomForSecure()) {
        error_ = "waiting for memory";
        while (!stopping_ && !roomForSecure()) vTaskDelay(pdMS_TO_TICKS(250));
      }
      break;
    case MQTT_EVENT_CONNECTED:
      error_ = "";
      connected_ = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (connected_) error_ = "connection lost";
      connected_ = false;
      break;
    case MQTT_EVENT_ERROR:
      if (event && event->error_handle) {
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
          error_ = refusal(event->error_handle->connect_return_code);
        } else if (event->error_handle->esp_tls_cert_verify_flags != 0) {
          error_ = "certificate not accepted";
        } else {
          error_ = "broker not reached";
        }
      }
      break;
    case MQTT_EVENT_DATA:
      // A message longer than the buffer arrives in pieces: the first one carries the
      // topic, and is enough to tell the message is too long.
      if (event && handler_ && event->current_data_offset == 0 && event->topic) {
        handler_(StringView(event->topic, static_cast<size_t>(event->topic_len)),
                 StringView(event->data, static_cast<size_t>(event->data_len)));
      }
      break;
    default:
      break;
  }
}

Status Mqtt::publish(const char* topic, StringView payload, uint8_t qos, bool retain) {
  if (!topic || !*topic) return Status::Invalid;
  if (!client_ || !connected_) return Status::Denied;
  // Queued: it leaves from the task of the driver, who asks never waits for the network.
  const int id = esp_mqtt_client_enqueue(static_cast<esp_mqtt_client_handle_t>(client_), topic,
                                         payload.data ? payload.data : "",
                                         static_cast<int>(payload.size), qos, retain ? 1 : 0,
                                         true);
  return id < 0 ? Status::NoMem : Status::Ok;
}

Status Mqtt::subscribe(const char* topic, uint8_t qos) {
  if (!topic || !*topic) return Status::Invalid;
  if (!client_ || !connected_) return Status::Denied;
  const int id =
      esp_mqtt_client_subscribe(static_cast<esp_mqtt_client_handle_t>(client_), topic, qos);
  return id < 0 ? Status::Io : Status::Ok;
}

#endif  // LMP_FEATURE_MQTT

}  // namespace lmp::hal::esp32
