#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "hal/adc_types.h"

//== CONFIGURATION =================================================================
// WiFi, MQTT, and application configuration
namespace TelemetryConfig {
    extern const char* WIFI_SSID;
    extern const char* WIFI_PASSWORD;
    extern const char* ABLY_API_KEY;
    extern const char* ABLY_CLIENT_ID_PREFIX;
    extern const char* ABLY_CHANNEL;
    extern const uint32_t PUBLISH_INTERVAL;
    
    // Ably MQTT configuration - SSL version
    extern const char* MQTT_BROKER_HOST;
    extern const int MQTT_BROKER_PORT;
    extern const char* MQTT_USERNAME;
    extern const char* MQTT_PASSWORD;
}

// Hardware configuration
namespace HardwareConfig {
    // MPU6050 I2C configuration
    extern const uint8_t MPU6050_ADDR;
    extern const uint8_t MPU6050_STEERING_ADDR;
    extern const uint8_t ADS1115_ADDR;
    extern const gpio_num_t I2C_MASTER_SCL_IO;
    extern const gpio_num_t I2C_MASTER_SDA_IO;
    extern const uint32_t I2C_MASTER_FREQ_HZ;
    
    // GPS UART configuration
    extern const uart_port_t GPS_UART_NUM;
    extern const int GPS_UART_BAUD_RATE;
    extern const gpio_num_t GPS_UART_RX_PIN;
    extern const gpio_num_t GPS_UART_TX_PIN;

    // Hall Speed Sensor
    extern const gpio_num_t HALL_SENSOR_PIN;
    extern const float WHEEL_CIRCUMFERENCE_M;

    // LED Indicator
    extern const gpio_num_t RGB_LED_PIN;

    // SD Card SPI configuration
    extern const gpio_num_t SD_SPI_MOSI;
    extern const gpio_num_t SD_SPI_MISO;
    extern const gpio_num_t SD_SPI_SCLK;
    extern const gpio_num_t SD_SPI_CS;
    extern const gpio_num_t SD_CARD_DETECT;

    // TWAI (CAN) configuration
    extern const gpio_num_t TWAI_TX_PIN;
    extern const gpio_num_t TWAI_RX_PIN;

    // Extra CS
    extern const gpio_num_t EXTRA_CS_PIN;

    // ADC Channels
    extern const adc_channel_t VOLTAGE_ADC_CHANNEL;
    extern const adc_channel_t CURRENT_ADC_CHANNEL;
}