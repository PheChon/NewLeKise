// FILE: connectivity_ota.cpp

#include "connectivity_ota.h"
#include <ArduinoJson.h>

// --- Module-level (static) variables ---
static WiFiClient espClient;
static PubSubClient client(espClient);
static AsyncWebServer server(80);
static SemaphoreHandle_t mqttMutex = xSemaphoreCreateMutex();

// --- Public Function Implementations ---

esp_err_t setupWiFi(wifiConfig wifi_parameter)
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_parameter.ssid, wifi_parameter.password);

    Serial.print("📡 Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n✅ WiFi Connected!");
        Serial.print("   IP Address: ");
        Serial.println(WiFi.localIP());
        return ESP_OK;
    }
    else
    {
        Serial.println("\n❌ WiFi Connection Failed!");
        return ESP_FAIL;
    }
}

esp_err_t setupMQTT(MqttConfig mqtt_parameter)
{
    client.setServer(mqtt_parameter.server, mqtt_parameter.port);
    
    if (WiFi.status() == WL_CONNECTED)
    {
        if (client.connect(mqtt_parameter.client_name, mqtt_parameter.username, mqtt_parameter.password))
        {
            Serial.println("[MQTT] Connected!");
            return ESP_OK;
        }
        else
        {
            return ESP_FAIL;
        }
    }

    return ESP_FAIL;
}

void keepWiFiMqttAlive(void *parameter)
{
    static const uint16_t delay_time_ms = 250;
    client.setKeepAlive(60);

    for (;;)
    {
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0))
        {
            Serial.println("⚠️ WiFi Disconnected! Attempting to reconnect...");

            WiFi.disconnect();
            vTaskDelay(pdMS_TO_TICKS(100));
            
            WiFi.begin(myWiFi.ssid, myWiFi.password);

            bool wifiReconnected = false;
            for (uint8_t i = 0; i < 10; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(delay_time_ms));
                esp_task_wdt_reset();

                if (WiFi.status() == WL_CONNECTED)
                {
                    wifiReconnected = true;
                    Serial.println("✅ WiFi Reconnected!");
                    break;
                }
            }
            if (!wifiReconnected)
            {
                Serial.println("❌ WiFi reconnection failed, retrying later...");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }
        else if (WiFi.status() == WL_CONNECTED && !client.connected())
        {
            Serial.println("⚠️ MQTT Disconnected while WiFi is Connected! Attempting to reconnect...");
            client.disconnect();

            bool mqttReconnected = false;
            for (uint8_t i = 0; i < 10; i++)
            {
                if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0))
                {
                    Serial.println("❌ WiFi disconnected during MQTT reconnect. Aborting MQTT reconnect attempt.");
                    break;
                }

                Serial.printf("🔄 Retrying MQTT Connection (Attempt %d/10)...\n", i+1);
                
                if (!espClient.connect(myMQTT.server, myMQTT.port)) {
                    Serial.println("   -> TCP connection failed!");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                
                if (client.connect(myMQTT.client_name, myMQTT.username, myMQTT.password))
                {
                    mqttReconnected = true;
                    Serial.println("✅ MQTT Reconnected!");
                    break;
                }
                
                Serial.print("   -> MQTT connection failed, rc=");
                Serial.println(client.state());

                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_task_wdt_reset();
            }

            if (!mqttReconnected)
            {
                Serial.println("❌ MQTT reconnection failed, retrying later...");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }
        else if (WiFi.status() == WL_CONNECTED && client.connected())
        {
            if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                client.loop();
                xSemaphoreGive(mqttMutex);
            }
            esp_task_wdt_reset();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t publishData(const loadDataPack &load_data, const solarDataPack &solar_data, const batteryDataPack &battery_data, const timeDataPack &time_data, const char *publish_topic)
{
    const uint16_t PACKAGE_SIZE = 1024;
    const TickType_t publishTimeout = pdMS_TO_TICKS(500);

    if (WiFi.status() != WL_CONNECTED || !client.connected())
    {
        return ESP_FAIL;
    }

    JsonDocument doc;
    
    doc["lv"] = load_data.load_voltage;
    doc["lc"] = load_data.load_current;
    doc["lp"] = load_data.load_power;
    doc["lw"] = load_data.load_wh;
    doc["sv"] = solar_data.solar_voltage;
    doc["sc"] = solar_data.solar_current;
    doc["sp"] = solar_data.solar_power;
    doc["bv"] = battery_data.battery_voltage;
    doc["bc"] = battery_data.battery_current;
    doc["bs"] = battery_data.battery_soc;
    doc["bt"] = battery_data.battery_temperature;
    doc["cw"] = battery_data.charge_wh; // Total Charge Wh (0x011C)
    doc["bse"] = round(battery_data.battery_soc_estimated * 10) / 10.0;

    // +++ START: เพิ่มค่าใหม่ลงใน JSON +++
    doc["tcah"] = battery_data.total_charge_ah; // Total Charge Ah (0x0118)
    doc["tdah"] = battery_data.total_discharge_ah; // Total Discharge Ah (0x011A)
    // +++ END: เพิ่มค่าใหม่ลงใน JSON +++
    
    doc["nw"] = round(New_Wh * 100) / 100.0;
    doc["nwe"] = round(New_Wh_E * 100) / 100.0;
    doc["fwh"] = round(Full_Wh * 100) / 100.0;
    doc["fwe"] = round(Full_Wh_E * 100) / 100.0;
    doc["ce"] = round(current_energy * 100) / 100.0;
    doc["cee"] = round(current_energy_E * 100) / 100.0;

    char timestamp[25];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d", 
             time_data.year, time_data.month, time_data.day, 
             time_data.hour, time_data.minute, time_data.second);
    doc["timestamp"] = timestamp;

    char buffer[PACKAGE_SIZE];
    size_t n = serializeJson(doc, buffer);

    bool success = false;
    if (xSemaphoreTake(mqttMutex, publishTimeout) == pdTRUE)
    {
        success = client.publish(publish_topic, buffer, n);
        xSemaphoreGive(mqttMutex);
    }
    
    return success ? ESP_OK : ESP_FAIL;
}

void elegantTask(void *parameter)
{
    const char *ota_username = "charaphat";
    const char *ota_password = "idealab2023";

    ElegantOTA.begin(&server, ota_username, ota_password);
    server.begin();
    Serial.println("✅ OTA Server Started.");

    for (;;)
    {
        ElegantOTA.loop();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}