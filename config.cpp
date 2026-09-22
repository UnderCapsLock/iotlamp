#include "config.h"
#include <Arduino.h>
#include <Preferences.h>

RuntimeConfig g_config;

static void applyDefaults() {
    g_config.config_version       = CONFIG_VERSION;
    g_config.bedtime_start_hour   = 22;
    g_config.bedtime_start_minute = 0;
    g_config.bedtime_duration_s   = 3600;
    g_config.wake_start_hour      = 6;
    g_config.wake_start_minute    = 0;
    g_config.wake_duration_s      = 1800;
    g_config.dark_threshold       = 550;
    g_config.presence_hold_s      = 6;
    g_config.presence_lost        = 0;
}

void loadConfig() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, true);

    uint8_t version = prefs.getUChar("cfg_ver", 0xFF);
    if (version != CONFIG_VERSION) {
        prefs.end();
        Serial.println(F("cfg: no saved config or version mismatch, using defaults"));
        applyDefaults();
        saveConfig();
        return;
    }

    g_config.config_version       = version;
    g_config.bedtime_start_hour   = prefs.getUChar("bs_h", 22);
    g_config.bedtime_start_minute = prefs.getUChar("bs_m", 0);
    g_config.bedtime_duration_s   = prefs.getUShort("bs_dur", 3600);
    g_config.wake_start_hour      = prefs.getUChar("ws_h", 6);
    g_config.wake_start_minute    = prefs.getUChar("ws_m", 0);
    g_config.wake_duration_s      = prefs.getUShort("ws_dur", 1800);
    g_config.dark_threshold       = prefs.getUShort("dark", 550);
    g_config.presence_hold_s      = prefs.getUShort("ph_s", 6);
    g_config.presence_lost        = prefs.getUChar("pl_act", 0);

    prefs.end();

    Serial.println(F("cfg: loaded from NVS"));
}

void saveConfig() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, false);

    prefs.putUChar("cfg_ver",  CONFIG_VERSION);
    prefs.putUChar("bs_h",     g_config.bedtime_start_hour);
    prefs.putUChar("bs_m",     g_config.bedtime_start_minute);
    prefs.putUShort("bs_dur",  g_config.bedtime_duration_s);
    prefs.putUChar("ws_h",     g_config.wake_start_hour);
    prefs.putUChar("ws_m",     g_config.wake_start_minute);
    prefs.putUShort("ws_dur",  g_config.wake_duration_s);
    prefs.putUShort("dark",    g_config.dark_threshold);
    prefs.putUShort("ph_s",    g_config.presence_hold_s);
    prefs.putUChar("pl_act",   g_config.presence_lost);

    prefs.end();

    Serial.println(F("cfg: saved to NVS"));
}

// --- setters ---

void setBedtimeWindow(uint8_t startH, uint8_t startM, uint16_t durationS) {
    if (startH > 23 || startM > 59 || durationS == 0 || durationS > 43200) {
        Serial.println(F("cfg: bedtime window out of range"));
        return;
    }
    g_config.bedtime_start_hour   = startH;
    g_config.bedtime_start_minute = startM;
    g_config.bedtime_duration_s   = durationS;
    saveConfig();
    Serial.printf("cfg: bedtime window %02d:%02d + %us\n", startH, startM, durationS);
}

void setWakeWindow(uint8_t startH, uint8_t startM, uint16_t durationS) {
    if (startH > 23 || startM > 59 || durationS == 0 || durationS > 43200) {
        Serial.println(F("cfg: wake window out of range"));
        return;
    }
    g_config.wake_start_hour   = startH;
    g_config.wake_start_minute = startM;
    g_config.wake_duration_s   = durationS;
    saveConfig();
    Serial.printf("cfg: wake window %02d:%02d + %us\n", startH, startM, durationS);
}

void setDarkThreshold(uint16_t value) {
    if (value > 4095) {
        Serial.println(F("cfg: dark threshold out of range (0-4095)"));
        return;
    }
    g_config.dark_threshold = value;
    saveConfig();
    Serial.printf("cfg: dark threshold set to %u\n", value);
}

void setPresenceBehavior(uint16_t holdS, uint8_t lostAction) {
    if (holdS < 1 || holdS > 300) {
        Serial.println(F("cfg: presence hold out of range (1-300s)"));
        return;
    }
    g_config.presence_hold_s = holdS;
    g_config.presence_lost   = (lostAction > 1) ? 1 : lostAction;
    saveConfig();
    Serial.printf("cfg: presence hold %us, lost action %u\n", holdS, lostAction);
}
