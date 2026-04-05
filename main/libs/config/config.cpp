#include "config.h"

namespace TelemetryConfig {
    const char* WIFI_SSID = "Nospasa";
    const char* WIFI_PASSWORD = "Canada031106";
    const char* ABLY_API_KEY = "ja_fwQ.K6CTEw:F-aWFMdJXPCv9MvxhYztCGna3XdRJZVgA0qm9pMfDOQ";
    const char* ABLY_CLIENT_ID_PREFIX = "esp32_telemetry_";
    const char* ABLY_CHANNEL = "EcoTele";
    const uint32_t PUBLISH_INTERVAL = 200; // Publish every 0.2 seconds
    
    const char* MQTT_BROKER_HOST = "mqtt.ably.io";
    const int MQTT_BROKER_PORT = 8883; // SSL port
    const char* MQTT_USERNAME = ABLY_API_KEY;
    const char* MQTT_PASSWORD = "";
}

namespace HardwareConfig {
    const uint8_t MPU6050_ADDR = 0x69;
    const uint8_t MPU6050_STEERING_ADDR = 0x68;
    const uint8_t ADS1115_ADDR = 0x48;
    const gpio_num_t I2C_MASTER_SCL_IO = GPIO_NUM_5;
    const gpio_num_t I2C_MASTER_SDA_IO = GPIO_NUM_4;
    const uint32_t I2C_MASTER_FREQ_HZ = 50000;
    
    const uart_port_t GPS_UART_NUM = UART_NUM_1;
    const int GPS_UART_BAUD_RATE = 9600;
    const gpio_num_t GPS_UART_RX_PIN = GPIO_NUM_24;
    const gpio_num_t GPS_UART_TX_PIN = GPIO_NUM_23;

    const gpio_num_t HALL_SENSOR_PIN = GPIO_NUM_7;
    const float WHEEL_CIRCUMFERENCE_M = 1.5959f; // 20 inch diameter wheel

    const gpio_num_t RGB_LED_PIN = GPIO_NUM_27;

    const gpio_num_t SD_SPI_MOSI = GPIO_NUM_10;    // Native FSPI
    const gpio_num_t SD_SPI_MISO = GPIO_NUM_9;     // Native FSPI
    const gpio_num_t SD_SPI_SCLK = GPIO_NUM_8;     // Native FSPI
    const gpio_num_t SD_SPI_CS = GPIO_NUM_0;       // Changed from 11 (UART0 TX)
    const gpio_num_t SD_CARD_DETECT = GPIO_NUM_12;

    const gpio_num_t TWAI_TX_PIN = GPIO_NUM_3;
    const gpio_num_t TWAI_RX_PIN = GPIO_NUM_2;

    const gpio_num_t EXTRA_CS_PIN = GPIO_NUM_11;

    const adc_channel_t VOLTAGE_ADC_CHANNEL = ADC_CHANNEL_5; // GPIO 6
    const adc_channel_t CURRENT_ADC_CHANNEL = ADC_CHANNEL_0; // GPIO 1
}
