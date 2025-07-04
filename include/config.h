// FILE: config.h

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "nvs_flash.h"

// ─────────────── PIN CONFIGURATION ───────────────
const uint8_t LED_PIN = 2;
const uint8_t SRNE_RX_PIN = 16;
const uint8_t SRNE_TX_PIN = 17;
const uint8_t SERIAL_RX_PIN = 25;
const uint8_t SERIAL_TX_PIN = 26;
const uint8_t EXTERNAL_INTERRUPT_PIN = 27;

// ─────────────── CONSTANTS ───────────────
const uint8_t SCHEDULE_SLOT_COUNT = 9;

// ─────────────── DATA STRUCTURES ───────────────
// ... (struct definitions are unchanged) ...
struct batteryDataPack
{
    float battery_voltage;
    float battery_current;
    int battery_soc;
    int battery_temperature;
    uint32_t charge_wh;
    uint32_t last_charge_wh;
    float battery_soc_estimated; 
};

struct solarDataPack
{
    float solar_voltage;
    float solar_current;
    int solar_power;
};

struct loadDataPack
{
    float load_voltage;
    float load_current;
    int load_power;
    uint32_t load_wh;
    uint32_t last_load_wh;
};

struct timeDataPack
{
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

struct chargingProfilePack
{
    int profile_number;
    float max_charge_current;
    float max_soc;
    float min_soc;
    float safe_soc_1;
    float safe_soc_2;
};

struct deviceSettingPack
{
    float max_load_current;
    int load_percentage;
    float voltage_light_control;
    int voltage_system;
    float over_charge_voltage;
    float over_charge_return_voltage;
    float over_discharge_voltage;
    float over_discharge_return_voltage;
    int nominal_capacity;
    uint16_t battery_type;
};

struct loadScheduleSettingPack
{
    uint8_t schedule_no;
    uint16_t duration_s;
    uint8_t attended_power;
    uint8_t unattended_power;
};

struct wifiConfig
{
    const char *ssid;
    const char *password;
};

struct MqttConfig
{
    const char *server;
    uint16_t port;
    const char *username;
    const char *password;
    const char *client_name;
};

// ─────────────── GLOBAL STATE VARIABLES (EXTERN) ───────────────
// The actual variables are defined in variables_store.cpp
extern batteryDataPack battery_data;
extern solarDataPack solar_data;
extern loadDataPack load_data;
extern timeDataPack time_data;
extern chargingProfilePack charge_profile;
extern deviceSettingPack device_setting;
extern loadScheduleSettingPack load_schedule[SCHEDULE_SLOT_COUNT];
extern wifiConfig myWiFi;
extern MqttConfig myMQTT;

extern int loaded_tud;
extern int loaded_pgr;
extern bool integrated;
extern volatile bool upload_mode;
extern volatile bool taskDone;
extern volatile uint8_t s;
extern volatile int TimeStartNewCurrent;
extern volatile bool NewCurrent;


// ─────────────── GLOBAL OBJECTS (EXTERN) ───────────────
extern RTC_DS3231 rtc;
extern HardwareSerial serial_port;

#endif // CONFIG_H