#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class HallSensor {
public:
    HallSensor();
    ~HallSensor();
    void initialize(gpio_num_t gpio_num, float wheel_circumference_m);
    float read_speed_kmh();
    uint32_t get_total_pulses() const { return pulse_count; }

private:
    static void IRAM_ATTR gpio_isr_handler(void* arg);
    void handle_pulse();

    gpio_num_t pin;
    float wheel_circumference;
    
    volatile uint32_t pulse_count;
    uint32_t last_pulse_count;
    int64_t last_time_us;
    bool is_initialized = false;
    
    SemaphoreHandle_t data_mutex;
};
