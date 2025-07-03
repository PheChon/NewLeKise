// FILE: main.cpp

#include "config.h"
#include "mcu_config.h"
#include "nvs_utils.h"
#include "upload_command.h"
#include "hal_rtc.h"
#include "hal_srne.h"
#include "timerRoutine.h"
#include "connectivity_ota.h"

// --- Function Prototypes ---
esp_err_t configSrne();

// --- Global Task Handles ---
TaskHandle_t resetTaskHandle = NULL;
TaskHandle_t wifiMqttTaskHandle = NULL;
TaskHandle_t elegantTaskHandle = NULL;

void setup()
{
    setupMCU();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    initNVS("b2eco");
    loaded_tud = loadIntFromNVS("tud", "tud_bu", 0);
    loaded_pgr = loadIntFromNVS("pgr", "pgr_bu", 0);
    Serial.printf("NVS Loaded: Time Updated Flag = %d, Programmed Flag = %d\n", loaded_tud, loaded_pgr);

    Wire.begin(); // Initialize I2C for RTC
    bool rtc_available = (setupDs3231() == ESP_OK);

    setupExternalInterrupt(EXTERNAL_INTERRUPT_PIN, SERIAL_TX_PIN, SERIAL_RX_PIN);
    xTaskCreatePinnedToCore(resetListenerTask, "ResetListener", 4096, NULL, 5, &resetTaskHandle, 1);

    if (rtc_available && loaded_tud) {
        if (setupSrne(SRNE_RX_PIN, SRNE_TX_PIN) == ESP_OK) {
             getCurrentTime(&time_data);
             setChargingProfile(time_data, &charge_profile);

            if (!loaded_pgr) {
                Serial.println("Device not programmed. Starting configuration...");
                if (configSrne() == ESP_OK) {
                    Serial.println("✅ SRNE controller configured successfully.");
                    saveIntToNVS("pgr", "pgr_bu", 1);
                } else {
                    Serial.println("❌ SRNE configuration failed. Halting.");
                    while(true) vTaskDelay(1000);
                }
            } else {
                 Serial.println("✅ Device already programmed. Skipping initial config.");
            }
        } else {
            Serial.println("❌ SRNE communication failed. Halting.");
            while(true) vTaskDelay(1000);
        }
    } else {
        Serial.println("⚠️ RTC not found or time not set. Waiting for command...");
        // The resetListenerTask will handle the time set command.
    }

    clearAccumulateData();

    setupWiFi(myWiFi);
    setupMQTT(myMQTT);

    xTaskCreatePinnedToCore(keepWiFiMqttAlive, "WiFiMqttTask", 8192, NULL, 4, &wifiMqttTaskHandle, 0);
    xTaskCreatePinnedToCore(elegantTask, "ElegantOTATask", 4096, NULL, 3, &elegantTaskHandle, 0);

    setupTimer(5.0);
}

void loop()
{
    if (!taskDone)
    {
        esp_task_wdt_reset();
        switch (s) {
            case 1:  slot_1_update_data();              break;
            case 2:  slot_2_safety_checks();            break;
            case 3:  slot_3_forecasting_and_adjustment(); break;
            case 4:  slot_4_integration_check();        break;
            case 5:  slot_5_publish_data();             break;
        }
        taskDone = true;
    }

    if (TimeStartNewCurrent >= 2160 && !NewCurrent) {
        if (setMaxLoadCurrent(device_setting.max_load_current * 0.7) == ESP_OK) {
            Serial.println("💡 Load current reduced after extended use.");
            NewCurrent = true;
        }
    }

    if (NewCurrent && time_data.hour == 5 && time_data.minute == 40) {
         if (setMaxLoadCurrent(device_setting.max_load_current) == ESP_OK) {
            Serial.println("💡 Load current restored to default.");
            NewCurrent = false;
            TimeStartNewCurrent = 0;
        }
    }

    if (load_data.load_current < 0.1 && TimeStartNewCurrent > 0) {
        TimeStartNewCurrent = 0;
    }


    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t configSrne()
{
    Serial.println("--- Starting SRNE Configuration ---");
    vTaskDelay(pdMS_TO_TICKS(500));

    // CORRECTED: Pass the struct directly to match the 'const deviceSettingPack&' reference
    if (setLithiumBattery(device_setting) != ESP_OK) return ESP_FAIL;
    Serial.println("1. Set Lithium Battery OK");
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (setMaxChargeCurrent(charge_profile.max_charge_current) != ESP_OK) return ESP_FAIL;
    Serial.println("2. Set Max Charge Current OK");
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (setMaxLoadCurrent(device_setting.max_load_current) != ESP_OK) return ESP_FAIL;
    Serial.println("3. Set Max Load Current OK");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // CORRECTED: Pass the specific member to match the 'uint8_t' argument
    if (setLoadPercentage(device_setting.load_percentage) != ESP_OK) return ESP_FAIL;
    Serial.println("4. Set Load Percentage OK");
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (setManualMode() != ESP_OK) return ESP_FAIL;
    Serial.println("5. Set Manual Mode OK");
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_task_wdt_reset();
    if (setLoadSchedules(load_schedule, SCHEDULE_SLOT_COUNT) != ESP_OK) return ESP_FAIL;
    Serial.println("6. Set Load Schedules OK");

    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println("--- SRNE Configuration Finished ---");
    return ESP_OK;
}