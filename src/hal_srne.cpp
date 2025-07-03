// FILE: hal_srne.cpp

#include "hal_srne.h"

// Module-level configurations
HardwareSerial srne_serial(1);
const uint16_t RETRY_INTERVAL_MS = 100;
const uint16_t COMMAND_INTERVAL_MS = 150;
const uint8_t MAX_RETRY = 3;

// --- Private (Static) Function Prototypes ---
static esp_err_t calculateCRC16(const uint8_t *data, size_t length, uint16_t &crc);
static esp_err_t srneParseData(uint8_t deviceAddress, uint16_t startAddress, uint16_t *pBuffer);
static esp_err_t srneParseData32(uint8_t deviceAddress, uint16_t startAddress, uint32_t *pBuffer);
static esp_err_t srneWriteData(uint8_t deviceAddress, uint16_t startAddress, uint16_t value);
static esp_err_t readDataWithRetry(uint16_t start_address, uint16_t *value, uint8_t max_retry, uint16_t retry_interval_ms);
static esp_err_t readDataWithRetry32(uint16_t start_address, uint32_t *value, uint8_t max_retry, uint16_t retry_interval_ms);


// --- Public Function Implementations ---

esp_err_t setupSrne(uint8_t rx_pin, uint8_t tx_pin) {
    srne_serial.begin(9600, SERIAL_8N1, rx_pin, tx_pin);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t pBuffer = 0;
    if (readDataWithRetry(0x000A, &pBuffer, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) {
        Serial.println("❌ SRNE controller not responding.");
        return ESP_FAIL;
    }
    Serial.println("✅ SRNE controller initialized successfully.");
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}

esp_err_t writeDataWithRetry(uint16_t start_address, uint16_t value, uint8_t max_retry, uint16_t retry_interval_ms) {
    uint8_t attempts = 0;
    while (attempts < max_retry) {
        if (srneWriteData(0x01, start_address, value) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            return ESP_OK;
        }
        attempts++;
        vTaskDelay(pdMS_TO_TICKS(retry_interval_ms));
    }
    return ESP_FAIL;
}

// --- CORRECTED FUNCTION ---
// Added 1000ms delays between each parameter write
esp_err_t setLithiumBattery(const deviceSettingPack &setting, uint8_t step_num) {
    uint8_t sub_step = 1;
    
    if (step_num != 0) Serial.printf("  %d.%d Writing System Voltage: %dV to address 0xE003\n", step_num, sub_step++, setting.voltage_system);
    if (writeDataWithRetry(0xE003, setting.voltage_system, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (step_num != 0) Serial.printf("  %d.%d Writing Overcharge Voltage: %.1fV to address 0xE008\n", step_num, sub_step++, setting.over_charge_voltage);
    if (writeDataWithRetry(0xE008, setting.over_charge_voltage * 10, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (step_num != 0) Serial.printf("  %d.%d Writing Overcharge Return Voltage: %.1fV to address 0xE009\n", step_num, sub_step++, setting.over_charge_return_voltage);
    if (writeDataWithRetry(0xE009, setting.over_charge_return_voltage * 10, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (step_num != 0) Serial.printf("  %d.%d Writing Over-discharge Voltage: %.1fV to address 0xE00D\n", step_num, sub_step++, setting.over_discharge_voltage);
    if (writeDataWithRetry(0xE00D, setting.over_discharge_voltage * 10, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (step_num != 0) Serial.printf("  %d.%d Writing Over-discharge Return Voltage: %.1fV to address 0xE00B\n", step_num, sub_step++, setting.over_discharge_return_voltage);
    if (writeDataWithRetry(0xE00B, setting.over_discharge_return_voltage * 10, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (step_num != 0) Serial.printf("  %d.%d Writing Nominal Capacity: %dAH to address 0xE002\n", step_num, sub_step++, setting.nominal_capacity);
    if (writeDataWithRetry(0xE002, setting.nominal_capacity, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (step_num != 0) Serial.printf("  %d.%d Writing Battery Type to Lithium (0x11) at address 0xE004\n", step_num, sub_step++);
    if (writeDataWithRetry(0xE004, 0x0011, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1000));

    return ESP_OK;
}


esp_err_t setLoadSchedules(const loadScheduleSettingPack *schedules, uint8_t schedule_amount, uint8_t step_num) {
    const uint16_t first_schedule_register = 0xE092;

    for (uint8_t i = 0; i < schedule_amount; i++)
    {
        esp_task_wdt_reset();
        uint8_t schedule_no = schedules[i].schedule_no;
        uint16_t register_address = first_schedule_register + (schedule_no - 1) * 3;

        if (step_num != 0 && schedules[i].duration_s > 0) {
             Serial.printf("  %d.%d Setting Schedule %d...\n", step_num, i + 1, schedules[i].schedule_no);
        }

        if (writeDataWithRetry(register_address, schedules[i].duration_s, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) {
            Serial.printf("Duration of slot %d: ERROR\n", schedule_no);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (writeDataWithRetry(register_address + 1, schedules[i].attended_power, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) {
            Serial.printf("Attended power of slot %d: ERROR\n", schedule_no);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (writeDataWithRetry(register_address + 2, schedules[i].unattended_power, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) {
            Serial.printf("Unattended power of slot %d: ERROR\n", schedule_no);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return ESP_OK;
}

esp_err_t setLoadPercentage(uint8_t percentage, uint8_t step_num) {
    if (step_num != 0) {
        Serial.printf("  %d.1 Writing Load Percentage for Manual/Test Mode: %d%% to address 0xDF0A\n", step_num, percentage);
    }
    return writeDataWithRetry(0xDF0A, percentage, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setMaxChargeCurrent(float max_current, uint8_t step_num) {
    float set_current = (max_current > 3.85) ? 3.85 : max_current;
    if (step_num != 0) {
        Serial.printf("  %d.1 Writing Max Charge Current: %.2fA to address 0xE001\n", step_num, set_current);
    }
    return writeDataWithRetry(0xE001, set_current * 100, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setMaxLoadCurrent(float max_current, uint8_t step_num) {
    if (step_num != 0) {
        Serial.printf("  %d.1 Writing Max Load Current: %.2fA to address 0xE08D\n", step_num, max_current);
    }
    return writeDataWithRetry(0xE08D, max_current * 100, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setManualMode(uint8_t step_num) {
    uint8_t sub_step = 1;
    if (step_num != 0) Serial.printf("  %d.%d Setting Load Operation Mode to Manual at address 0xDF09\n", step_num, sub_step++);
    if (writeDataWithRetry(0xDF09, 0x0200, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    
    if (step_num != 0) Serial.printf("  %d.%d Setting Manual Power to 0%% at address 0xDF0A\n", step_num, sub_step++);
    if (writeDataWithRetry(0xDF0A, 0x0000, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;

    if (step_num != 0) Serial.printf("  %d.%d Setting Manual Duration at address 0xDF0B\n", step_num, sub_step++);
    if (writeDataWithRetry(0xDF0B, 0xD2F0, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    
    return ESP_OK;
}

// ... (The rest of the file is unchanged) ...
esp_err_t factoryReset() {
    bool success = true;
    if (writeDataWithRetry(0xDF02, 0x0001, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) success = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (writeDataWithRetry(0xDF05, 0x0001, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) success = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (writeDataWithRetry(0xDF01, 0x0001, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) success = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    return success ? ESP_OK : ESP_FAIL;
}
esp_err_t getLoadInfo(loadDataPack *ld) {
    uint16_t buffer;
    if (readDataWithRetry(0x0104, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) ld->load_voltage = buffer * 0.1; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0105, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) ld->load_current = buffer * 0.01; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0106, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) ld->load_power = buffer; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}
esp_err_t getSolarInfo(solarDataPack *solar_data) {
    uint16_t buffer;
    if (readDataWithRetry(0x0107, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) solar_data->solar_voltage = buffer * 0.1; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0108, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) solar_data->solar_current = buffer * 0.01; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0109, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) solar_data->solar_power = buffer; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}
esp_err_t getBatteryInfo(batteryDataPack *battery_data) {
    uint16_t buffer;
    if (readDataWithRetry(0x0100, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) battery_data->battery_soc = buffer; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0101, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) battery_data->battery_voltage = buffer * 0.1; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0102, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) battery_data->battery_current = buffer * 0.01; else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    if (readDataWithRetry(0x0103, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) battery_data->battery_temperature = (int8_t)(buffer & 0xFF); else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}
esp_err_t getLoadWh(loadDataPack *load_data) {
    uint32_t pBuffer = 0;
    if (readDataWithRetry32(0x011E, &pBuffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        load_data->load_wh = pBuffer;
    } else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}
esp_err_t getChargeWh(batteryDataPack *battery_data) {
    uint32_t pBuffer = 0;
    if (readDataWithRetry32(0x011C, &pBuffer, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        battery_data->charge_wh = pBuffer;
    } else return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
    return ESP_OK;
}
esp_err_t clearAccumulateData() {
    if (writeDataWithRetry(0xDF05, 1, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(COMMAND_INTERVAL_MS));
        return ESP_OK;
    }
    return ESP_FAIL;
}
static esp_err_t readDataWithRetry(uint16_t start_address, uint16_t *value, uint8_t max_retry, uint16_t retry_interval_ms) {
    uint8_t attempts = 0;
    uint16_t responseBuffer[2] = {0};
    while (attempts < max_retry) {
        if (srneParseData(0x01, start_address, responseBuffer) == ESP_OK) {
            *value = (responseBuffer[0] << 8) | responseBuffer[1];
            return ESP_OK;
        }
        attempts++;
        vTaskDelay(pdMS_TO_TICKS(retry_interval_ms));
    }
    return ESP_FAIL;
}
static esp_err_t readDataWithRetry32(uint16_t start_address, uint32_t *value, uint8_t max_retry, uint16_t retry_interval_ms) {
    uint8_t attempts = 0;
    uint8_t rawBuffer[4] = {0};
    while (attempts < max_retry) {
        if (srneParseData32(0x01, start_address, (uint32_t*)rawBuffer) == ESP_OK) {
            *value = (uint32_t)rawBuffer[0] << 24 | (uint32_t)rawBuffer[1] << 16 | (uint32_t)rawBuffer[2] << 8 | (uint32_t)rawBuffer[3];
            return ESP_OK;
        }
        attempts++;
        vTaskDelay(pdMS_TO_TICKS(retry_interval_ms));
    }
    return ESP_FAIL;
}
static esp_err_t srneParseData(uint8_t deviceAddress, uint16_t startAddress, uint16_t *pBuffer) {
    const int32_t time_out_ms = 60;
    uint8_t data[8];
    uint8_t response[7]; 
    memset(data, 0, sizeof(data));
    data[0] = deviceAddress;
    data[1] = 0x03; 
    data[2] = (startAddress >> 8) & 0xFF;
    data[3] = startAddress & 0xFF;
    data[4] = 0x00;
    data[5] = 0x01; 
    uint16_t calculatedCrc;
    calculateCRC16(data, 6, calculatedCrc);
    data[6] = calculatedCrc & 0xFF;
    data[7] = (calculatedCrc >> 8) & 0xFF;
    while(srne_serial.available()) srne_serial.read();
    srne_serial.write(data, sizeof(data));
    srne_serial.flush();
    srne_serial.setTimeout(time_out_ms);
    size_t received_count = srne_serial.readBytes(response, sizeof(response));
    if (received_count < sizeof(response)) return ESP_ERR_TIMEOUT;
    if (response[0] != deviceAddress || response[1] != 0x03) return ESP_FAIL;
    uint16_t responseCrc = (response[6] << 8) | response[5];
    calculateCRC16(response, 5, calculatedCrc);
    if (responseCrc != calculatedCrc) return ESP_FAIL;
    pBuffer[0] = response[3]; 
    pBuffer[1] = response[4]; 
    return ESP_OK;
}
static esp_err_t srneParseData32(uint8_t deviceAddress, uint16_t startAddress, uint32_t *pBuffer) {
    const int32_t time_out_ms = 60;
    uint8_t data[8];
    uint8_t response[9];
    data[0] = deviceAddress;
    data[1] = 0x03;
    data[2] = (startAddress >> 8) & 0xFF;
    data[3] = startAddress & 0xFF;
    data[4] = 0x00;
    data[5] = 0x02; 
    uint16_t calculatedCrc;
    calculateCRC16(data, 6, calculatedCrc);
    data[6] = calculatedCrc & 0xFF;
    data[7] = (calculatedCrc >> 8) & 0xFF;
    while(srne_serial.available()) srne_serial.read();
    srne_serial.write(data, sizeof(data));
    srne_serial.flush();
    srne_serial.setTimeout(time_out_ms);
    size_t received_count = srne_serial.readBytes(response, sizeof(response));
    if (received_count < sizeof(response)) return ESP_ERR_TIMEOUT;
    if (response[0] != deviceAddress || response[1] != 0x03) return ESP_FAIL;
    uint16_t responseCrc = (response[8] << 8) | response[7];
    calculateCRC16(response, 7, calculatedCrc);
    if (responseCrc != calculatedCrc) return ESP_FAIL;
    uint8_t* byte_ptr = (uint8_t*)pBuffer;
    byte_ptr[0] = response[3]; 
    byte_ptr[1] = response[4];
    byte_ptr[2] = response[5];
    byte_ptr[3] = response[6]; 
    return ESP_OK;
}
static esp_err_t srneWriteData(uint8_t deviceAddress, uint16_t startAddress, uint16_t value) {
    const uint32_t time_out_ms = 60;
    uint8_t data[8];
    uint8_t response[8];
    data[0] = deviceAddress;
    data[1] = 0x06; 
    data[2] = (startAddress >> 8) & 0xFF;
    data[3] = startAddress & 0xFF;
    data[4] = (value >> 8) & 0xFF;
    data[5] = value & 0xFF;
    uint16_t calculatedCrc;
    calculateCRC16(data, 6, calculatedCrc);
    data[6] = calculatedCrc & 0xFF;
    data[7] = (calculatedCrc >> 8) & 0xFF;
    while(srne_serial.available()) srne_serial.read();
    srne_serial.write(data, sizeof(data));
    srne_serial.flush();
    srne_serial.setTimeout(time_out_ms);
    size_t received_count = srne_serial.readBytes(response, sizeof(response));
    if (received_count < sizeof(response)) return ESP_ERR_TIMEOUT;
    for (int i = 0; i < 8; i++) {
        if (data[i] != response[i]) return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t calculateCRC16(const uint8_t *data, size_t length, uint16_t &crc)
{
    crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (size_t j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ESP_OK;
}