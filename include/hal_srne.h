// FILE: hal_srne.h

#ifndef HAL_SRNE_H
#define HAL_SRNE_H

#include "config.h"

esp_err_t setupSrne(uint8_t rx_pin, uint8_t tx_pin);
esp_err_t writeDataWithRetry(uint16_t start_address, uint16_t value, uint8_t max_retry, uint16_t retry_interval_ms);

// --- เพิ่ม Declaration ใหม่ ---
esp_err_t setManualLoadPowerWithDuration(uint8_t power, uint16_t duration_s);

// Configuration and Setup Functions
esp_err_t setLithiumBattery(const deviceSettingPack &setting, uint8_t step_num = 0);
esp_err_t setLoadPercentage(uint8_t percentage, uint8_t step_num = 0);
esp_err_t setLoadSchedules(const loadScheduleSettingPack *schedules, uint8_t schedule_amount, uint8_t step_num = 0);
esp_err_t setMaxChargeCurrent(float max_current, uint8_t step_num = 0);
esp_err_t setMaxLoadCurrent(float max_current, uint8_t step_num = 0);
esp_err_t setManualMode(uint8_t step_num = 0);
esp_err_t factoryReset();
esp_err_t clearAccumulateData();

// Data Acquisition Functions
esp_err_t getLoadInfo(loadDataPack *load_data);
esp_err_t getSolarInfo(solarDataPack *solar_data);
esp_err_t getBatteryInfo(batteryDataPack *battery_data);
esp_err_t getLoadWh(loadDataPack *load_data);
esp_err_t getChargeWh(batteryDataPack *battery_data);
esp_err_t updateEstimatedSOC(batteryDataPack *battery_data);

#endif // HAL_SRNE_H