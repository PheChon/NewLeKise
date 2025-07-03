// FILE: hal_rtc.h

#ifndef HAL_RTC_H
#define HAL_RTC_H

#include "config.h"

esp_err_t setupDs3231();
esp_err_t getCurrentTime(timeDataPack *time_data);

#endif // HAL_RTC_H