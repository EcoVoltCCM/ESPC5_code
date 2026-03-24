#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

class ADS1115 {
public:
    ADS1115();
    ~ADS1115();
    esp_err_t initialize(i2c_master_bus_handle_t bus_handle, uint8_t dev_addr);
    uint16_t read_channel(uint8_t channel);
    float read_voltage(uint8_t channel);
    bool is_ready() const { return is_initialized; }

private:
    i2c_master_dev_handle_t dev_handle = NULL;
    bool is_initialized = false;
    
    static constexpr uint8_t CONVERSION_REG = 0x00;
    static constexpr uint8_t CONFIG_REG     = 0x01;
};
