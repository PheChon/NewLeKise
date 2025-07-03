// FILE: model.h

#ifndef MODEL_H
#define MODEL_H

#include "config.h"

float exponentialSmoothingWithTrend(uint32_t charge_wh);
void setChargingProfile(const timeDataPack &time_data, chargingProfilePack *charge_profile);
void chargingProfileAdjustment(float forecast_charge_wh, chargingProfilePack *charge_profile, const batteryDataPack &battery_data, const loadDataPack &load_data);

#endif // MODEL_H