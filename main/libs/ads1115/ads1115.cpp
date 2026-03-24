#include "ads1115.h"
#include "../config/config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ADS1115";

ADS1115::ADS1115() : dev_handle(NULL), is_initialized(false) {}

ADS1115::~ADS1115() {
    if (dev_handle) i2c_master_bus_rm_device(dev_handle);
}

esp_err_t ADS1115::initialize(i2c_master_bus_handle_t bus_handle, uint8_t dev_addr) {
    if (is_initialized) return ESP_OK;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = dev_addr;
    dev_cfg.scl_speed_hz = HardwareConfig::I2C_MASTER_FREQ_HZ;
    
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    is_initialized = true;
    ESP_LOGI(TAG, "ADS1115 (0x%02X) linked successfully.", dev_addr);
    return ESP_OK;
}

uint16_t ADS1115::read_channel(uint8_t channel) {
    if (!is_initialized || !dev_handle) return 0;

    uint16_t config = 0x8183; 
    config |= (uint16_t)(0x04 | (channel & 0x03)) << 12; 
    
    uint8_t config_buf[3] = { CONFIG_REG, (uint8_t)(config >> 8), (uint8_t)(config & 0xFF) };
    if (i2c_master_transmit(dev_handle, config_buf, 3, pdMS_TO_TICKS(100)) != ESP_OK) {
        return 0;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t reg = CONVERSION_REG;
    uint8_t data[2] = {0, 0};
    if (i2c_master_transmit_receive(dev_handle, &reg, 1, data, 2, pdMS_TO_TICKS(100)) != ESP_OK) {
        return 0;
    }

    return (uint16_t)((data[0] << 8) | data[1]);
}

float ADS1115::read_voltage(uint8_t channel) {
    uint16_t raw = read_channel(channel);
    int16_t res = (int16_t)raw;
    if (res < 0) res = 0; 
    return (float)res * 4.096f / 32768.0f;
}
