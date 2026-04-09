#include "sd_card.h"
#include "../config/config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <sys/stat.h>
#include <string>

static const char *TAG = "SD_CARD";

SDCard::SDCard() {
    mutex = xSemaphoreCreateMutex();
}

SDCard::~SDCard() {
    if (mutex) vSemaphoreDelete(mutex);
}

esp_err_t SDCard::initialize() {
    esp_err_t ret;
    last_flush_time = esp_timer_get_time() / 1000; // ms
    last_log_flush_time = last_flush_time;

    // Configure Card Detect Pin
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << HardwareConfig::SD_CARD_DETECT);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = true;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;
    mount_config.disk_status_check_enable = false;

    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = HardwareConfig::SD_SPI_MOSI;
    bus_cfg.miso_io_num = HardwareConfig::SD_SPI_MISO;
    bus_cfg.sclk_io_num = HardwareConfig::SD_SPI_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;
    bus_cfg.data_io_default_level = 0;
    bus_cfg.flags = 0;
    bus_cfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    bus_cfg.intr_flags = 0;

    ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = HardwareConfig::SD_SPI_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_FATFS_LFN_STACK option in menuconfig.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);

    // Find a unique filename (data_1.csv, data_1.txt)
    int file_idx = 1;
    struct stat st;
    char base_path[128];
    while (file_idx < 1000) {
        snprintf(base_path, sizeof(base_path), "%s/data_%d", mount_point, file_idx);
        snprintf(telemetry_path, sizeof(telemetry_path), "%s.csv", base_path);
        if (stat(telemetry_path, &st) != 0) {
            // File does not exist, use this one
            snprintf(log_path, sizeof(log_path), "%s.txt", base_path);
            break;
        }
        file_idx++;
    }

    ESP_LOGI(TAG, "Using telemetry: %s and log: %s", telemetry_path, log_path);

    // Write header to the new file
    FILE* f = fopen(telemetry_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing header", telemetry_path);
        return ESP_FAIL;
    }
    fprintf(f, "timestamp,msg_id,uptime,speed_ms,voltage,current,max_current,avg_power,energy,distance,lat,lon,alt,"
               "accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,"
               "s_accel_x,s_accel_y,s_accel_z,s_gyro_x,s_gyro_y,s_gyro_z,"
               "heading,total_accel,throttle_pct,brake_pct,brake2_pct,"
               "g_lat,g_long\n");
    fclose(f);

    // Create log file
    f = fopen(log_path, "w");
    if (f) {
        fprintf(f, "--- START OF LOG ---\n");
        fclose(f);
    }

    is_initialized = true;
    return ESP_OK;
}

esp_err_t SDCard::write_telemetry(const TelemetryData& telemetry, int& records_since_flush) {
    if (!is_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(mutex, portMAX_DELAY);
    buffer.append(telemetry.toCSV());
    total_writes++;
    records_since_flush++;

    // Flush if interval reached or buffer too large
    int64_t now = esp_timer_get_time() / 1000;
    if (now - last_flush_time >= FLUSH_INTERVAL_MS || buffer.size() >= MAX_BUFFER_SIZE) {
        esp_err_t res = flush(records_since_flush);
        xSemaphoreGive(mutex);
        return res;
    }

    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t SDCard::write_log(const char* text) {
    if (!is_initialized || !mutex) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    log_buffer.append(text);
    
    int64_t now = esp_timer_get_time() / 1000;
    if (now - last_log_flush_time >= FLUSH_INTERVAL_MS || log_buffer.size() >= MAX_BUFFER_SIZE) {
        esp_err_t res = flush_logs();
        xSemaphoreGive(mutex);
        return res;
    }
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t SDCard::flush(int& records_since_flush) {
    if (buffer.empty()) return ESP_OK;

    FILE* f = fopen(telemetry_path, "a");
    if (f == NULL) {
        return ESP_FAIL;
    }

    size_t written = fwrite(buffer.c_str(), 1, buffer.size(), f);
    fclose(f);

    if (written != buffer.size()) {
        return ESP_FAIL;
    }

    buffer.clear();
    records_since_flush = 0;
    last_flush_time = esp_timer_get_time() / 1000;
    
    return ESP_OK;
}

esp_err_t SDCard::flush_logs() {
    if (log_buffer.empty() || !is_initialized) return ESP_OK;

    FILE* f = fopen(log_path, "a");
    if (f == NULL) return ESP_FAIL;

    fwrite(log_buffer.c_str(), 1, log_buffer.size(), f);
    fclose(f);

    log_buffer.clear();
    last_log_flush_time = esp_timer_get_time() / 1000;
    return ESP_OK;
}

bool SDCard::isConnected() const {
    // Card detect is usually active low (connected = ground)
    return gpio_get_level(HardwareConfig::SD_CARD_DETECT) == 0;
}
