#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "../telemetry_data/telemetry_data.h"
#include "mqtt_client.h" // Use the public header for the MQTT component
#include <cstddef>
#include <cstdint>

class MQTTClient {
private:
    static constexpr int MQTT_QOS = 1;
    static constexpr int MQTT_RETAIN = 0;

    struct PublishDiagnostics {
        uint32_t publish_attempts = 0;
        uint32_t publish_success = 0;
        uint32_t publish_fail = 0;
        uint32_t consecutive_failures = 0;
        uint32_t published_events = 0;
        int last_result = 0;
        size_t last_payload_bytes = 0;
        int64_t last_json_build_ms = 0;
        int64_t last_enqueue_ms = 0;
        uint32_t connected_events = 0;
        uint32_t disconnected_events = 0;
        uint32_t error_events = 0;
        int64_t last_event_ms = 0;
    };

    esp_mqtt_client_handle_t client;
    EventGroupHandle_t event_group;
    static constexpr int CONNECTED_BIT = BIT0;
    PublishDiagnostics diagnostics;
     
    std::string generateClientId();
    static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
     
public:
    MQTTClient();
    ~MQTTClient();
     
    bool initialize();
    bool waitForConnection(uint32_t timeout_ms = 30000);
    bool isConnected() const;
    int outboxSize() const;
    bool publish(const TelemetryData& telemetry, int* out_result = nullptr, size_t* out_payload_bytes = nullptr);
};
