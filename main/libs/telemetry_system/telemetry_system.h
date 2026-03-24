#pragma once

#include "../wifi_manager/wifi_manager.h"
#include "../mqtt_client/my_mqtt_client.h"
#include "../adc_reader/adc_reader.h"
#include "../mpu6050/mpu6050.h"
#include "../hall_sensor/hall_sensor.h"
#include "../led_indicator/led_indicator.h"
#include "../ads1115/ads1115.h"
#include "../sd_card/sd_card.h"

class TelemetrySystem {
private:
    WiFiManager wifi_manager;
    MQTTClient mqtt_client;
    ADCReader adc_reader;
    MPU6050 mpu;
    MPU6050 steering_mpu;
    ADS1115 external_adc;
    HallSensor hall_sensor;
    LEDIndicator led_indicator;
    SDCard sd_card;
    i2c_master_bus_handle_t i2c_bus = NULL;

    float cumulative_energy = 0.0f;
    float cumulative_distance = 0.0f;
    float vehicle_heading = 0.0f;
    int message_count = 0;
    int64_t start_time_us;
    int64_t last_tick_time = 0;
    
    // Task synchronization
    QueueHandle_t telemetry_queue = nullptr;
    static constexpr size_t QUEUE_SIZE = 100;
    
public:
    TelemetrySystem();
    void run(); // Orchestrator
    static void sensor_task_entry(void* arg);
    static void sink_task_entry(void* arg);
    
    void sensor_loop();
    void sink_loop();
};
