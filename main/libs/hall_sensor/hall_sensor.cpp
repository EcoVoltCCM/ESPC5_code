#include "hall_sensor.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "HALL_SENSOR";

HallSensor::HallSensor() 
    : pin(GPIO_NUM_NC), wheel_circumference(0.0f), 
      pulse_count(0), last_pulse_count(0), is_initialized(false) {
    data_mutex = xSemaphoreCreateMutex();
    last_time_us = esp_timer_get_time();
}

void HallSensor::initialize(gpio_num_t gpio_num, float wheel_circumference_m) {
    if (is_initialized) return;

    pin = gpio_num;
    wheel_circumference = wheel_circumference_m;

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE; 
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, gpio_isr_handler, this);
    
    is_initialized = true;
    ESP_LOGI(TAG, "Hall speed sensor initialized on GPIO %d", pin);
}

HallSensor::~HallSensor() {
    if (is_initialized) {
        gpio_isr_handler_remove(pin);
    }
    vSemaphoreDelete(data_mutex);
}

void IRAM_ATTR HallSensor::gpio_isr_handler(void* arg) {
    static_cast<HallSensor*>(arg)->handle_pulse();
}

void IRAM_ATTR HallSensor::handle_pulse() {
    static int64_t last_interrupt_time = 0;
    int64_t current_time = esp_timer_get_time();
    
    // 30ms debounce (max ~115 km/h for a 12" wheel)
    // This effectively filters out high-frequency electrical noise.
    if (current_time - last_interrupt_time > 30000) {
        pulse_count = pulse_count + 1;
        last_interrupt_time = current_time;
    }
}

float HallSensor::read_speed_kmh() {
    if (!is_initialized) return 0.0f;

    static float speed_filter = 0.0f;
    int64_t current_time = esp_timer_get_time();
    
    if (xSemaphoreTake(data_mutex, 0) == pdTRUE) {
        uint32_t current_pulses = pulse_count;
        float dt_s = (current_time - last_time_us) / 1000000.0f;
        
        if (dt_s < 0.1f) { // Require at least 100ms between calculations
            xSemaphoreGive(data_mutex);
            return speed_filter; 
        }

        uint32_t pulses = current_pulses - last_pulse_count;
        last_time_us = current_time;
        last_pulse_count = current_pulses;
        xSemaphoreGive(data_mutex);

        // Speed = (Pulses * Circumference) / Time
        float instant_speed_mps = (pulses * wheel_circumference) / dt_s;
        float instant_speed_kmh = instant_speed_mps * 3.6f;

        // Simple Low-Pass Filter (Alpha = 0.3)
        speed_filter = (instant_speed_kmh * 0.3f) + (speed_filter * 0.7f);
        
        // If no pulses for a while, force to zero
        if (pulses == 0 && dt_s > 0.5f) speed_filter = 0.0f;

        return speed_filter;
    }
    return speed_filter;
}
