#pragma once
#include <Arduino.h>
typedef enum { ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP = 1, ESP_MAC_BT = 2 } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);
