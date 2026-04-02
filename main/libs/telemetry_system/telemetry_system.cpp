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
    last_tick_time = esp_timer_get_time();

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        int64_t sample_time_us = esp_timer_get_time();
        int64_t loop_period_ms = (sample_time_us - last_tick_time) / 1000;
        int64_t loop_jitter_ms = loop_period_ms - (int64_t)TelemetryConfig::PUBLISH_INTERVAL;
        last_tick_time = sample_time_us;

        TelemetryData telemetry;
        telemetry.debug_created_us = sample_time_us;
        
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
            float b2_v = external_adc.read_voltage(1);
            float b1_v = external_adc.read_voltage(2);
            float t_v = external_adc.read_voltage(3);
            
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

        bool bad_numeric =
            !std::isfinite(avg_voltage) ||
            !std::isfinite(avg_current) ||
            !std::isfinite(avg_power) ||
            !std::isfinite(cumulative_energy) ||
            !std::isfinite(cumulative_distance) ||
            !std::isfinite(speed_ms) ||
            !std::isfinite(total_acceleration);

        if (bad_numeric) {
            ESP_LOGW(TAG,
                     "Invalid sensor numeric data: msg=%d V=%.3f I=%.3f P=%.3f E=%.3f D=%.3f speed=%.3f accel=%.3f",
                     (int)telemetry.data.message_id,
                     avg_voltage,
                     avg_current,
                     avg_power,
                     cumulative_energy,
                     cumulative_distance,
                     speed_ms,
                     total_acceleration);
        }

        if (loop_jitter_ms > 30 || loop_jitter_ms < -30) {
            ESP_LOGW(TAG,
                     "Sensor timing jitter: msg=%d period=%lldms expected=%lums jitter=%+lldms queue=%u",
                     (int)telemetry.data.message_id,
                     (long long)loop_period_ms,
                     (unsigned long)TelemetryConfig::PUBLISH_INTERVAL,
                     (long long)loop_jitter_ms,
                     (unsigned)uxQueueMessagesWaiting(telemetry_queue));
        }

        // Send to sink task
        if (xQueueSend(telemetry_queue, &telemetry, 0) != pdTRUE) {
            ESP_LOGW(TAG,
                     "Telemetry drop: queue full msg=%d queue=%u/%u speed=%.2fm/s V=%.2fV I=%.2fA",
                     (int)telemetry.data.message_id,
                     (unsigned)uxQueueMessagesWaiting(telemetry_queue),
                     (unsigned)QUEUE_SIZE,
                     telemetry.data.speed_ms,
                     telemetry.data.voltage_v,
                     telemetry.data.current_a);
        }
    }
}

void TelemetrySystem::sink_loop() {
    TelemetryData telemetry;
    int records_since_flush = 0;
    int64_t high_block_mqtt_ms = 0;
    int64_t high_block_sd_ms = 0;
    int64_t high_e2e_ms = 0;

    while (true) {
        if (xQueueReceive(telemetry_queue, &telemetry, portMAX_DELAY) == pdTRUE) {
            int64_t start_time = esp_timer_get_time();
            bool wifi_conn = wifi_manager.isConnected();
            bool mqtt_conn = mqtt_client.isConnected();
            int mqtt_outbox = mqtt_client.outboxSize();
            UBaseType_t items_waiting = uxQueueMessagesWaiting(telemetry_queue);
            int64_t queue_delay_ms = (telemetry.debug_created_us > 0)
                                       ? ((start_time - telemetry.debug_created_us) / 1000)
                                       : -1;
            int64_t backlog_est_ms = (int64_t)items_waiting * (int64_t)TelemetryConfig::PUBLISH_INTERVAL;

            if (queue_delay_ms > (int64_t)(TelemetryConfig::PUBLISH_INTERVAL * 2) ||
                items_waiting > (QUEUE_SIZE * 0.3)) {
                ESP_LOGW(TAG,
                         "Queue delay detected: msg=%d delay=%lldms queue=%u/%u backlog_est=%lldms wifi=%d mqtt=%d outbox=%d",
                         (int)telemetry.data.message_id,
                         (long long)queue_delay_ms,
                         (unsigned)items_waiting,
                         (unsigned)QUEUE_SIZE,
                         (long long)backlog_est_ms,
                         wifi_conn ? 1 : 0,
                         mqtt_conn ? 1 : 0,
                         mqtt_outbox);
            }
             
            // 1. MQTT Publish
            int64_t mqtt_start = esp_timer_get_time();
            int mqtt_result = -5;
            size_t payload_bytes = 0;
            bool mqtt_sent = false;
            mqtt_sent = mqtt_client.publish(telemetry, &mqtt_result, &payload_bytes);
            if (mqtt_sent) {
                if (items_waiting < 5) {
                    led_indicator.flash_success(wifi_conn);
                }
                if (telemetry.data.message_id % 50 == 0) {
                    ESP_LOGI(TAG, "Sent #%d over MQTT (5Hz)", (int)telemetry.data.message_id);
                }
            } else {
                if (items_waiting < 5) {
                    led_indicator.flash_error(wifi_conn);
                }
                if (items_waiting > (QUEUE_SIZE * 0.7) && (telemetry.data.message_id % 10 == 0)) {
                    ESP_LOGW(TAG,
                             "Critical congestion with publish failure: queue=%u/%u msg=%d queue_delay=%lldms backlog_est=%lldms uptime=%lldms",
                             (unsigned)items_waiting,
                             (unsigned)QUEUE_SIZE,
                             (int)telemetry.data.message_id,
                             (long long)queue_delay_ms,
                             (long long)backlog_est_ms,
                             (long long)(esp_timer_get_time() / 1000));
                }
            }
            int64_t mqtt_end = esp_timer_get_time();

            // 2. SD Card Write
            int64_t sd_start = esp_timer_get_time();
            if (sd_card.isReady()) {
                if (sd_card.write_telemetry(telemetry, records_since_flush) != ESP_OK) {
                    ESP_LOGE(TAG, "SD Card write failed");
                }
            }
            int64_t sd_end = esp_timer_get_time();

            // Performance logging
            int64_t mqtt_dur = (mqtt_end - mqtt_start) / 1000;
            int64_t sd_dur = (sd_end - sd_start) / 1000;
            int64_t total_dur = (esp_timer_get_time() - start_time) / 1000;

            if (mqtt_dur > high_block_mqtt_ms) {
                high_block_mqtt_ms = mqtt_dur;
            }
            if (sd_dur > high_block_sd_ms) {
                high_block_sd_ms = sd_dur;
            }
            if (queue_delay_ms > high_e2e_ms) {
                high_e2e_ms = queue_delay_ms;
            }

            if (total_dur > 50 ||
                (int)items_waiting > (QUEUE_SIZE * 0.5) ||
                !mqtt_sent ||
                queue_delay_ms > 400) {
                ESP_LOGI(TAG,
                         "Sink Perf: msg=%d e2e=%lldms total=%lldms (MQTT=%lldms,res=%d,payload=%uB SD=%lldms) queue=%u/%u backlog_est=%lldms wifi=%d mqtt=%d outbox=%d highs[e2e=%lld,mqtt=%lld,sd=%lld]",
                         (int)telemetry.data.message_id,
                         (long long)queue_delay_ms,
                         (long long)total_dur,
                         (long long)mqtt_dur,
                         mqtt_result,
                         (unsigned)payload_bytes,
                         (long long)sd_dur,
                         (unsigned)items_waiting,
                         (unsigned)QUEUE_SIZE,
                         (long long)backlog_est_ms,
                         wifi_conn ? 1 : 0,
                         mqtt_conn ? 1 : 0,
                         mqtt_outbox,
                         (long long)high_e2e_ms,
                         (long long)high_block_mqtt_ms,
                         (long long)high_block_sd_ms);
            }

            if (!mqtt_conn && (telemetry.data.message_id % 10 == 0)) {
                ESP_LOGW(TAG,
                         "MQTT offline mode: msg=%d queue=%u/%u outbox=%d wifi=%d -> storing to SD only",
                         (int)telemetry.data.message_id,
                         (unsigned)items_waiting,
                         (unsigned)QUEUE_SIZE,
                         mqtt_outbox,
                         wifi_conn ? 1 : 0);
            }

            if (mqtt_dur > (int64_t)TelemetryConfig::PUBLISH_INTERVAL && items_waiting > 0) {
                ESP_LOGW(TAG,
                         "MQTT block over budget: msg=%d mqtt=%lldms budget=%lums queue=%u/%u res=%d payload=%uB outbox=%d",
                         (int)telemetry.data.message_id,
                         (long long)mqtt_dur,
                         (unsigned long)TelemetryConfig::PUBLISH_INTERVAL,
                         (unsigned)items_waiting,
                         (unsigned)QUEUE_SIZE,
                         mqtt_result,
                         (unsigned)payload_bytes,
                         mqtt_outbox);
            }
        }
    }
}
