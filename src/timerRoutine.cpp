// FILE: timerRoutine.cpp

#include "timerRoutine.h"

void slot_1_update_data()
{
    esp_task_wdt_reset();
    static bool profile_updated_today = false;
    static uint8_t last_day_checked = 0;

    Serial.println("========== Slot 1: Data Acquisition & Profile Setup ==========");

    getCurrentTime(&time_data);

    Serial.println("Current Time:");
    Serial.printf("  %02d/%02d/%04d %02d:%02d:%02d\n",
                  time_data.day, time_data.month, time_data.year,
                  time_data.hour, time_data.minute, time_data.second);
    Serial.println("------------------------------------------------------------");

    if (getLoadInfo(&load_data) == ESP_OK && getLoadWh(&load_data) == ESP_OK)
    {
        Serial.println("Load Data:");
        Serial.printf("  Voltage                : %.2f V\n", load_data.load_voltage);
        Serial.printf("  Current                : %.2f A\n", load_data.load_current);
        Serial.printf("  Power                  : %d W\n", load_data.load_power);
        Serial.println("------------------------------------------------------------");
    }

    if (getSolarInfo(&solar_data) == ESP_OK)
    {
        Serial.println("------------------------------------------------------------");
        Serial.println("Solar Data:");
        Serial.printf("  Voltage                : %.2f V\n", solar_data.solar_voltage);
        Serial.printf("  Current                : %.2f A\n", solar_data.solar_current);
        Serial.printf("  Power                  : %d W\n", solar_data.solar_power);
        Serial.println("------------------------------------------------------------");
    }

    // --- แก้ไขเงื่อนไขและเพิ่มการแสดงผล ---
    if (getBatteryInfo(&battery_data) == ESP_OK && getChargeWh(&battery_data) == ESP_OK && updateEstimatedSOC(&battery_data) == ESP_OK && get_energy(&battery_data) == ESP_OK)
    {
        Serial.println("------------------------------------------------------------");
        Serial.println("Battery Data:");
        Serial.printf("  Voltage                : %.2f V\n", battery_data.battery_voltage);
        Serial.printf("  Current                : %.2f A\n", battery_data.battery_current);
        Serial.printf("  SOC (From Controller)  : %d %%\n", battery_data.battery_soc);
        Serial.printf("  SOC (Estimated)        : %.1f %%\n", battery_data.battery_soc_estimated);
        Serial.printf("  Temp                   : %d °C\n", battery_data.battery_temperature);
        Serial.printf("  Total Charge (Wh)      : %lu Wh\n", battery_data.charge_wh); // Address 0x011C
        // +++ START: เพิ่มการแสดงผลค่าใหม่ +++
        Serial.printf("  Total Charge (Ah)      : %lu Ah\n", battery_data.total_charge_ah); // Address 0x0118
        Serial.printf("  Total Discharge (Ah)   : %lu Ah\n", battery_data.total_discharge_ah); // Address 0x011A
        // +++ END: เพิ่มการแสดงผลค่าใหม่ +++
        Serial.println("-----------------------------------------------------------");
    }

    // --- Handle Profile Update Logic ---
    if (time_data.day != last_day_checked) {
        profile_updated_today = false;
        last_day_checked = time_data.day;
    }

    bool is_profile_time = (time_data.hour == 5 && time_data.minute >= 30 && time_data.minute <= 35);
    if (is_profile_time && !profile_updated_today)
    {
        setChargingProfile(time_data, &charge_profile);
        if (integrated) {
            setMaxChargeCurrent(charge_profile.max_charge_current);
        }
        profile_updated_today = true;
    }
}

void slot_2_safety_checks()
{
    esp_task_wdt_reset();
    static bool soc_max_reached = false;
    static bool soc_min_reached = false; // Currently unused logic
    static bool check_11_done = false;
    static bool check_12_done = false;

    Serial.println("========== Slot 2: Real-time Safety & SOC Checks =============");

    bool is_day_time = (time_data.hour >= 6 && time_data.hour <= 17);
    float last_max_charge_current = charge_profile.max_charge_current;

    // Safety: If SOC is full during the day, stop charging
    if ( is_day_time && battery_data.battery_voltage >= 13.9 && !soc_max_reached && integrated)//is_day_time && battery_data.battery_voltage >= 13.9 && !soc_max_reached && integrated
    {
        if (setMaxChargeCurrent(0.0) == ESP_OK) {
            soc_max_reached = true;
            Serial.println("SOC Max reached. Charging stopped.");
        }
    }
    else if (!is_day_time && soc_max_reached) {
        soc_max_reached = false; // Reset for the next day
    }

    // Mid-day SOC check to boost charging if needed
    bool is_rainy_season = (charge_profile.profile_number == 2);
    if (time_data.hour == 11 && !check_11_done)
    {
        if (battery_data.battery_soc < charge_profile.safe_soc_1) {
            charge_profile.max_charge_current = is_rainy_season ? 3.85 : charge_profile.max_charge_current + 0.2;
        }
        check_11_done = true;
    }
    else if (time_data.hour == 12 && !check_12_done)
    {
        if (battery_data.battery_soc < charge_profile.safe_soc_2) {
            charge_profile.max_charge_current = is_rainy_season ? 3.85 : charge_profile.max_charge_current + 0.2;
        }
        check_12_done = true;
    }
    else if (time_data.hour >= 18) // Reset checks for the next day
    {
        check_11_done = false;
        check_12_done = false;
    }

    // Apply any current changes from the mid-day check
    if (is_day_time && abs(charge_profile.max_charge_current - last_max_charge_current) > 0.01 && integrated)
    {
        setMaxChargeCurrent(charge_profile.max_charge_current);
        Serial.printf("Mid-day SOC boost. Set charge current to %.2fA\n", charge_profile.max_charge_current);
    }
}

void slot_3_forecasting_and_adjustment()
{
    esp_task_wdt_reset();
    static bool charge_wh_captured = false;
    static bool load_wh_captured = false;
    static bool first_forecast_run = true;
    static bool overheated_mode = false;
    static float current_before_overheat = 0;

    Serial.println("========== Slot 3: Forecasting & Thermal Management ==========");

    bool is_night = (time_data.hour >= 20 || time_data.hour < 6);
    bool is_day = !is_night;

    if (is_night && load_data.load_current > 0.1 && !charge_wh_captured)
    {
        getChargeWh(&battery_data);
        battery_data.last_charge_wh = battery_data.charge_wh;
        charge_wh_captured = true;
    }

    if (is_day && load_data.load_current < 0.1 && charge_wh_captured && !load_wh_captured)
    {
        getLoadWh(&load_data);
        load_data.last_load_wh = load_data.load_wh;
        load_wh_captured = true;
    }

    if (charge_wh_captured && load_wh_captured)
    {
        if (first_forecast_run) {
            first_forecast_run = false;
        } else {
            float forecasted_charge_wh = exponentialSmoothingWithTrend(battery_data.charge_wh);
            chargingProfileAdjustment(forecasted_charge_wh, &charge_profile, battery_data, load_data);
            if (integrated) {
                setMaxChargeCurrent(charge_profile.max_charge_current);
            }
        }
        clearAccumulateData();
        charge_wh_captured = false;
        load_wh_captured = false;
    }

    bool is_peak_temp_time = (time_data.hour >= 12 && time_data.hour <= 15);
    bool is_soc_high = (battery_data.battery_soc >= (charge_profile.max_soc - 15));
    if (is_peak_temp_time && is_soc_high && battery_data.battery_temperature >= 65 && !overheated_mode && integrated)
    {
        overheated_mode = true;
        current_before_overheat = charge_profile.max_charge_current;
        if (setMaxChargeCurrent(2.5) == ESP_OK) {
             Serial.println("🌡️ Overheat protection triggered. Reducing charge current.");
        }
    }
    else if (is_day && overheated_mode && battery_data.battery_temperature < 60 && integrated)
    {
        overheated_mode = false;
        if (setMaxChargeCurrent(current_before_overheat) == ESP_OK) {
            Serial.println("🌡️ Temperature normal. Restoring charge current.");
        }
    }
}

void slot_4_integration_check()
{
    esp_task_wdt_reset();
    static bool soc_recharged = false;
    Serial.println("========== Slot 4: System Integration Check ==================");

    if (!integrated) {
        bool is_day_time = (time_data.hour >= 6 && time_data.hour <= 17);
        bool is_off_time = (time_data.hour >= 5 && time_data.hour <= 7);

        if (is_day_time && battery_data.battery_soc >= charge_profile.max_soc) {
            soc_recharged = true;
        }

        if (is_off_time && soc_recharged && battery_data.battery_soc >= charge_profile.min_soc && load_data.load_voltage < 10) {
            integrated = true;
            soc_recharged = false;
            Serial.println("✅ System integration re-enabled.");
        }
    }
}

void slot_5_publish_data()
{
    esp_task_wdt_reset();
    Serial.println("========== Slot 5: Publishing Data ===========================");
    const char *publish_topic = "test/data/up1";

    if (publishData(load_data, solar_data, battery_data, time_data, publish_topic) == ESP_OK)
    {
        Serial.println("✅ MQTT Publish Successful");
    } else {
        Serial.println("❌ MQTT Publish Failed");
    }
}

// void slot_6_Load_Control()
// {
//     esp_task_wdt_reset();
//     Serial.println("========== Slot 6: Load Control & Energy Calculation ==========");

//     // ใช้ static variables เพื่อเก็บค่าสถานะระหว่างการเรียกใช้ฟังก์ชัน
//     static float initial_SOC = 0, final_SOC = 0;
//     static float initial_SOC_E = 0, final_SOC_E = 0;
//     static uint32_t initial_ChangeWh = 0, final_ChangeWh = 0;

//     // State machine: 0 = รอเริ่มต้นวัน (กลางคืน), 1 = รอสิ้นสุดวัน (กลางวัน)
//     static int calculation_state = 0;

//     // +++ START: เพิ่ม Code สำหรับ Debug +++
//     Serial.printf("[DEBUG] Current State: %d, Solar Voltage: %.2fV\n", calculation_state, solar_data.solar_voltage);
//     // +++ END: เพิ่ม Code สำหรับ Debug +++

//     // State 0: รอให้พระอาทิตย์ขึ้น (solar_voltage >= 10V)
//     if (calculation_state == 0 && solar_data.solar_voltage >= 10.0)
//     {
//         Serial.println("[SLOT 6] Day started. Capturing initial SOC and Wh.");
//         initial_SOC = battery_data.battery_soc;
//         initial_SOC_E = battery_data.battery_soc_estimated;
//         getChargeWh(&battery_data); // อ่านค่า charge_wh ล่าสุด
//         initial_ChangeWh = battery_data.charge_wh;

//         // +++ START: เพิ่ม Code สำหรับ Debug +++
//         Serial.printf("[DEBUG] Initial values captured: SOC=%.0f, SOC_E=%.1f, ChargeWh=%u\n", initial_SOC, initial_SOC_E, initial_ChangeWh);
//         // +++ END: เพิ่ม Code สำหรับ Debug +++

//         calculation_state = 1; // เปลี่ยน state เป็น "รอสิ้นสุดวัน"
//     }
//     // State 1: รอให้พระอาทิตย์ตก (solar_voltage < 10V)
//     else if (calculation_state == 1 && solar_data.solar_voltage < 10.0)
//     {
//         Serial.println("[SLOT 6] Day ended. Capturing final state and calculating.");
//         final_SOC = battery_data.battery_soc;
//         final_SOC_E = battery_data.battery_soc_estimated;
//         getChargeWh(&battery_data); // อ่านค่า charge_wh ล่าสุด
//         final_ChangeWh = battery_data.charge_wh;

//         float Gain_SOC = final_SOC - initial_SOC;
//         float Gain_SOC_E = final_SOC_E - initial_SOC_E;
//         uint32_t Gain_ChangeWh = final_ChangeWh - initial_ChangeWh;

//         // +++ START: เพิ่ม Code สำหรับ Debug +++
//         Serial.println("--- Values for Calculation Check ---");
//         Serial.printf("Final SOC: %.0f, Initial SOC: %.0f, Gain_SOC: %.1f\n", final_SOC, initial_SOC, Gain_SOC);
//         Serial.printf("Final SOC_E: %.1f, Initial SOC_E: %.1f, Gain_SOC_E: %.2f\n", final_SOC_E, initial_SOC_E, Gain_SOC_E);
//         Serial.println("------------------------------------");
//         // +++ END: เพิ่ม Code สำหรับ Debug +++


//         // ป้องกันการหารด้วยศูนย์ ถ้า SOC ไม่มีการเปลี่ยนแปลง
//         if (Gain_SOC > 0 && Gain_SOC_E > 0)
//         {
//             // คำนวณหาว่า 1% SOC เทียบเท่ากับกี่ Wh
//             float Wh_per_SOC = (float)Gain_ChangeWh / Gain_SOC;
//             float Wh_per_SOC_E = (float)Gain_ChangeWh / Gain_SOC_E;

//             // --- เก็บค่าที่คำนวณได้ลงตัวแปรส่วนกลาง ---
//             Full_Wh = Wh_per_SOC * 100.0;
//             Full_Wh_E = Wh_per_SOC_E * 100.0;

//             current_energy = (Full_Wh * final_SOC) / 100.0;
//             current_energy_E = (Full_Wh_E * final_SOC_E) / 100.0;

//             New_Wh = current_energy - (75.0 + 50.0);
//             New_Wh_E = current_energy_E - (75.0 + 50.0);

//             if (New_Wh < 0) New_Wh = 0;
//             if (New_Wh_E < 0) New_Wh_E = 0;

//             Serial.printf("[SLOT 6] Energy Gained: %u Wh\n", Gain_ChangeWh);
//             Serial.printf("[SLOT 6] SOC Gained: %.1f %%\n", Gain_SOC);
//             Serial.printf("[SLOT 6] Wh per 1%% SOC: %.2f Wh\n", Wh_per_SOC);
//             Serial.printf("[SLOT 6] Calculated Total Capacity: %.2f Wh\n", Full_Wh);
//             Serial.printf("[SLOT 6] Current Energy: %.2f Wh\n", current_energy);
//             Serial.printf("[SLOT 6] New Target Wh for night: %.2f Wh\n", New_Wh);
//         }
//         else
//         {
//             Serial.println("[SLOT 6] Calculation skipped: SOC did not increase.");
//         }

//         calculation_state = 0; // รีเซ็ต State machine เพื่อรอวันถัดไป
//     }
// }