#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include "cJSON.h"

class TelemetryData {
public:
    struct SensorData {
        float speed_ms = 0.0f;
        float voltage_v = 0.0f;
        float current_a = 0.0f;
        float max_current_a = 0.0f;
        float avg_power_w = 0.0f;
        float energy_j = 0.0f;
        float distance_m = 0.0f;
        double latitude = 0.0;
        double longitude = 0.0;
        float altitude = 0.0f;
        float gyro_x = 0.0f, gyro_y = 0.0f, gyro_z = 0.0f;
        float accel_x = 0.0f, accel_y = 0.0f, accel_z = 0.0f;
        float steering_gyro_x = 0.0f, steering_gyro_y = 0.0f, steering_gyro_z = 0.0f;
        float steering_accel_x = 0.0f, steering_accel_y = 0.0f, steering_accel_z = 0.0f;
        float vehicle_heading = 0.0f;
        float total_acceleration = 0.0f;
        float throttle_pct = 0.0f;  // Throttle position percentage (0-100%)
        float brake_pct = 0.0f;     // Main brake percentage
        float brake2_pct = 0.0f;    // Brake 2 percentage
        float g_lat = 0.0f;         // Lateral acceleration in G
        float g_long = 0.0f;        // Longitudinal acceleration in G
        int message_id = 0;
        float uptime_seconds = 0.0f;
        char timestamp[32];
    };

    SensorData data;
    int64_t debug_created_us = 0;

    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> toJSON() const;
    std::string toCSV() const;
};
