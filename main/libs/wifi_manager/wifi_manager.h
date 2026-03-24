#pragma once

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../led_indicator/led_indicator.h"

class WiFiManager {
private:
    EventGroupHandle_t event_group;
    static constexpr int CONNECTED_BIT = BIT0;
    LEDIndicator* led_indicator = nullptr;
    
    static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    
public:
    WiFiManager();
    ~WiFiManager();
    
    void initialize();
    void waitForConnection(uint32_t timeout_ms = portMAX_DELAY);
    bool isConnected();
    void set_led_indicator(LEDIndicator* led) { led_indicator = led; }
};