#pragma once

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "../telemetry_data/telemetry_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string>

class SDCard {
private:
    sdmmc_card_t *card = nullptr;
    const char *mount_point = "/sdcard";
    bool is_initialized = false;
    char telemetry_path[150];
    char log_path[150];
    uint32_t total_writes = 0;
    
    std::string buffer;
    std::string log_buffer;
    int64_t last_flush_time = 0;
    int64_t last_log_flush_time = 0;
    static constexpr int64_t FLUSH_INTERVAL_MS = 2000;
    static constexpr size_t MAX_BUFFER_SIZE = 16384; // 16KB safety limit

    SemaphoreHandle_t mutex = nullptr;

public:
    SDCard();
    ~SDCard();
    esp_err_t initialize();
    esp_err_t write_telemetry(const TelemetryData& telemetry, int& records_since_flush);
    esp_err_t write_log(const char* text);
    esp_err_t flush(int& records_since_flush);
    esp_err_t flush_logs();
    bool isReady() const { return is_initialized; }
    bool isConnected() const;
};
