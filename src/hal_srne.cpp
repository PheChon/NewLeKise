// FILE: hal_srne.cpp

#include "hal_srne.h"
#include <math.h> // เพิ่ม Library สำหรับฟังก์ชัน round()

// Module-level configurations
HardwareSerial srne_serial(1);
const uint16_t RETRY_INTERVAL_MS = 100;
const uint16_t COMMAND_INTERVAL_MS = 150;
const uint8_t MAX_RETRY = 3;

// --- Private (Static) Function Prototypes ---
static esp_err_t calculateCRC16(const uint8_t *data, size_t length, uint16_t &crc);
static esp_err_t srneParseData(uint8_t deviceAddress, uint16_t startAddress, uint8_t *pBuffer);
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

esp_err_t setManualLoadPowerWithDuration(uint8_t power, uint16_t duration_s) {
    const uint8_t deviceAddress = 0x01;
    const uint16_t startAddress = 0xDF0A;
    const uint16_t numRegisters = 2;
    const uint8_t byteCount = 4;

    uint8_t frame[13];
    
    uint8_t payload[4];
    payload[0] = 0x00;
    payload[1] = power;
    payload[2] = (duration_s >> 8) & 0xFF;
    payload[3] = duration_s & 0xFF;

    frame[0] = deviceAddress;
    frame[1] = 0x10;
    frame[2] = (startAddress >> 8) & 0xFF;
    frame[3] = startAddress & 0xFF;
    frame[4] = (numRegisters >> 8) & 0xFF;
    frame[5] = numRegisters & 0xFF;
    frame[6] = byteCount;
    memcpy(&frame[7], payload, byteCount);

    uint16_t crc;
    calculateCRC16(frame, 11, crc);
    frame[11] = crc & 0xFF;
    frame[12] = (crc >> 8) & 0xFF;

    while(srne_serial.available()) srne_serial.read();
    srne_serial.write(frame, sizeof(frame));
    srne_serial.flush();

    uint8_t response[8];
    srne_serial.setTimeout(100);
    size_t received_count = srne_serial.readBytes(response, sizeof(response));

    if (received_count < sizeof(response)) {
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t setLoadSchedules(const loadScheduleSettingPack *schedules, uint8_t schedule_amount, uint8_t step_num) {
    const uint16_t first_schedule_register = 0xE092;
    uint16_t current_value;
    uint16_t new_value;

    for (uint8_t i = 0; i < schedule_amount; i++)
    {
        esp_task_wdt_reset();
        uint8_t schedule_no = schedules[i].schedule_no;
        uint16_t base_address = first_schedule_register + (schedule_no - 1) * 3;

        if (step_num != 0) {
             Serial.printf("--- Checking Schedule %d (Slot %d) ---\n", i + 1, schedules[i].schedule_no);
        }

        // --- 1) Duration ---
        uint16_t duration_address = base_address;
        new_value = schedules[i].duration_s;
        if (readDataWithRetry(duration_address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
            if (step_num != 0) Serial.printf("      Comparing Duration: Current value = %d, New value = %d\n", current_value, new_value);
            if(current_value == new_value) {
                if (step_num != 0) Serial.printf("  %d.%d.1 Duration for schedule %d is already %us. Skipping.\n", step_num, i + 1, schedule_no, new_value);
            } else {
                if (step_num != 0) Serial.printf("  %d.%d.1 Writing Duration for schedule %d: %us\n", step_num, i + 1, schedule_no, new_value);
                if (writeDataWithRetry(duration_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            if (step_num != 0) Serial.printf("      Failed to read Duration. Writing new value %d\n", new_value);
            if (writeDataWithRetry(duration_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // --- 2) Attended power ---
        uint16_t attended_address = base_address + 1;
        new_value = schedules[i].attended_power;
        if (readDataWithRetry(attended_address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
            if (step_num != 0) Serial.printf("      Comparing Attended Power: Current value = %d, New value = %d\n", current_value, new_value);
            if (current_value == new_value) {
                if (step_num != 0) Serial.printf("  %d.%d.2 Attended power for schedule %d is already %d%%. Skipping.\n", step_num, i + 1, schedule_no, new_value);
            } else {
                if (step_num != 0) Serial.printf("  %d.%d.2 Writing Attended power for schedule %d: %d%%\n", step_num, i + 1, schedule_no, new_value);
                if (writeDataWithRetry(attended_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            if (step_num != 0) Serial.printf("      Failed to read Attended Power. Writing new value %d\n", new_value);
            if (writeDataWithRetry(attended_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // --- 3) Unattended power ---
        uint16_t unattended_address = base_address + 2;
        new_value = schedules[i].unattended_power;
        if (readDataWithRetry(unattended_address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
            if (step_num != 0) Serial.printf("      Comparing Unattended Power: Current value = %d, New value = %d\n", current_value, new_value);
            if (current_value == new_value) {
                if (step_num != 0) Serial.printf("  %d.%d.3 Unattended power for schedule %d is already %d%%. Skipping.\n", step_num, i + 1, schedule_no, new_value);
            } else {
                if (step_num != 0) Serial.printf("  %d.%d.3 Writing Unattended power for schedule %d: %d%%\n", step_num, i + 1, schedule_no, new_value);
                if (writeDataWithRetry(unattended_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            if (step_num != 0) Serial.printf("      Failed to read Unattended power. Writing new value %d\n", new_value);
            if (writeDataWithRetry(unattended_address, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    return ESP_OK;
}

esp_err_t setLithiumBattery(const deviceSettingPack &setting, uint8_t step_num) {
    uint8_t sub_step = 1;
    uint16_t current_value;
    uint16_t new_value;

    // Helper macro to reduce repetition
    #define CHECK_AND_WRITE(address, value_to_write, log_name, format_spec, original_value) \
    do { \
        if (readDataWithRetry(address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) { \
            if (step_num != 0) Serial.printf("      Comparing %s: Current value = %d, New value = %d\n", log_name, current_value, (value_to_write)); \
            if (current_value == (value_to_write)) { \
                if (step_num != 0) Serial.printf("  %d.%d %s is already " format_spec ". Skipping write.\n", step_num, sub_step++, log_name, (original_value)); \
            } else { \
                if (step_num != 0) Serial.printf("  %d.%d Writing %s: " format_spec " to address 0x%04X\n", step_num, sub_step++, log_name, (original_value), address); \
                if (writeDataWithRetry(address, (value_to_write), MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL; \
                vTaskDelay(pdMS_TO_TICKS(1000)); \
            } \
        } else { \
            if (step_num != 0) { \
                Serial.printf("      Failed to read %s. Writing new value " format_spec "\n", log_name, (original_value)); \
                Serial.printf("  %d.%d Writing %s: " format_spec " to address 0x%04X\n", step_num, sub_step++, log_name, (original_value), address); \
            } \
            if (writeDataWithRetry(address, (value_to_write), MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL; \
            vTaskDelay(pdMS_TO_TICKS(1000)); \
        } \
    } while (0)

    CHECK_AND_WRITE(0xE003, setting.voltage_system, "System Voltage", "%dV", setting.voltage_system);
    CHECK_AND_WRITE(0xE008, round(setting.over_charge_voltage * 10.0f), "Overcharge Voltage", "%.1fV", setting.over_charge_voltage);
    CHECK_AND_WRITE(0xE009, round(setting.over_charge_return_voltage * 10.0f), "Overcharge Return Voltage", "%.1fV", setting.over_charge_return_voltage);
    CHECK_AND_WRITE(0xE00D, round(setting.over_discharge_voltage * 10.0f), "Over-discharge Voltage", "%.1fV", setting.over_discharge_voltage);
    CHECK_AND_WRITE(0xE00B, round(setting.over_discharge_return_voltage * 10.0f), "Over-discharge Return Voltage", "%.1fV", setting.over_discharge_return_voltage);
    CHECK_AND_WRITE(0xE002, setting.nominal_capacity, "Nominal Capacity", "%dAH", setting.nominal_capacity);
    CHECK_AND_WRITE(0xE004, 0x0011, "Battery Type", "Lithium (0x%02X)", 0x11);

    #undef CHECK_AND_WRITE // Undefine the macro after use
    return ESP_OK;
}

esp_err_t setLoadPercentage(uint8_t percentage, uint8_t step_num) {
    const uint16_t address = 0xDF0A;
    uint16_t new_value = percentage;
    uint16_t current_value;

    if (readDataWithRetry(address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing Load Percentage: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.1 Load Percentage is already set to %d%%. Skipping write.\n", step_num, percentage);
            return ESP_OK;
        }
    } else {
        if (step_num != 0) Serial.printf("      Failed to read Load Percentage. Proceeding to write.\n");
    }

    if (step_num != 0) Serial.printf("  %d.1 Writing Load Percentage: %d%% to address 0xDF0A\n", step_num, percentage);
    return writeDataWithRetry(address, new_value, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setMaxChargeCurrent(float max_current, uint8_t step_num) {
    const uint16_t address = 0xE001;
    float set_current = (max_current > 3.85) ? 3.85 : max_current;
    uint16_t new_value = round(set_current * 100.0f);
    uint16_t current_value;

    if (readDataWithRetry(address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing MaxChargeCurrent: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.1 Max Charge Current is already set to %.2fA. Skipping write.\n", step_num, set_current);
            return ESP_OK;
        }
    } else {
         if (step_num != 0) Serial.printf("      Failed to read MaxChargeCurrent. Proceeding to write.\n");
    }
    
    if (step_num != 0) Serial.printf("  %d.1 Writing Max Charge Current: %.2fA to address 0xE001\n", step_num, set_current);
    return writeDataWithRetry(address, new_value, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setMaxLoadCurrent(float max_current, uint8_t step_num) {
    const uint16_t address = 0xE08D;
    uint16_t new_value = round(max_current * 100.0f);
    uint16_t current_value;

    if (readDataWithRetry(address, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing MaxLoadCurrent: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.1 Max Load Current is already set to %.2fA. Skipping write.\n", step_num, max_current);
            return ESP_OK;
        }
    } else {
        if (step_num != 0) Serial.printf("      Failed to read MaxLoadCurrent. Proceeding to write.\n");
    }

    if (step_num != 0) Serial.printf("  %d.1 Writing Max Load Current: %.2fA to address 0xE08D\n", step_num, max_current);
    return writeDataWithRetry(address, new_value, MAX_RETRY, RETRY_INTERVAL_MS);
}

esp_err_t setManualMode(uint8_t step_num) {
    uint8_t sub_step = 1;
    uint16_t current_value;
    uint16_t new_value;

    // 1. Set Load Operation Mode to Manual (0x0200)
    new_value = 0x0200;
    if (readDataWithRetry(0xDF09, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing Load Op Mode: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.%d Load Operation Mode is already Manual. Skipping.\n", step_num, sub_step++);
        } else {
             if (step_num != 0) Serial.printf("  %d.%d Setting Load Operation Mode to Manual (0x0200) at address 0xDF09\n", step_num, sub_step++);
             if (writeDataWithRetry(0xDF09, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
        }
    } else {
        if (step_num != 0) Serial.printf("      Failed to read Load Op Mode. Writing new value %d\n", new_value);
        if (writeDataWithRetry(0xDF09, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    }
    
    // 2. Set Manual Power to 0%
    new_value = 0x0000;
    if (readDataWithRetry(0xDF0A, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing Manual Power: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.%d Manual Power is already 0%%. Skipping.\n", step_num, sub_step++);
        } else {
            if (step_num != 0) Serial.printf("  %d.%d Setting Manual Power to 0%% at address 0xDF0A\n", step_num, sub_step++);
            if (writeDataWithRetry(0xDF0A, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
        }
    } else {
        if (step_num != 0) Serial.printf("      Failed to read Manual Power. Writing new value %d\n", new_value);
        if (writeDataWithRetry(0xDF0A, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    }

    // 3. Set Manual Duration
    new_value = 0xD2F0;
    if (readDataWithRetry(0xDF0B, &current_value, MAX_RETRY, RETRY_INTERVAL_MS) == ESP_OK) {
        if (step_num != 0) Serial.printf("      Comparing Manual Duration: Current value = %d, New value = %d\n", current_value, new_value);
        if (current_value == new_value) {
            if (step_num != 0) Serial.printf("  %d.%d Manual Duration is already 0xD2F0. Skipping.\n", step_num, sub_step++);
        } else {
            if (step_num != 0) Serial.printf("  %d.%d Setting Manual Duration to 0xD2F0 at address 0xDF0B\n", step_num, sub_step++);
            if (writeDataWithRetry(0xDF0B, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
        }
    } else {
        if (step_num != 0) Serial.printf("      Failed to read Manual Duration. Writing new value %d\n", new_value);
        if (writeDataWithRetry(0xDF0B, new_value, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) return ESP_FAIL;
    }
    
    return ESP_OK;
}

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

struct VoltageSOCTable {
    float voltage;
    float soc;
};

const VoltageSOCTable soc_lookup_table[] = {
    {10.0f, 0.0f},   {10.1f, 1.0f},   {10.2f, 2.0f},   {10.3f, 3.0f},
    {10.4f, 4.0f},   {10.5f, 5.0f},   {10.6f, 6.0f},   {10.7f, 7.0f},
    {10.8f, 8.0f},   {10.9f, 9.0f},   {11.0f, 9.1f},   {11.1f, 9.2f},
    {11.2f, 9.3f},   {11.3f, 9.4f},   {11.4f, 9.5f},   {11.5f, 9.6f},
    {11.6f, 9.7f},   {11.7f, 9.8f},   {11.8f, 9.9f},   {12.0f, 10.0f},
    {12.1f, 11.2f},  {12.2f, 12.5f},  {12.3f, 13.8f},  {12.4f, 15.0f},
    {12.5f, 16.2f},  {12.6f, 17.5f},  {12.7f, 18.8f},  {12.8f, 20.0f},
    {12.9f, 30.0f},  {13.0f, 50.0f},  {13.1f, 60.0f},  {13.2f, 70.0f},
    {13.3f, 80.0f},  {13.4f, 90.0f},  {13.5f, 99.0f},  {13.6f, 100.0f}
};
const int soc_table_size = sizeof(soc_lookup_table) / sizeof(soc_lookup_table[0]);

esp_err_t updateEstimatedSOC(batteryDataPack *battery_data) {
    uint16_t buffer;
    esp_task_wdt_reset();
    if (readDataWithRetry(0x0101, &buffer, MAX_RETRY, RETRY_INTERVAL_MS) != ESP_OK) {
        return ESP_FAIL;
    }

    float voltage = buffer * 0.1f;
    float estimated_soc = 0.0f;

    if (voltage <= soc_lookup_table[0].voltage) {
        estimated_soc = soc_lookup_table[0].soc;
    } else if (voltage >= soc_lookup_table[soc_table_size - 1].voltage) {
        estimated_soc = soc_lookup_table[soc_table_size - 1].soc;
    } else {
        for (int i = 0; i < soc_table_size - 1; i++) {
            if (voltage >= soc_lookup_table[i].voltage && voltage < soc_lookup_table[i+1].voltage) {
                float v1 = soc_lookup_table[i].voltage;
                float s1 = soc_lookup_table[i].soc;
                float v2 = soc_lookup_table[i+1].voltage;
                float s2 = soc_lookup_table[i+1].soc;
                estimated_soc = s1 + ((voltage - v1) * (s2 - s1)) / (v2 - v1);
                break;
            }
        }
    }

    battery_data->battery_soc_estimated = estimated_soc;
    battery_data->battery_voltage = voltage;

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

static esp_err_t srneParseData(uint8_t deviceAddress, uint16_t startAddress, uint8_t *pBuffer) {
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
    if (response[0] != deviceAddress || response[1] != 0x03 || response[2] != 0x02) return ESP_FAIL;
    
    uint16_t responseCrc = (response[6] << 8) | response[5];
    calculateCRC16(response, 5, calculatedCrc);
    if (responseCrc != calculatedCrc) return ESP_FAIL;

    pBuffer[0] = response[3];
    pBuffer[1] = response[4];
    return ESP_OK;
}

static esp_err_t readDataWithRetry(uint16_t start_address, uint16_t *value, uint8_t max_retry, uint16_t retry_interval_ms) {
    uint8_t attempts = 0;
    uint8_t responseBuffer[2] = {0};
    while (attempts < max_retry) {
        if (srneParseData(0x01, start_address, responseBuffer) == ESP_OK) {
            *value = ((uint16_t)responseBuffer[0] << 8) | responseBuffer[1];
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