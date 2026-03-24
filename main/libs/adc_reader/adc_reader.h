#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class ADCReader {
public:
    ADCReader();
    ~ADCReader();

    /**
     * @brief Reads 200 samples of V and I.
     * @param avg_voltage Output: Average voltage of the 200 samples
     * @param max_current Output: Maximum current observed in the 200 samples
     * @param max_power   Output: Maximum instantaneous power observed
     * @param avg_power   Output: Average power of the 200 samples
     * @param energy_joules Input/Output: Energy accumulator to be updated with integrated power
     */
    void read_processed_data(float& avg_voltage, float& avg_current, float& max_current, float& max_power, float& avg_power, float& energy_joules);

private:
    void configure_adc();
    bool do_calibration(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);

    adc_oneshot_unit_handle_t adc1_handle;
    adc_channel_t voltage_channel;
    adc_channel_t current_channel;

    adc_cali_handle_t voltage_cali_handle = nullptr;
    adc_cali_handle_t current_cali_handle = nullptr;

    float zero_offset_a = 0.0f;
    float calibration_sum = 0.0f;
    int calibration_count = 0;
    bool is_calibrated = false;

    SemaphoreHandle_t data_mutex;
};
