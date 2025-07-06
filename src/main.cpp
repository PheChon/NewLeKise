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
void demoLoadRampDown();
// --- Global Task Handles ---
TaskHandle_t resetTaskHandle = NULL;
TaskHandle_t wifiMqttTaskHandle = NULL;
TaskHandle_t elegantTaskHandle = NULL;

void setup()
{
    setupMCU();

    String mac_address = WiFi.macAddress();
    mac_address.replace(":", "");
    String client_id_str = "ESP32-SRNE-" + mac_address;
    myMQTT.client_name = client_id_str.c_str();
    Serial.printf("✅ MQTT Client ID set to: %s\n", myMQTT.client_name);

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

    Wire.begin(); 
    bool rtc_available = (setupDs3231() == ESP_OK);

    setupExternalInterrupt(EXTERNAL_INTERRUPT_PIN, SERIAL_TX_PIN, SERIAL_RX_PIN);
    xTaskCreatePinnedToCore(resetListenerTask, "ResetListener", 4096, NULL, 5, &resetTaskHandle, 1);

    if (rtc_available && loaded_tud) {
        if (setupSrne(SRNE_RX_PIN, SRNE_TX_PIN) == ESP_OK) {
             getCurrentTime(&time_data);
             setChargingProfile(time_data, &charge_profile);

            Serial.println("Forcing SRNE configuration on every boot...");
            if (configSrne() == ESP_OK) {
                saveIntToNVS("pgr", "pgr_bu", 1);
                loaded_pgr = loadIntFromNVS("pgr", "pgr_bu", 1);
                Serial.println("✅ SRNE controller configured successfully.");
            } else {
                Serial.println("❌ SRNE configuration failed. Halting.");
                while(true) vTaskDelay(1000);
            }

        } else {
            Serial.println("❌ SRNE communication failed. Halting.");
            while(true) vTaskDelay(1000);
        }
    } else {
        Serial.println("⚠️ RTC not found or time not set. Waiting for command...");
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
    // if (TimeStartNewCurrent = 5 && !NewCurrent)
    // {
    //     demoLoadRampDown();
    //     Serial.println("setManualLoadPowerWithDuration");
    //     NewCurrent = true;
    // }
    
    // if (TimeStartNewCurrent >= 2160 && !NewCurrent) {
    //     if (setMaxLoadCurrent(device_setting.max_load_current * 0.7) == ESP_OK) {
    //         Serial.println("💡 Load current reduced after extended use.");
    //         NewCurrent = true;
    //     }
    // }

    // if (NewCurrent && time_data.hour == 5 && time_data.minute == 40) {
    //      if (setMaxLoadCurrent(device_setting.max_load_current) == ESP_OK) {
    //         Serial.println("💡 Load current restored to default.");
    //         NewCurrent = false;
    //         TimeStartNewCurrent = 0;
    //     }
    // }
    
    // if (load_data.load_current < 0.1 && TimeStartNewCurrent > 0) {
    //     TimeStartNewCurrent = 0;
    // }

    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t configSrne()
{
    Serial.println("--- Starting SRNE Configuration ---");
    uint8_t step = 1;

    Serial.printf("%d. Set Max Charge Current\n", step);
    if (setMaxChargeCurrent(charge_profile.max_charge_current, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Add delay between major steps

    Serial.printf("%d. Set Max Load Current\n", step);
    if (setMaxLoadCurrent(device_setting.max_load_current, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Add delay between major steps

    Serial.printf("%d. Set Load Percentage\n", step);
    if (setLoadPercentage(device_setting.load_percentage, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Add delay between major steps

    // +++ START: เพิ่มการเรียกใช้ฟังก์ชันใหม่ที่นี่ +++
    Serial.printf("%d. Set Light Control Voltage\n", step);
    if (setLightControlVoltage(device_setting.voltage_light_control, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Add delay between major steps
    // +++ END: เพิ่มการเรียกใช้ฟังก์ชันใหม่ที่นี่ +++
    
    Serial.printf("%d. Set Manual Mode\n", step);
    if (setManualMode(step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Add delay between major steps

    Serial.printf("%d. Set Load Schedules\n", step);
    esp_task_wdt_reset();
    if (setLoadSchedules(load_schedule, SCHEDULE_SLOT_COUNT, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Add delay between major steps

    Serial.printf("%d. Set Lithium Battery Parameters\n", step);
    if (setLithiumBattery(device_setting, step++) != ESP_OK) {
        Serial.println("   -> FAILED"); return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Add delay between major steps
    
    Serial.println("--- SRNE Configuration Finished ---");
    return ESP_OK;
}
void demoLoadRampDown() {
    Serial.println("Step 1: Turning load ON at 100% power (in test mode).");
    
    // สั่งเปิดไฟ 100% เป็นเวลา 2 นาที (120 วินาที) เพื่อให้แน่ใจว่าโหลดเปิดอยู่
    if (setManualLoadPowerWithDuration(20, 30) == ESP_OK) {
        Serial.println(" -> Load is ON at 100%.");
    } else {
        Serial.println(" -> Failed to turn on load.");
        return; 
    }

    // Serial.println("Step 2: Waiting for 30 seconds...");
    // vTaskDelay(pdMS_TO_TICKS(1000));

    // Serial.println("Step 3: Reducing power to 70%.");
    
    // // สั่งลดกำลังไฟเหลือ 70% และให้ทำงานต่อไปอีก 1 ชั่วโมง (3600 วินาที)
    // if (setManualLoadPowerWithDuration(70, 3600) == ESP_OK) {
    //     Serial.println(" -> Power successfully reduced to 70%.");
    // } else {
    //     Serial.println(" -> Failed to reduce power.");
    // }
}