#pragma once

#include <stdint.h>

#define CONFIG_VERSION   2
#define CONFIG_NAMESPACE "lamp_cfg"

struct RuntimeConfig {
    uint8_t  config_version;

    uint8_t  bedtime_start_hour;
    uint8_t  bedtime_start_minute;
    uint16_t bedtime_duration_s;

    uint8_t  wake_start_hour;
    uint8_t  wake_start_minute;
    uint16_t wake_duration_s;

    uint16_t dark_threshold;
};

extern RuntimeConfig g_config;

void loadConfig();
void saveConfig();

void setBedtimeWindow(uint8_t startH, uint8_t startM, uint16_t durationS);
void setWakeWindow(   uint8_t startH, uint8_t startM, uint16_t durationS);
void setDarkThreshold(uint16_t value);
