#include "wifi_manager.h"
#include "../config/config.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

static void ensureDnsServers() {
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        ESP_LOGW(TAG, "Failed to get WIFI_STA_DEF netif for DNS check");
        return;
    }

    esp_netif_dns_info_t dns_main = {};
    esp_netif_dns_info_t dns_backup = {};
    esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_main);
    esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns_backup);

    ESP_LOGI(TAG,
             "DNS servers: main=" IPSTR " backup=" IPSTR,
             IP2STR(&dns_main.ip.u_addr.ip4),
             IP2STR(&dns_backup.ip.u_addr.ip4));

    if (dns_main.ip.u_addr.ip4.addr == 0) {
        esp_netif_dns_info_t fallback_main = {};
        fallback_main.ip.type = ESP_IPADDR_TYPE_V4;
        ip4addr_aton("8.8.8.8", &fallback_main.ip.u_addr.ip4);
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &fallback_main));

        esp_netif_dns_info_t fallback_backup = {};
        fallback_backup.ip.type = ESP_IPADDR_TYPE_V4;
        ip4addr_aton("1.1.1.1", &fallback_backup.ip.u_addr.ip4);
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &fallback_backup));

        ESP_LOGW(TAG, "Main DNS missing; fallback DNS applied (8.8.8.8 / 1.1.1.1)");
    }
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WiFiManager* instance = static_cast<WiFiManager*>(arg);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (instance->led_indicator) instance->led_indicator->set_wifi_connecting();
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (instance->led_indicator) instance->led_indicator->set_wifi_disconnected();
        xEventGroupClearBits(instance->event_group, instance->CONNECTED_BIT);
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying WiFi connection...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* ip_event = static_cast<ip_event_got_ip_t*>(event_data);
        if (instance->led_indicator) instance->led_indicator->set_wifi_connected();
        xEventGroupSetBits(instance->event_group, instance->CONNECTED_BIT);
        ESP_LOGI(TAG,
                 "WiFi Connected! ip=" IPSTR " gw=" IPSTR " mask=" IPSTR,
                 IP2STR(&ip_event->ip_info.ip),
                 IP2STR(&ip_event->ip_info.gw),
                 IP2STR(&ip_event->ip_info.netmask));
        ensureDnsServers();
    }
}

WiFiManager::WiFiManager() { 
    event_group = xEventGroupCreate(); 
}

WiFiManager::~WiFiManager() { 
    vEventGroupDelete(event_group); 
}

void WiFiManager::initialize() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &eventHandler, this, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &eventHandler, this, NULL);
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, TelemetryConfig::WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, TelemetryConfig::WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Telemetry is latency-sensitive; disable modem sleep to reduce publish jitter.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save disabled (WIFI_PS_NONE) for low-latency telemetry");

    // Force ESP32-C5 to use 5GHz band - Must be called after esp_wifi_start()
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
}

void WiFiManager::waitForConnection(uint32_t timeout_ms) {
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    xEventGroupWaitBits(event_group, CONNECTED_BIT, pdFALSE, pdFALSE, ticks);
}

bool WiFiManager::isConnected() {
    return (xEventGroupGetBits(event_group) & CONNECTED_BIT) != 0;
}
