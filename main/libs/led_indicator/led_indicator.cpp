#include "led_indicator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED_INDICATOR";

LEDIndicator::LEDIndicator() : led_strip(nullptr), is_initialized(false), wifi_connected(false) {}

LEDIndicator::~LEDIndicator() {
    if (is_initialized) {
        led_strip_del(led_strip);
    }
}

void LEDIndicator::initialize(gpio_num_t gpio_num) {
    if (is_initialized) return;

    /* LED strip initialization with the RMT backend */
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio_num;
    strip_config.max_leds = 1; 
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz
    rmt_config.flags.with_dma = false;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    led_strip_clear(led_strip);
    is_initialized = true;
    ESP_LOGI(TAG, "LED Indicator initialized on GPIO %d", gpio_num);
}

void LEDIndicator::set_color(uint32_t red, uint32_t green, uint32_t blue) {
    if (!is_initialized) return;
    // Reduce intensity to half
    led_strip_set_pixel(led_strip, 0, red / 2, green / 2, blue / 2);
    led_strip_refresh(led_strip);
}

void LEDIndicator::set_wifi_connecting() {
    // Disabled blue for connecting as requested - turning off instead
    set_color(0, 0, 0);
}

void LEDIndicator::set_wifi_connected() {
    wifi_connected = true;
    // Half of previous 64
    set_color(0, 32, 0);
}

void LEDIndicator::set_wifi_disconnected() {
    wifi_connected = false;
    // Half of previous 64
    set_color(32, 0, 0);
}

void LEDIndicator::set_sd_detected() {
    set_color(0, 0, 128); // Half of 255 (approx)
}

void LEDIndicator::flash_sd_write() {
    set_color(0, 0, 128); // Half of 255 (approx)
    vTaskDelay(pdMS_TO_TICKS(5));
    // Restore WiFi status color
    if (wifi_connected) set_wifi_connected();
    else set_wifi_disconnected();
}

void LEDIndicator::flash_success(bool connected) {
    set_color(0, 64, 0); // Half of 128
    vTaskDelay(pdMS_TO_TICKS(10));
    if (connected) set_wifi_connected();
    else set_wifi_disconnected();
}

void LEDIndicator::flash_error(bool connected) {
    set_color(64, 0, 0); // Half of 128
    vTaskDelay(pdMS_TO_TICKS(10));
    if (connected) set_wifi_connected();
    else set_wifi_disconnected();
}
