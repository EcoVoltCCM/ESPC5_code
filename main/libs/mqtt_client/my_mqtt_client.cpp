#include "my_mqtt_client.h"
#include "../config/config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h" // Use the public header for the MQTT component
#include "esp_timer.h"
#include <sstream>
#include <iomanip>
#include <cstring>

static const char *TAG = "MQTT_CLIENT";

std::string MQTTClient::generateClientId() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    std::ostringstream oss;
    oss << TelemetryConfig::ABLY_CLIENT_ID_PREFIX << std::hex << std::setfill('0');
    for (int i = 0; i < 6; ++i) oss << std::setw(2) << static_cast<unsigned>(mac[i]);
    return oss.str();
}

void MQTTClient::mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto* instance = static_cast<MQTTClient*>(handler_args);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    instance->diagnostics.last_event_ms = esp_timer_get_time() / 1000;

    (void)base;

    if (event->event_id == MQTT_EVENT_CONNECTED) {
        instance->diagnostics.connected_events++;
        instance->diagnostics.consecutive_failures = 0;
        ESP_LOGI(TAG, "MQTT connected: reconnects=%u total_errors=%u", 
                 (unsigned)instance->diagnostics.connected_events,
                 (unsigned)instance->diagnostics.error_events);
        xEventGroupSetBits(instance->event_group, CONNECTED_BIT);
    } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        instance->diagnostics.disconnected_events++;
        int outbox = esp_mqtt_client_get_outbox_size(instance->client);
        ESP_LOGW(TAG, "MQTT disconnected: disconnects=%u outbox=%d pending_fail_streak=%u", 
                 (unsigned)instance->diagnostics.disconnected_events, 
                 outbox,
                 (unsigned)instance->diagnostics.consecutive_failures);
        xEventGroupClearBits(instance->event_group, CONNECTED_BIT);
    } else if (event->event_id == MQTT_EVENT_PUBLISHED) {
        instance->diagnostics.published_events++;
        if ((instance->diagnostics.published_events % 100) == 0) {
            int outbox = esp_mqtt_client_get_outbox_size(instance->client);
            ESP_LOGI(TAG,
                     "MQTT ack stats: published=%u outbox=%d",
                     (unsigned)instance->diagnostics.published_events,
                     outbox);
        }
    } else if (event->event_id == MQTT_EVENT_ERROR) {
        instance->diagnostics.error_events++;
        ESP_LOGE(TAG, "MQTT error event: count=%u type=%d", 
                 (unsigned)instance->diagnostics.error_events,
                 event->error_handle ? event->error_handle->error_type : -1);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "Last error: %s", strerror(event->error_handle->esp_transport_sock_errno));
            ESP_LOGE(TAG, "Transport error: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "TLS stack error: 0x%x", event->error_handle->esp_tls_stack_err);
        }
    }
}

MQTTClient::MQTTClient() : client(nullptr) { 
    event_group = xEventGroupCreate(); 
}

MQTTClient::~MQTTClient() { 
    if (client) esp_mqtt_client_destroy(client); 
    vEventGroupDelete(event_group); 
}

bool MQTTClient::initialize() {
    std::string client_id = generateClientId();
    ESP_LOGI(TAG, "Using MQTT client ID: %s", client_id.c_str());
    
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.hostname = TelemetryConfig::MQTT_BROKER_HOST;
    mqtt_cfg.broker.address.port = TelemetryConfig::MQTT_BROKER_PORT;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    mqtt_cfg.credentials.username = TelemetryConfig::MQTT_USERNAME;
    mqtt_cfg.credentials.client_id = client_id.c_str();
    mqtt_cfg.network.disable_auto_reconnect = false;
    mqtt_cfg.network.reconnect_timeout_ms = 2000;
    mqtt_cfg.session.keepalive = 30;
    mqtt_cfg.session.disable_clean_session = false;
    mqtt_cfg.session.message_retransmit_timeout = 3000;
    mqtt_cfg.session.disable_keepalive = false;
    mqtt_cfg.buffer.size = 2048;
    mqtt_cfg.buffer.out_size = 2048;
    mqtt_cfg.outbox.limit = 65536;
    
    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) return false;
    
    esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, (esp_event_handler_t)mqttEventHandler, this);
    return esp_mqtt_client_start(client) == ESP_OK;
}

bool MQTTClient::waitForConnection(uint32_t timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(event_group, CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & CONNECTED_BIT) != 0;
}

bool MQTTClient::isConnected() const {
    return (xEventGroupGetBits(event_group) & CONNECTED_BIT) != 0;
}

int MQTTClient::outboxSize() const {
    if (!client) return -1;
    return esp_mqtt_client_get_outbox_size(client);
}

bool MQTTClient::publish(const TelemetryData& telemetry, int* out_result, size_t* out_payload_bytes) {
    diagnostics.publish_attempts++;
    bool connected_now = isConnected();

    int64_t json_start_us = esp_timer_get_time();
    auto json = telemetry.toJSON();
    int64_t json_ready_us = esp_timer_get_time();
    char* json_string = cJSON_PrintUnformatted(json.get());
    if (!json_string) {
        diagnostics.publish_fail++;
        diagnostics.consecutive_failures++;
        diagnostics.last_result = -3;
        diagnostics.last_payload_bytes = 0;
        diagnostics.last_json_build_ms = (esp_timer_get_time() - json_start_us) / 1000;
        diagnostics.last_enqueue_ms = 0;
        if (out_result) *out_result = -3;
        if (out_payload_bytes) *out_payload_bytes = 0;
        ESP_LOGE(TAG, "JSON build failed: msg=%d json_time=%lldms attempts=%u fail=%u", 
                 telemetry.data.message_id,
                 (long long)diagnostics.last_json_build_ms,
                 (unsigned)diagnostics.publish_attempts,
                 (unsigned)diagnostics.publish_fail);
        return false;
    }

    size_t payload_bytes = strlen(json_string);

    if (payload_bytes > 1300) {
        ESP_LOGW(TAG,
                 "Payload unusually large: msg=%d payload=%uB",
                 telemetry.data.message_id,
                 (unsigned)payload_bytes);
    }

    int64_t publish_start_us = esp_timer_get_time();
    int msg_id = esp_mqtt_client_enqueue(client,
                                         TelemetryConfig::ABLY_CHANNEL,
                                         json_string,
                                         0,
                                         MQTT_QOS,
                                         MQTT_RETAIN,
                                         true);
    int64_t publish_end_us = esp_timer_get_time();

    diagnostics.last_result = msg_id;
    diagnostics.last_payload_bytes = payload_bytes;
    diagnostics.last_json_build_ms = (json_ready_us - json_start_us) / 1000;
    diagnostics.last_enqueue_ms = (publish_end_us - publish_start_us) / 1000;

    if (out_result) *out_result = msg_id;
    if (out_payload_bytes) *out_payload_bytes = payload_bytes;

    if (msg_id >= 0) {
        diagnostics.publish_success++;
        diagnostics.consecutive_failures = 0;

        if ((diagnostics.publish_success % 50) == 0 || diagnostics.last_enqueue_ms > 50 || !connected_now) {
            int outbox = esp_mqtt_client_get_outbox_size(client);
            ESP_LOGI(TAG,
                     "Publish ok: msg=%d mqtt_msg_id=%d payload=%uB json=%lldms enqueue=%lldms outbox=%d connected=%d ok=%u fail=%u",
                     telemetry.data.message_id,
                     msg_id,
                     (unsigned)payload_bytes,
                     (long long)diagnostics.last_json_build_ms,
                     (long long)diagnostics.last_enqueue_ms,
                     outbox,
                     connected_now ? 1 : 0,
                     (unsigned)diagnostics.publish_success,
                     (unsigned)diagnostics.publish_fail);
        }
    } else {
        diagnostics.publish_fail++;
        diagnostics.consecutive_failures++;
        int outbox = esp_mqtt_client_get_outbox_size(client);
        ESP_LOGE(TAG,
                 "MQTT enqueue failed: msg=%d result=%d payload=%uB json=%lldms enqueue=%lldms outbox=%d connected=%d attempts=%u ok=%u fail=%u streak=%u topic=%s",
                 telemetry.data.message_id,
                 msg_id,
                 (unsigned)payload_bytes,
                 (long long)diagnostics.last_json_build_ms,
                 (long long)diagnostics.last_enqueue_ms,
                 outbox,
                 connected_now ? 1 : 0,
                 (unsigned)diagnostics.publish_attempts,
                 (unsigned)diagnostics.publish_success,
                 (unsigned)diagnostics.publish_fail,
                 (unsigned)diagnostics.consecutive_failures,
                 TelemetryConfig::ABLY_CHANNEL);
    }

    free(json_string);
    return msg_id >= 0;
}
