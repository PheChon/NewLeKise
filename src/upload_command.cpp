// FILE: upload_command.cpp

#include "upload_command.h"

// --- Private function for this file ---
static void checkSum(const uint8_t *data, size_t length, uint8_t &sum)
{
    sum = 0x00;
    for (size_t i = 0; i < length; ++i)
    {
        sum ^= data[i];
    }
}

void resetListenerTask(void *parameter)
{
    const uint8_t START_BYTE = 0xA5;
    const uint8_t END_BYTE = 0x5A;
    const uint8_t MODE_CMD = 0x04;
    
    enum Command : uint8_t {
        CMD_SET_TIME = 0x01,
        CMD_CLEAR_NVS = 0x02,
        CMD_FACTORY_RESET = 0x03,
    };

    for (;;)
    {
        if (serial_port.available() < 3)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (serial_port.peek() != START_BYTE)
        {
            serial_port.read(); // Discard byte
            continue;
        }

        // Read header and mode
        uint8_t header = serial_port.read();
        uint8_t mode = serial_port.read();
        
        if (mode != MODE_CMD) {
            Serial.printf("❌ Unknown command mode: 0x%02X\n", mode);
            continue;
        }

        uint8_t cmd = serial_port.read();

        if (cmd == CMD_SET_TIME)
        {
            uint8_t payload[9]; // 7 data bytes + checksum + end byte
            if (serial_port.readBytes(payload, 9) != 9) continue; // Timeout

            if (payload[8] != END_BYTE) {
                Serial.println("❌ Bad tail in SET TIME packet");
                continue;
            }

            uint8_t cs = mode ^ cmd ^ payload[0] ^ payload[1] ^ payload[2] ^ payload[3] ^ payload[4] ^ payload[5] ^ payload[6];
            if (cs != payload[7]) {
                Serial.println("❌ Bad checksum in SET TIME packet");
                continue;
            }
            
            DateTime dt( (payload[2] << 8) | payload[3], payload[1], payload[0], payload[4], payload[5], payload[6]);
            rtc.adjust(dt);

            Serial.printf("⏱️ RTC Time updated: %02d/%02d/%04d %02d:%02d:%02d\n",
                          dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute(), dt.second());
            saveIntToNVS("tud", "tud_bu", 1);
            
            vTaskDelay(pdMS_TO_TICKS(100));
            Serial.println("🔄 Restarting to apply time changes...");
            esp_restart();
        }
        else if (cmd == CMD_CLEAR_NVS || cmd == CMD_FACTORY_RESET)
        {
            uint8_t payload[3]; // data + checksum + end byte
            if (serial_port.readBytes(payload, 3) != 3) continue;

            if (payload[0] != 0x01 || payload[2] != END_BYTE) {
                Serial.println("❌ Bad framing in CMD packet");
                continue;
            }

            if ((mode ^ cmd ^ payload[0]) != payload[1]) {
                Serial.println("❌ Bad checksum in CMD packet");
                continue;
            }

            if (cmd == CMD_CLEAR_NVS) {
                Serial.println("🧹 Executing Clear NVS command...");
                eraseAllNVS(); // This function will restart the device
            } else {
                Serial.println("🔄 Executing Factory Reset command...");
                factoryReset();
                saveIntToNVS("pgr", "pgr_bu", 0); // Mark program as not configured
                vTaskDelay(pdMS_TO_TICKS(100));
                Serial.println("🔄 Restarting after factory reset...");
                esp_restart();
            }
        }
        else
        {
            Serial.printf("❓ Unknown command: 0x%02X\n", cmd);
        }
    }
}

// updateTime function seems to be unused legacy code, can be removed if not needed.
// I've left it commented out.
/*
esp_err_t updateTime(uint16_t *time_package)
{
    // ... implementation
};
*/