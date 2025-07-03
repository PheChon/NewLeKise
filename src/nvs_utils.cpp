// FILE: nvs_utils.cpp

#include "nvs_utils.h"

// Private function for this file
uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    while (length--)
    {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
    }
    return ~crc;
}

void initNVS(const char *keyname)
{
    Preferences prefs;
    if (!prefs.begin(keyname, false))
    {
        Serial.printf("⚠️ Preferences.begin failed on init: %s\n", keyname);
    }
    else
    {
        Serial.printf("✅ Preferences.begin success for: %s\n", keyname);
        prefs.end();
    }
}

void eraseAllNVS()
{
    Serial.println("🧹 Erasing all NVS...");
    esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK)
    {
        Serial.printf("⚠️ NVS erase failed: %s\n", esp_err_to_name(erase_err));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t init_err = nvs_flash_init();
    if (init_err != ESP_OK)
    {
        Serial.printf("❌ NVS re-initialization failed: %s\n", esp_err_to_name(init_err));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    if (erase_err == ESP_OK && init_err == ESP_OK)
    {
        Serial.println("✅ NVS erased and re-initialized. Restarting...");
        esp_restart();
    }
}

void saveIntToNVS(const char *main_key, const char *backup_key, int value)
{
    Preferences prefs;
    if (!prefs.begin("b2eco", false))
    {
        Serial.println("⚠️ Failed to open NVS for writing");
        return;
    }

    uint8_t v = static_cast<uint8_t>(value);
    uint32_t calculated_crc = crc32(&v, sizeof(v));
    String main_crc_key = String(main_key) + "_crc";
    String backup_crc_key = String(backup_key) + "_crc";

    // Write backup first, then main
    prefs.putUChar(backup_key, v);
    prefs.putULong(backup_crc_key.c_str(), calculated_crc);
    prefs.putUChar(main_key, v);
    prefs.putULong(main_crc_key.c_str(), calculated_crc);

    prefs.end();
}

int loadIntFromNVS(const char *main_key, const char *backup_key, int default_value)
{
    Preferences prefs;
    if (!prefs.begin("b2eco", true)) // Read-only
    {
        Serial.println("⚠️ Failed to open NVS for reading");
        return default_value;
    }

    // --- Try loading from the main key ---
    uint8_t value = prefs.getUChar(main_key, static_cast<uint8_t>(default_value));
    uint32_t stored_crc = prefs.getULong((String(main_key) + "_crc").c_str(), 0);
    uint32_t calculated_crc = crc32(&value, sizeof(value));

    if (stored_crc == calculated_crc)
    {
        prefs.end();
        return value; // Main value is valid
    }
    Serial.printf("⚠️ Main NVS key '%s' is corrupted. Trying backup.\n", main_key);

    // --- Try loading from the backup key ---
    uint8_t backup_value = prefs.getUChar(backup_key, static_cast<uint8_t>(default_value));
    uint32_t backup_stored_crc = prefs.getULong((String(backup_key) + "_crc").c_str(), 0);
    uint32_t backup_calculated_crc = crc32(&backup_value, sizeof(backup_value));

    prefs.end(); // End read-only session

    if (backup_stored_crc == backup_calculated_crc)
    {
        Serial.printf("✅ Backup key '%s' is valid. Restoring main key.\n", backup_key);
        saveIntToNVS(main_key, backup_key, backup_value); // Restore the corrupted main key
        return backup_value;
    }
    Serial.printf("⚠️ Backup NVS key '%s' is also corrupted. Using default.\n", backup_key);

    // --- Both are corrupted, save the default value ---
    saveIntToNVS(main_key, backup_key, default_value);
    return default_value;
}