#include "telemetry_data.h"
#include <cmath>
#include <cstdio>

std::unique_ptr<cJSON, decltype(&cJSON_Delete)> TelemetryData::toJSON() const {
    auto json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>(cJSON_CreateObject(), cJSON_Delete);
    
    cJSON_AddStringToObject(json.get(), "timestamp", data.timestamp);
    cJSON_AddNumberToObject(json.get(), "speed_ms", std::round(data.speed_ms * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "voltage_v", std::round(data.voltage_v * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "current_a", std::round(data.current_a * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "max_current_a", std::round(data.max_current_a * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "avg_power_w", std::round(data.avg_power_w * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "energy_j", std::round(data.energy_j * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "distance_m", std::round(data.distance_m * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "latitude", std::round(data.latitude * 1000000) / 1000000);
    cJSON_AddNumberToObject(json.get(), "longitude", std::round(data.longitude * 1000000) / 1000000);
    cJSON_AddNumberToObject(json.get(), "altitude", std::round(data.altitude * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "gyro_x", std::round(data.gyro_x * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "gyro_y", std::round(data.gyro_y * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "gyro_z", std::round(data.gyro_z * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "accel_x", std::round(data.accel_x * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "accel_y", std::round(data.accel_y * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "accel_z", std::round(data.accel_z * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_gyro_x", std::round(data.steering_gyro_x * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_gyro_y", std::round(data.steering_gyro_y * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_gyro_z", std::round(data.steering_gyro_z * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_accel_x", std::round(data.steering_accel_x * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_accel_y", std::round(data.steering_accel_y * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "steering_accel_z", std::round(data.steering_accel_z * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "vehicle_heading", std::round(std::fmod(data.vehicle_heading, 360.0f) * 100) / 100);
    cJSON_AddNumberToObject(json.get(), "total_acceleration", std::round(data.total_acceleration * 1000) / 1000);
    cJSON_AddNumberToObject(json.get(), "throttle_pct", std::round(data.throttle_pct * 10) / 10);
    cJSON_AddNumberToObject(json.get(), "brake_pct", std::round(data.brake_pct * 10) / 10);
    cJSON_AddNumberToObject(json.get(), "brake2_pct", std::round(data.brake2_pct * 10) / 10);
    cJSON_AddNumberToObject(json.get(), "g_lat", std::round(data.g_lat * 1000) / 1000.0);
    cJSON_AddNumberToObject(json.get(), "g_long", std::round(data.g_long * 1000) / 1000.0);
    cJSON_AddNumberToObject(json.get(), "message_id", data.message_id);
    cJSON_AddNumberToObject(json.get(), "uptime_seconds", std::round(data.uptime_seconds * 100) / 100);
    
    return json;
}

std::string TelemetryData::toCSV() const {
    char row[400];
    snprintf(row, sizeof(row), 
            "%s,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.6f,%.6f,%.2f,"
            "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
            "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
            "%.2f,%.2f,%.1f,%.1f,%.1f,%.3f,%.3f\n",
            data.timestamp, data.message_id, data.uptime_seconds, data.speed_ms, data.voltage_v, data.current_a, data.max_current_a, data.avg_power_w, data.energy_j, data.distance_m,
            data.latitude, data.longitude, data.altitude,
            data.accel_x, data.accel_y, data.accel_z, data.gyro_x, data.gyro_y, data.gyro_z,
            data.steering_accel_x, data.steering_accel_y, data.steering_accel_z, data.steering_gyro_x, data.steering_gyro_y, data.steering_gyro_z,
            data.vehicle_heading, data.total_acceleration, data.throttle_pct, data.brake_pct, data.brake2_pct,
            data.g_lat, data.g_long);
    return std::string(row);
}
