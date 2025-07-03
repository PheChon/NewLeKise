// FILE: upload_command.h

#ifndef UPLOAD_COMMAND_H
#define UPLOAD_COMMAND_H

#include "config.h"
#include "nvs_utils.h"
#include "hal_srne.h"

void resetListenerTask(void *parameter);
esp_err_t updateTime(uint16_t *time_package); // This seems unused, but keeping declaration

#endif // UPLOAD_COMMAND_H