// FILE: mcu_config.cpp

#include "mcu_config.h"

HardwareSerial serial_port(2);
hw_timer_t *timer = NULL;

void IRAM_ATTR interruptUploadMode()
{
    upload_mode = true;
}

void IRAM_ATTR timer5s()
{
    taskDone = false;
    s++;
    // +++ START: แก้ไขรอบของ Timer +++
    if (s > 6) // เดิมคือ 12, แก้เป็น 6 เพื่อให้วนครบ 6 slots
    {
        s = 1;
    }
    // +++ END: แก้ไขรอบของ Timer +++
    if (load_data.load_current > 0.1) // Use a small threshold to account for noise
    {
        TimeStartNewCurrent++;
    }
}

void setupMCU()
{
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    setCpuFrequencyMhz(160);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_task_wdt_init(20, true);
    esp_task_wdt_add(NULL); // Add current task to WDT
    vTaskDelay(pdMS_TO_TICKS(50));

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    vTaskDelay(pdMS_TO_TICKS(50));
}

esp_err_t setupTimer(float time_s)
{
    uint64_t ticks = (uint64_t)(time_s * 1000000);

    timer = timerBegin(0, 80, true); // Use prescaler 80 for 1MHz clock
    if (!timer)
    {
        Serial.println("❌ Failed to begin hardware timer");
        return ESP_FAIL;
    }

    timerAttachInterrupt(timer, &timer5s, true);
    timerAlarmWrite(timer, ticks, true);
    timerAlarmEnable(timer);

    return ESP_OK;
}

void setupExternalInterrupt(uint8_t external_intterupt_pin, uint8_t tx_pin, uint8_t rx_pin)
{
    serial_port.begin(9600, SERIAL_8N1, rx_pin, tx_pin);
    pinMode(external_intterupt_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(external_intterupt_pin), interruptUploadMode, FALLING);

    vTaskDelay(pdMS_TO_TICKS(500));
}