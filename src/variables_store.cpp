// FILE: variables_store.cpp

#include "config.h"

// ─────────────── GLOBAL STATE VARIABLE DEFINITIONS ───────────────
// ... (other variables are unchanged) ...
batteryDataPack battery_data = {
    .battery_voltage = 0.0,
    .battery_current = 0.0,
    .battery_soc = 0,
    .battery_temperature = 0,
    .charge_wh = 0,
    .last_charge_wh = 0,
};

solarDataPack solar_data = {
    .solar_voltage = 0.0,
    .solar_current = 0.0,
    .solar_power = 0,
};

loadDataPack load_data = {
    .load_voltage = 0.0,
    .load_current = 0.0,
    .load_power = 0,
    .load_wh = 0,
    .last_load_wh = 0,
};

timeDataPack time_data = {
    .day = 0,
    .month = 0,
    .year = 0,
    .hour = 0,
    .minute = 0,
    .second = 0,
};

chargingProfilePack charge_profile = {
    .profile_number = 0,
    .max_charge_current = 0.0,
    .max_soc = 0.0,
    .min_soc = 0.0,
    .safe_soc_1 = 0.0,
    .safe_soc_2 = 0.0,
};

deviceSettingPack device_setting = {
    .max_load_current = 1.15,
    .load_percentage = 100,
    .voltage_light_control = 5.0,
    .voltage_system = 12,
    .over_charge_voltage = 14.4,
    .over_charge_return_voltage = 13.8,
    .over_discharge_voltage = 9.2,
    .over_discharge_return_voltage = 10.8,
    .nominal_capacity = 310,
    .battery_type = 0x11, 
};

loadScheduleSettingPack load_schedule[SCHEDULE_SLOT_COUNT] = {
    {1, 10800, 100, 100}, // 3 h 25w
    {2, 32400, 56, 56}, // 7 h 14w
    {3, 7200, 100, 100},  // 2 h 25w
    {4, 0, 0, 0},
    {5, 0, 0, 0},
    {6, 0, 0, 0},
    {7, 0, 0, 0},
    {8, 0, 0, 0},
    {9, 0, 0, 0},
};
// ... (rest of the variables are unchanged) ...
wifiConfig myWiFi = {
    .ssid = "1729",   //lekise_solar
    .password = "88888888",//idealab2023
};

MqttConfig myMQTT = {
    .server = "smartsolar-th.com",
    .port = 1883,
    .username = "guest",
    .password = "LeKise&KMUTT",
    .client_name = "test3"
};
int loaded_tud = 0;
int loaded_pgr = 0;
bool integrated = true;
volatile bool upload_mode = false;
volatile bool taskDone = true;
volatile uint8_t s = 0;
volatile int TimeStartNewCurrent = 0;
volatile bool NewCurrent = false;