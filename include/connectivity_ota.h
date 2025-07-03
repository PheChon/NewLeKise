// FILE: connectivity_ota.h

#ifndef CONNECTIVITY_OTA_H
#define CONNECTIVITY_OTA_H

#include "config.h"

esp_err_t setupWiFi(wifiConfig wifi_parameter);
esp_err_t setupMQTT(MqttConfig mqtt_parameter);
void keepWiFiMqttAlive(void *parameter);

// --- CORRECTED FUNCTION SIGNATURE ---
esp_err_t publishData(const loadDataPack &load_data, const solarDataPack &solar_data, const batteryDataPack &battery_data, const timeDataPack &time_data, const char *publish_topic);

void elegantTask(void *parameter);

#endif // CONNECTIVITY_OTA_H