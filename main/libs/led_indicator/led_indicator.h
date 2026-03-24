#pragma once

#include "led_strip.h"
#include "driver/gpio.h"

class LEDIndicator {
public:
    LEDIndicator();
    ~LEDIndicator();
    void initialize(gpio_num_t gpio_num);
    void set_color(uint32_t red, uint32_t green, uint32_t blue);
    void set_wifi_connecting();
    void set_wifi_connected();
    void set_wifi_disconnected();
    void set_sd_detected();
    void flash_sd_write();
    void flash_success(bool connected);
    void flash_error(bool connected);

private:
    led_strip_handle_t led_strip;
    bool is_initialized = false;
    bool wifi_connected = false;
};
