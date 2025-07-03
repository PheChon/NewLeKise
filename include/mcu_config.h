// FILE: mcu_config.h

#ifndef MCU_CONFIG_H
#define MCU_CONFIG_H

#include "config.h"

void setupMCU();
esp_err_t setupTimer(float time_s);
void setupExternalInterrupt(uint8_t external_intterupt_pin, uint8_t tx_pin, uint8_t rx_pin);

#endif // MCU_CONFIG_H