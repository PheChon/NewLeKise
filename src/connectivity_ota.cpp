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
        Serial.print("🌐 Connecting to MQTT...");
        if (client.connect(mqtt_parameter.client_name, mqtt_parameter.username, mqtt_parameter.password))
        {
            Serial.println("\n✅ MQTT Connected!");
            return ESP_OK;
        }
        else
        {
            Serial.print("\n❌ MQTT Connection Failed during setup, rc=");
            Serial.println(client.state());
            return ESP_FAIL;
        }
    }
    
    Serial.println("WiFi not available during setup, MQTT connection will be attempted by keep-alive task.");
    return ESP_FAIL;
}

void keepWiFiMqttAlive(void *parameter)
{
    client.setKeepAlive(60);

    for (;;)
    {
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("⚠️ WiFi Disconnected! keep-alive task will wait for reconnect.");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!client.connected())
        {
            Serial.println("⚠️ MQTT Disconnected. Attempting to connect...");
            
            bool mqttReconnected = false;
            for (uint8_t i = 0; i < 5; i++)
            {
                if (WiFi.status() != WL_CONNECTED) {
                    Serial.println("❌ WiFi disconnected during MQTT reconnect attempt. Aborting.");
                    break; 
                }

                if (client.connect(myMQTT.client_name, myMQTT.username, myMQTT.password))
                {
                    mqttReconnected = true;
                    Serial.println("✅ MQTT Reconnected!");
                    break;
                }
                
                Serial.print("❌ MQTT connection failed, rc=");
                Serial.print(client.state());
                Serial.println(". Retrying in 3 seconds...");
                vTaskDelay(pdMS_TO_TICKS(3000));
            }

            if (!mqttReconnected) {
                 vTaskDelay(pdMS_TO_TICKS(5000));
                 continue;
            }
        }
        
        if (client.connected())
        {
            if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                client.loop();
                xSemaphoreGive(mqttMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

// --- CORRECTED FUNCTION ---
esp_err_t publishData(const loadDataPack &load_data, const solarDataPack &solar_data, const batteryDataPack &battery_data, const timeDataPack &time_data, const char *publish_topic)
{
    // Increased size to accommodate the timestamp
    const uint16_t PACKAGE_SIZE = 384; 
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
    doc["cw"] = battery_data.charge_wh;

    // --- ADDED TIMESTAMP ---
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