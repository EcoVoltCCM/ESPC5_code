#include "adc_reader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../config/config.h"
#include "driver/gpio.h"
#include <algorithm>

static const char *TAG = "ADC_READER";

// Hardware constants
#define R1      33000.0f // Ohms
#define R2      2200.0f  // Ohms
// #define R1      1000000.0f // 1M Ohm
// #define R2      56000.0f   // 56k Ohm
#define SHUNT_RESISTOR 0.005f // Ohms
#define GAIN    20.0f
#define VREF_V  1.65f
#define NUM_SAMPLES 200

ADCReader::ADCReader() 
    : voltage_channel(HardwareConfig::VOLTAGE_ADC_CHANNEL),
      current_channel(HardwareConfig::CURRENT_ADC_CHANNEL) {
    configure_adc();
    data_mutex = xSemaphoreCreateMutex();
}

ADCReader::~ADCReader() {
    vSemaphoreDelete(data_mutex);
    adc_oneshot_del_unit(adc1_handle);
}

bool ADCReader::do_calibration(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {};
        cali_config.unit_id = unit;
        cali_config.chan = channel;
        cali_config.atten = atten;
        cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {};
        cali_config.unit_id = unit;
        cali_config.atten = atten;
        cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    return calibrated;
}

void ADCReader::configure_adc() {
    // Manually configure GPIOs using constants from config.h
    // (GPIO 6 for Voltage, GPIO 1 for Current)
    gpio_num_t v_pin = GPIO_NUM_6; 
    gpio_num_t i_pin = GPIO_NUM_1;
    
    gpio_reset_pin(v_pin);
    gpio_reset_pin(i_pin);
    gpio_set_pull_mode(v_pin, GPIO_FLOATING);
    gpio_set_pull_mode(i_pin, GPIO_FLOATING);

    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {};
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    config.atten = ADC_ATTEN_DB_12;

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, voltage_channel, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, current_channel, &config));

    do_calibration(ADC_UNIT_1, voltage_channel, ADC_ATTEN_DB_12, &voltage_cali_handle);
    do_calibration(ADC_UNIT_1, current_channel, ADC_ATTEN_DB_12, &current_cali_handle);

    ESP_LOGI(TAG, "ADC configured. Voltage sampled 10x per burst.");
}

void ADCReader::read_processed_data(float& avg_voltage, float& avg_current, float& max_current, float& max_power, float& avg_power, float& energy_joules) {
    float voltage_sum = 0;
    float current_sum = 0;
    float power_sum = 0;
    int voltage_read_count = 0;
    max_current = -1000.0f;
    max_power = -1000.0f;
    
    const float dt_per_sample = 0.2f / NUM_SAMPLES; // 0.001s per sample
    float last_inst_voltage = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw_v, raw_i;
        int mv_v = 0, mv_i = 0;
        
        // 1. Read Current (Every sample - 200x)
        adc_oneshot_read(adc1_handle, current_channel, &raw_i);
        if (current_cali_handle) adc_cali_raw_to_voltage(current_cali_handle, raw_i, &mv_i);
        else mv_i = raw_i * 3300 / 4095;
        float inst_current = ((mv_i / 1000.0f) - VREF_V) / (GAIN * SHUNT_RESISTOR);

        // Calibration phase: first 1000 samples (5 calls * 200 samples)
        if (!is_calibrated) {
            calibration_sum += inst_current;
            calibration_count++;
            
            if (calibration_count >= 1000) {
                zero_offset_a = calibration_sum / 1000.0f;
                is_calibrated = true;
                ESP_LOGI(TAG, "Current calibration complete. Zero offset: %.4f A", zero_offset_a);
            }
            
            // During calibration, report as zero
            inst_current = 0.0f;
        } else {
            // Apply zero calibration offset
            inst_current -= zero_offset_a;
        }

        current_sum += inst_current;

        // 2. Read Voltage (Every 20 samples - 10x total)
        if (i % 20 == 0) {
            adc_oneshot_read(adc1_handle, voltage_channel, &raw_v);
            if (voltage_cali_handle) adc_cali_raw_to_voltage(voltage_cali_handle, raw_v, &mv_v);
            else mv_v = raw_v * 3300 / 4095;
            last_inst_voltage = (mv_v / 1000.0f) * ((R1 + R2) / R2);
            
            voltage_sum += last_inst_voltage;
            voltage_read_count++;
        }
        
        // 3. Calculate Power using current and the most recent voltage
        float inst_power = last_inst_voltage * inst_current;
        power_sum += inst_power;
        
        // 4. Track Peaks and Energy
        if (inst_current > max_current) max_current = inst_current;
        if (inst_power > max_power) max_power = inst_power;
        energy_joules += (inst_power * dt_per_sample);
    }
    
    avg_voltage = (voltage_read_count > 0) ? (voltage_sum / voltage_read_count) : 0;
    avg_current = current_sum / NUM_SAMPLES;
    avg_power = power_sum / NUM_SAMPLES;
}
