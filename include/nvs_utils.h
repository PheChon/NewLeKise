// FILE: nvs_utils.h

#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include "config.h"

void initNVS(const char *keyname);
void eraseAllNVS();
void saveIntToNVS(const char *main_key, const char *backup_key, int value);
int loadIntFromNVS(const char *main_key, const char *backup_key, int default_value);

#endif // NVS_UTILS_H