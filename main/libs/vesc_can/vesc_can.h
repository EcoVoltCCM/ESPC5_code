#pragma once

#include "esp_twai_onchip.h"
#include "esp_twai.h"
#include <stdint.h>

struct vesc_data_t {
    float rpm = 0.0f;
    float current_a = 0.0f;
    float voltage_v = 0.0f;
    float motor_temp_c = 0.0f;
};

class VescCan {
public:
    VescCan();
    ~VescCan();
    
    void initialize();
    
    // Reads any available CAN messages without blocking
    // Updates internal state and populates out_data
    void read_messages(vesc_data_t& out_data);
    
private:
    static bool rx_done_isr(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx);
    void parse_message_isr(const twai_frame_t& frame);

    twai_node_handle_t twai_node = NULL;
    vesc_data_t latest_data;
    uint32_t msg_count = 0;
};
