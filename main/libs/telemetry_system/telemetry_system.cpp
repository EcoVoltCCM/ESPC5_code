#include "telemetry_system.h"
#include "../config/config.h"
#include "../data_types/data_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include <sys/time.h>
#include <sstream>
#include <iomanip>
#include <cmath>

static const char *TAG = "TELEMETRY_SYSTEM";

void initialize_sntp() {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

void getISOTimestamp(char* dest, size_t max_len) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec;
    struct tm* ptm = gmtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", ptm);
    snprintf(dest, max_len, "%s.%03ldZ", buf, tv.tv_usec / 1000);
}

TelemetrySystem::TelemetrySystem() {
    telemetry_queue = xQueueCreate(QUEUE_SIZE, sizeof(TelemetryData));
}

void TelemetrySystem::sensor_task_entry(void* arg) {
    static_cast<TelemetrySystem*>(arg)->sensor_loop();
    vTaskDelete(NULL);
}

void TelemetrySystem::sink_task_entry(void* arg) {
    static_cast<TelemetrySystem*>(arg)->sink_loop();
    vTaskDelete(NULL);
}

void TelemetrySystem::run() {
    // 1. Initialize Hardware (I2C, LED, WiFi)
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = I2C_NUM_0;
    i2c_mst_config.scl_io_num = HardwareConfig::I2C_MASTER_SCL_IO;
    i2c_mst_config.sda_io_num = HardwareConfig::I2C_MASTER_SDA_IO;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus));

    led_indicator.initialize(HardwareConfig::RGB_LED_PIN);
    wifi_manager.set_led_indicator(&led_indicator);
    wifi_manager.initialize();
    
    // 2. Initialize Sensors
    vTaskDelay(pdMS_TO_TICKS(100));
    mpu.initialize(i2c_bus, HardwareConfig::MPU6050_ADDR);
    vTaskDelay(pdMS_TO_TICKS(100));
    steering_mpu.initialize(i2c_bus, HardwareConfig::MPU6050_STEERING_ADDR);
    vTaskDelay(pdMS_TO_TICKS(100));
    external_adc.initialize(i2c_bus, HardwareConfig::ADS1115_ADDR);

    // 2.5 Wait for WiFi with a 10s timeout before forcing SD start
    ESP_LOGI(TAG, "Waiting for WiFi (10s timeout)...");
    wifi_manager.waitForConnection(10000); 
    
    if (wifi_manager.isConnected()) {
        ESP_LOGI(TAG, "WiFi Connected. Initializing SNTP.");
        initialize_sntp();
    } else {
        ESP_LOGW(TAG, "WiFi not connected after 10s. Proceeding with SD initialization to avoid data loss.");
    }

    hall_sensor.initialize(HardwareConfig::HALL_SENSOR_PIN, HardwareConfig::WHEEL_CIRCUMFERENCE_M);
    
    // 3. Initialize SD Card
    if (sd_card.initialize() == ESP_OK) {
        ESP_LOGI(TAG, "SD Card detected, blue LED ON for 5 seconds.");
        led_indicator.set_sd_detected();
        vTaskDelay(pdMS_TO_TICKS(5000));
        // Reset to wifi status
        if (wifi_manager.isConnected()) led_indicator.set_wifi_connected();
        else led_indicator.set_wifi_disconnected();
    } else {
        ESP_LOGE(TAG, "Failed to initialize SD Card");
    }

    // 4. Initialize MQTT
    mqtt_client.initialize();
    
    ESP_LOGI(TAG, "Hardware ready. Launching tasks...");
    start_time_us = esp_timer_get_time();

    // 5. Launch Tasks
    // Sensor Task (High Priority: 10)
    xTaskCreate(sensor_task_entry, "sensor_task", 8192, this, 10, NULL);
    
    // Sink Task (Medium Priority: 5)
    xTaskCreate(sink_task_entry, "sink_task", 8192, this, 5, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Just keep the orchestrator alive
    }
}

void TelemetrySystem::sensor_loop() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(TelemetryConfig::PUBLISH_INTERVAL);

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        TelemetryData telemetry;
        
        // Read Internal ADC (Voltage/Current) - 200 samples, energy integration, and peak tracking
        float avg_voltage, avg_current, max_current, max_power, avg_power;
        adc_reader.read_processed_data(avg_voltage, avg_current, max_current, max_power, avg_power, cumulative_energy);
        
        // Read Hall Sensor
        float speed_kmh = hall_sensor.read_speed_kmh();
        float speed_ms = speed_kmh / 3.6f;
        
        float time_delta_s = TelemetryConfig::PUBLISH_INTERVAL / 1000.0f;
        cumulative_distance += speed_ms * time_delta_s;

        // Read IMUs
        g_sensor_data.mpu_valid = (mpu.read_data(&g_sensor_data.mpu_data) == ESP_OK);
        g_sensor_data.steering_mpu_valid = (steering_mpu.read_data(&g_sensor_data.steering_mpu_data) == ESP_OK);

        if (g_sensor_data.mpu_valid) {
            vehicle_heading += g_sensor_data.mpu_data.gyro_z * time_delta_s;
        }
        
        float total_acceleration = 0.0f;
        if (g_sensor_data.mpu_valid) {
            const auto& a = g_sensor_data.mpu_data;
            total_acceleration = std::sqrt(a.accel_x*a.accel_x + a.accel_y*a.accel_y + a.accel_z*a.accel_z);
        }

        // External ADC (Throttle/Brakes)
        if (external_adc.is_ready()) {
            float t_v = external_adc.read_voltage(0);
            float b1_v = external_adc.read_voltage(1);
            float b2_v = external_adc.read_voltage(2);
            
            auto map_to_pct = [](float v) {
                float pct = (v - 0.54f) / (1.72f - 0.54f) * 100.0f;
                return (pct < 0) ? 0 : (pct > 100 ? 100 : pct);
            };
            
            telemetry.data.throttle_pct = map_to_pct(t_v);
            telemetry.data.brake_pct    = map_to_pct(b1_v);
            telemetry.data.brake2_pct   = map_to_pct(b2_v);
        }

        // Populate Telemetry Object
        message_count++;
        getISOTimestamp(telemetry.data.timestamp, sizeof(telemetry.data.timestamp));
        telemetry.data.message_id = message_count;
        telemetry.data.uptime_seconds = (esp_timer_get_time() - start_time_us) / 1000000.0f;
        telemetry.data.speed_ms = speed_ms;
        telemetry.data.latitude = g_sensor_data.gps_data.latitude;
        telemetry.data.longitude = g_sensor_data.gps_data.longitude;
        telemetry.data.altitude = g_sensor_data.gps_data.altitude;
        telemetry.data.accel_x = g_sensor_data.mpu_data.accel_x;
        telemetry.data.accel_y = g_sensor_data.mpu_data.accel_y;
        telemetry.data.accel_z = g_sensor_data.mpu_data.accel_z;
        telemetry.data.g_long = g_sensor_data.mpu_data.accel_x / 9.81f;
        telemetry.data.g_lat = g_sensor_data.mpu_data.accel_y / 9.81f;
        telemetry.data.gyro_x = g_sensor_data.mpu_data.gyro_x;
        telemetry.data.gyro_y = g_sensor_data.mpu_data.gyro_y;
        telemetry.data.gyro_z = g_sensor_data.mpu_data.gyro_z;
        telemetry.data.steering_accel_x = g_sensor_data.steering_mpu_data.accel_x;
        telemetry.data.steering_accel_y = g_sensor_data.steering_mpu_data.accel_y;
        telemetry.data.steering_accel_z = g_sensor_data.steering_mpu_data.accel_z;
        telemetry.data.steering_gyro_x = g_sensor_data.steering_mpu_data.gyro_x;
        telemetry.data.steering_gyro_y = g_sensor_data.steering_mpu_data.gyro_y;
        telemetry.data.steering_gyro_z = g_sensor_data.steering_mpu_data.gyro_z;
        telemetry.data.total_acceleration = total_acceleration;
        telemetry.data.vehicle_heading = vehicle_heading;
        telemetry.data.voltage_v = avg_voltage;
        telemetry.data.current_a = avg_current;
        telemetry.data.max_current_a = max_current;
        telemetry.data.avg_power_w = avg_power;
        telemetry.data.energy_j = cumulative_energy;
        telemetry.data.distance_m = cumulative_distance;

        // Send to sink task
        if (xQueueSend(telemetry_queue, &telemetry, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Telemetry queue full, dropping record!");
        }
    }
}

void TelemetrySystem::sink_loop() {
    TelemetryData telemetry;
    int records_since_flush = 0;

    while (true) {
        if (xQueueReceive(telemetry_queue, &telemetry, portMAX_DELAY) == pdTRUE) {
            bool wifi_conn = wifi_manager.isConnected();
            UBaseType_t items_waiting = uxQueueMessagesWaiting(telemetry_queue);
            
            // 1. MQTT Publish (Restored to 5Hz - Every record)
            // Skip only if queue is critically full (> 80%) to ensure SD safety
            if (items_waiting < (QUEUE_SIZE * 0.8)) {
                if (mqtt_client.publish(telemetry)) {
                    led_indicator.flash_success(wifi_conn);
                    if (telemetry.data.message_id % 50 == 0) {
                        ESP_LOGI(TAG, "Sent #%d over MQTT (5Hz)", (int)telemetry.data.message_id);
                    }
                } else {
                    led_indicator.flash_error(wifi_conn);
                }
            } else if (telemetry.data.message_id % 10 == 0) {
                ESP_LOGW(TAG, "Critical Congestion! Queue=%d, skipping MQTT to save SD data.", (int)items_waiting);
            }

            // 2. SD Card Write (ALWAYS write to SD, 5Hz)
            if (sd_card.isReady()) {
                if (sd_card.write_telemetry(telemetry, records_since_flush) == ESP_OK) {
                    // led_indicator.flash_sd_write();
                } else {
                    ESP_LOGE(TAG, "SD Card write failed");
                }
            }
        }
    }
}
