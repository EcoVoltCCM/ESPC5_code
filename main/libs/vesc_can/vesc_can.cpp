#include "vesc_can.h"
#include "../config/config.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "VESC_CAN";

#define CAN_PACKET_STATUS_1 9
#define CAN_PACKET_STATUS_4 16 // 0x10
#define CAN_PACKET_STATUS_5 27 // 0x1B

#define MOTOR_POLE_PAIRS 21.0f

static portMUX_TYPE vesc_mux = portMUX_INITIALIZER_UNLOCKED;

VescCan::VescCan() {
}

VescCan::~VescCan() {
    if (twai_node) {
        twai_node_disable(twai_node);
        twai_node_delete(twai_node);
    }
}

void VescCan::initialize() {
    ESP_LOGI(TAG, "Initializing TWAI (CAN FD) for VESC");

    twai_onchip_node_config_t node_config = {};
    node_config.io_cfg.tx = HardwareConfig::TWAI_TX_PIN;
    node_config.io_cfg.rx = HardwareConfig::TWAI_RX_PIN;
    node_config.io_cfg.quanta_clk_out = (gpio_num_t)-1;
    node_config.io_cfg.bus_off_indicator = (gpio_num_t)-1;
    node_config.clk_src = (twai_clock_source_t)0; // Default clock
    
    // Set timing for 500kbps Classic CAN
    node_config.bit_timing.bitrate = 500000;
    
    node_config.tx_queue_depth = 5;
    node_config.intr_priority = 1;

    esp_err_t err = twai_new_node_onchip(&node_config, &twai_node);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TWAI driver allocated");
    } else {
        ESP_LOGE(TAG, "Failed to allocate TWAI node: %s", esp_err_to_name(err));
        return;
    }

    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = VescCan::rx_done_isr;
    
    err = twai_node_register_event_callbacks(twai_node, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(err));
        return;
    }

    // Start TWAI driver
    err = twai_node_enable(twai_node);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TWAI driver started");
    } else {
        ESP_LOGE(TAG, "Failed to start TWAI driver: %s", esp_err_to_name(err));
    }
}

void VescCan::read_messages(vesc_data_t& out_data) {
    // We already parse asynchronously in the ISR.
    // Just copy the latest data out safely.
    portENTER_CRITICAL(&vesc_mux);
    out_data = latest_data;
    portEXIT_CRITICAL(&vesc_mux);
}

bool IRAM_ATTR VescCan::rx_done_isr(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx) {
    VescCan* self = (VescCan*)user_ctx;
    
    twai_frame_t rx_frame;
    uint8_t data_buf[64];
    rx_frame.buffer = data_buf;
    rx_frame.buffer_len = sizeof(data_buf);

    while (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK) {
        self->parse_message_isr(rx_frame);
    }
    return false; // Did not wake higher priority task
}

void IRAM_ATTR VescCan::parse_message_isr(const twai_frame_t& frame) {
    if (!frame.header.ide) {
        // Not extended ID
        return;
    }

    uint8_t packet_id = (frame.header.id >> 8) & 0xFF;
    uint16_t data_len = twaifd_dlc2len(frame.header.dlc);

    if (packet_id == CAN_PACKET_STATUS_1 && data_len >= 8) {
        int32_t erpm = (frame.buffer[0] << 24) | (frame.buffer[1] << 16) | (frame.buffer[2] << 8) | frame.buffer[3];
        int16_t current = (frame.buffer[4] << 8) | frame.buffer[5];
        
        portENTER_CRITICAL_ISR(&vesc_mux);
        latest_data.rpm = (float)erpm / MOTOR_POLE_PAIRS;
        latest_data.current_a = (float)current / 10.0f;
        portEXIT_CRITICAL_ISR(&vesc_mux);
        
    } else if (packet_id == CAN_PACKET_STATUS_4 && data_len >= 8) {
        int16_t temp_motor = (frame.buffer[2] << 8) | frame.buffer[3];
        
        portENTER_CRITICAL_ISR(&vesc_mux);
        latest_data.motor_temp_c = (float)temp_motor / 10.0f;
        portEXIT_CRITICAL_ISR(&vesc_mux);
        
    } else if (packet_id == CAN_PACKET_STATUS_5 && data_len >= 8) {
        int16_t v_in = (frame.buffer[4] << 8) | frame.buffer[5];
        
        portENTER_CRITICAL_ISR(&vesc_mux);
        latest_data.voltage_v = (float)v_in / 10.0f;
        portEXIT_CRITICAL_ISR(&vesc_mux);
    }
}
