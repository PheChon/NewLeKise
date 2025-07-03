// FILE: hal_rtc.cpp

#include "hal_rtc.h"

RTC_DS3231 rtc;

esp_err_t setupDs3231()
{
    // Wire.begin() should be called once in setup, not here.
    // Assuming it's called in setupMCU() or main setup().
    if (!rtc.begin())
    {
        Serial.println("❌ Couldn't find RTC");
        return ESP_FAIL;
    }
    Serial.println("✅ RTC Found");
    return ESP_OK;
}

esp_err_t getCurrentTime(timeDataPack *time_data)
{
    if (!rtc.now().isValid()) {
        return ESP_FAIL;
    }
    
    DateTime now = rtc.now();

    time_data->day = now.day();
    time_data->month = now.month();
    time_data->year = now.year();
    time_data->hour = now.hour();
    time_data->minute = now.minute();
    time_data->second = now.second();

    return ESP_OK;
}