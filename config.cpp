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
    g_config.radar_min_dist_cm    = 0;
    g_config.radar_max_dist_cm    = 0;
    g_config.mqtt_enabled         = 0;
    g_config.mqtt_port            = 1883;
    g_config.mqtt_host[0]         = 0;
    g_config.mqtt_user[0]         = 0;
    g_config.mqtt_pass[0]         = 0;
}

void loadConfig() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, true);

    uint8_t version = prefs.getUChar("cfg_ver", 0xFF);
    if (version == 0xFF) {
        prefs.end();
        Serial.println(F("cfg: no saved config, using defaults"));
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
    g_config.radar_min_dist_cm    = prefs.getUShort("min_dist", 0);
    g_config.radar_max_dist_cm    = prefs.getUShort("max_dist", 0);
    g_config.mqtt_enabled         = prefs.getUChar("m_en", 0);
    g_config.mqtt_port            = prefs.getUShort("m_port", 1883);

    String h = prefs.getString("m_host", "");
    strlcpy(g_config.mqtt_host, h.c_str(), sizeof(g_config.mqtt_host));
    String u = prefs.getString("m_user", "");
    strlcpy(g_config.mqtt_user, u.c_str(), sizeof(g_config.mqtt_user));
    String p = prefs.getString("m_pass", "");
    strlcpy(g_config.mqtt_pass, p.c_str(), sizeof(g_config.mqtt_pass));

    prefs.end();

    if (version != CONFIG_VERSION) {
        Serial.println(F("cfg: migrating config to new version"));
        saveConfig();
    } else {
        Serial.println(F("cfg: loaded from NVS"));
    }
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
    prefs.putUShort("min_dist", g_config.radar_min_dist_cm);
    prefs.putUShort("max_dist", g_config.radar_max_dist_cm);
    prefs.putUChar("m_en",     g_config.mqtt_enabled);
    prefs.putUShort("m_port",  g_config.mqtt_port);
    prefs.putString("m_host",  g_config.mqtt_host);
    prefs.putString("m_user",  g_config.mqtt_user);
    prefs.putString("m_pass",  g_config.mqtt_pass);

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

void setRadarMinDist(uint16_t cm) {
    if (cm > 600) {
        Serial.println(F("cfg: min distance out of range (0-600cm)"));
        return;
    }
    g_config.radar_min_dist_cm = cm;
    saveConfig();
    Serial.printf("cfg: radar min distance %ucm\n", cm);
}

void setRadarMaxDist(uint16_t cm) {
    if (cm > 600) {
        Serial.println(F("cfg: max distance out of range (0-600cm)"));
        return;
    }
    g_config.radar_max_dist_cm = cm;
    saveConfig();
    Serial.printf("cfg: radar max distance %ucm\n", cm);
}

void setMqttConfig(uint8_t enabled, const char *host, uint16_t port, const char *user, const char *pass) {    g_config.mqtt_enabled = enabled ? 1 : 0;
    g_config.mqtt_port    = (port == 0) ? 1883 : port;
    strlcpy(g_config.mqtt_host, host ? host : "", sizeof(g_config.mqtt_host));
    strlcpy(g_config.mqtt_user, user ? user : "", sizeof(g_config.mqtt_user));
    strlcpy(g_config.mqtt_pass, pass ? pass : "", sizeof(g_config.mqtt_pass));
    saveConfig();
    Serial.printf("cfg: mqtt %s %s:%u\n", enabled ? "on" : "off",
                  g_config.mqtt_host, g_config.mqtt_port);
}
