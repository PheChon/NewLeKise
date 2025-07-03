// FILE: timerRoutine.h

#ifndef TIMER_ROUTINE_H
#define TIMER_ROUTINE_H

#include "config.h"
#include "hal_srne.h"
#include "hal_rtc.h"
#include "model.h"
#include "connectivity_ota.h"

void slot_1_update_data();
void slot_2_safety_checks();
void slot_3_forecasting_and_adjustment();
void slot_4_integration_check();
void slot_5_publish_data();

#endif // TIMER_ROUTINE_H