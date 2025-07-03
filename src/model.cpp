// FILE: model.cpp

#include "model.h"

float exponentialSmoothingWithTrend(uint32_t charge_wh)
{
    // Holt's Linear Trend Method
    const float alpha = 0.332437; // Smoothing factor for the level
    const float beta = 0.998839;  // Smoothing factor for the trend

    static float smoothed = 0;
    static float trend = 0;
    static bool initialized = false;

    if (!initialized)
    {
        smoothed = (float)charge_wh;
        trend = 0; // Assume no initial trend
        initialized = true;
        return smoothed;
    }

    float old_smoothed = smoothed;
    smoothed = alpha * (float)charge_wh + (1 - alpha) * (old_smoothed + trend);
    trend = beta * (smoothed - old_smoothed) + (1 - beta) * trend;

    // Return the forecast for the next period
    return smoothed + trend;
}

void setChargingProfile(const timeDataPack &time_data, chargingProfilePack *charge_profile)
{
    // Profiles based on Thai seasons
    const chargingProfilePack summer_profile = {1, 3.2, 95, 5, 65, 75}; // Mar-May
    const chargingProfilePack rainy_profile = {2, 3.3, 95, 5, 55, 65};  // Jun-Oct
    const chargingProfilePack winter_profile = {3, 3.4, 95, 5, 65, 75}; // Nov-Feb

    if (time_data.month >= 3 && time_data.month <= 5)
    {
        *charge_profile = summer_profile;
        Serial.println("☀️ Set charging profile to: Summer");
    }
    else if (time_data.month >= 6 && time_data.month <= 10)
    {
        *charge_profile = rainy_profile;
        Serial.println("🌧️ Set charging profile to: Rainy");
    }
    else
    {
        *charge_profile = winter_profile;
        Serial.println("❄️ Set charging profile to: Winter");
    }
}

void chargingProfileAdjustment(float forecast_charge_wh, chargingProfilePack *charge_profile, const batteryDataPack &battery_data, const loadDataPack &load_data)
{
    static bool first_run = true;
    if (first_run)
    {
        first_run = false;
        return;
    }

    // Energy balance check
    float forecast_balance = forecast_charge_wh - load_data.load_wh;
    float previous_day_balance = (float)battery_data.last_charge_wh - (float)load_data.last_load_wh;

    float original_current = charge_profile->max_charge_current;

    if (forecast_balance > 0 && previous_day_balance > 0)
    {
        // Consistent energy surplus, can reduce charging current slightly
        charge_profile->max_charge_current -= 0.05;
    }
    else if (forecast_balance <= 0 && previous_day_balance <= 0)
    {
        // Consistent energy deficit, need to increase charging current more aggressively
        charge_profile->max_charge_current += 0.2;
    }
    else
    {
        // Fluctuating conditions, increase charging current moderately
        charge_profile->max_charge_current += 0.15;
    }

    // Clamp the value to a safe range
    if (charge_profile->max_charge_current < 1.0) charge_profile->max_charge_current = 1.0;
    if (charge_profile->max_charge_current > 3.85) charge_profile->max_charge_current = 3.85;

    if (abs(charge_profile->max_charge_current - original_current) > 0.01) {
        Serial.printf("🔌 Charging current adjusted from %.2fA to %.2fA based on forecast.\n", original_current, charge_profile->max_charge_current);
    }
}