#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>
#include "pins.h"
#include "state.h"
#include "wifi_config.h"
#include "MyLD2410.h"
#include "config.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LittleFS.h>

// #define RADAR_DEBUG 1

// ---------- brightness / color ----------

const uint8_t  DIM_FLOOR           = 10;
const uint16_t DIM_FLOOR_CCT       = 2200;
const uint16_t WAKE_PEAK_CCT      = 5000;
const uint16_t SLEEP_DIM_RAMP_S   = 30;
const uint16_t LDR_HYSTERESIS     = 200;
const unsigned long PRESENCE_HOLD_MS = 6000;

// ---------- defaults ----------

const uint8_t  FULL_BRIGHTNESS = 255;
const uint16_t DEFAULT_CCT     = 2700;
#define FW_VERSION "1.1.0"

// ---------- globals ----------

HardwareSerial radarSerial(2);
MyLD2410      radar(radarSerial);

CRGB leds[LED_COUNT];

LampState g_state = {
    LampMode::AUTO,
    Presence::NONE,
    false,
    0,
    DEFAULT_CCT,
    0,
    0.0f,
};

// ---------- time ----------

struct tm    g_timeinfo;
bool         g_time_synced    = false;
unsigned long g_last_ntp_sync   = 0;
unsigned long g_last_ntp_attempt = 0;

// ---------- ldr rolling average ----------

static const uint8_t LDR_WINDOW = 8;

uint16_t ldr_buf[LDR_WINDOW];
uint8_t  ldr_i    = 0;
uint32_t ldr_sum  = 0;
unsigned long g_last_ldr = 0;

// ---------- presence debounce ----------

static const uint8_t DEBOUNCE = 3;

Presence      presence_hist[DEBOUNCE];
uint8_t       presence_i   = 0;
Presence      confirmed     = Presence::NONE;
unsigned long g_last_radar  = 0;
bool          g_radar_online = false;
unsigned long g_last_radar_reinit = 0;
uint8_t       g_radar_rx_pin = LD2410_RX_PIN;
uint8_t       g_radar_tx_pin = LD2410_TX_PIN;
unsigned long g_radar_last_frame_ms = 0;
unsigned long g_radar_last_fc = 0;
unsigned long g_radar_poll_fc = 0;
unsigned long g_presence_last_seen = 0;

unsigned long g_radar_rx_bytes = 0;
uint8_t       g_radar_probe[32];
uint8_t       g_radar_probe_n = 0;
bool          g_radar_probe_printed = false;

// ---------- led ----------

unsigned long g_last_led = 0;

// ---------- heartbeat ----------

unsigned long g_last_hb = 0;
unsigned long g_last_ws = 0;

// ---------- websocket ----------

AsyncWebServer server(80);
AsyncWebSocket wsSock("/ws");

// ---------- manual overrides ----------

uint8_t  g_manual_brightness = 255;
uint16_t g_manual_cct        = 2700;
bool     g_manual_bri_active = false;
bool     g_manual_cct_active = false;
bool     g_rgb_active        = false;
uint8_t  g_rgb_r = 255, g_rgb_g = 255, g_rgb_b = 255;
unsigned long g_last_energy_save = 0;

// ---------- sleep timer ----------

uint32_t      g_sleep_timer_s     = 0;
bool          g_sleep_dimming     = false;
unsigned long g_sleep_dim_start   = 0;
uint8_t       g_sleep_start_bri   = 255;
unsigned long g_sleep_timer_start = 0;
uint32_t      g_sleep_timer_total = 0;

// ---------- cct -> rgb (Tanner Helland) ----------

CRGB kelvin_to_rgb(uint16_t kelvin) {
    float t = kelvin / 100.0f;

    uint8_t r, g, b;

    if (t <= 66) r = 255;
    else {
        r = (uint8_t)constrain(329.698727446f * powf(t - 60, -0.1332047592f), 0.0f, 255.0f);
        g = (uint8_t)constrain(288.1221695283f * powf(t - 60, -0.0755148492f), 0.0f, 255.0f);
    }

    if (t <= 66) {
        g = (uint8_t)constrain(99.4708025861f * logf(t) - 161.1195681661f, 0.0f, 255.0f);
        if (t <= 19) b = 0;
        else b = (uint8_t)constrain(138.5177312231f * logf(t - 10) - 305.0447927307f, 0.0f, 255.0f);
    }

    if (t > 66) b = 255;

    return CRGB(r, g, b);
}

// ---------- easing ----------

static inline float ease_in_cubic(float t)  { return t * t * t; }
static inline float ease_out_cubic(float t) { float f = t - 1; return f * f * f + 1; }

// ---------- forward decls ----------

void wifi_connect();
void sync_ntp();
void read_ldr();
void poll_radar();
void debounce_radar();
void compute_output();
void write_leds();
void heartbeat();
void wsBroadcast();
void wsSendState(AsyncWebSocketClient *client);
void wsHandleCommand(uint8_t *data, size_t len, AsyncWebSocketClient *client);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
bool isInScheduleWindow();
void setLampMode(LampMode mode);
const char *presenceStr(Presence p);
const char *modeStr(LampMode m);
void saveEnergy();
void loadEnergy();

// ================================================================

static bool ota_in_progress = false;

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println(F("\n=== LightPlus ==="));

    FastLED.addLeds<LED_TYPE, LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
    FastLED.setBrightness(0);
    FastLED.clear(true);

    WiFi.mode(WIFI_STA);
    wifi_country_t country = {};
    memcpy(country.cc, "MY", 3);
    country.schan = 1;
    country.nchan = 13;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);
    WiFi.setAutoReconnect(true);
    wifi_connect();

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    radarSerial.begin(256000, SERIAL_8N1, LD2410_RX_PIN, LD2410_TX_PIN);
    g_radar_online = radar.begin();
    Serial.printf("radar: begin %s\n", g_radar_online ? "online" : "OFFLINE");
    pinMode(LD2410_OUT, INPUT_PULLDOWN);
#ifdef RADAR_DEBUG
    radar.debugOn();
#endif

    analogReadResolution(12);

    loadConfig();
    loadEnergy();

    delay(2000);
    sync_ntp();

    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() {
        ota_in_progress = true;
        FastLED.clear(true);
        FastLED.show();
        Serial.println(F("OTA: start"));
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        static unsigned int last = 0;
        unsigned int pct = (p * 100) / t;
        if (pct != last) { last = pct; Serial.printf("OTA: %u%%\n", pct); }
    });
    ArduinoOTA.onEnd([]() {
        ota_in_progress = false;
        Serial.println(F("OTA: end"));
    });
    ArduinoOTA.onError([](ota_error_t e) {
        const char *msg;
        switch (e) {
            case OTA_AUTH_ERROR:    msg = "auth"; break;
            case OTA_BEGIN_ERROR:   msg = "begin"; break;
            case OTA_CONNECT_ERROR: msg = "connect"; break;
            case OTA_RECEIVE_ERROR: msg = "receive"; break;
            case OTA_END_ERROR:     msg = "end"; break;
            default:                msg = "unknown"; break;
        }
        ota_in_progress = false;
        Serial.printf("OTA: error [%u] %s\n", e, msg);
    });

    ArduinoOTA.setTimeout(120000);
    ArduinoOTA.begin();

    if (LittleFS.begin(true)) {
        Serial.println(F("LittleFS mounted"));
    } else {
        Serial.println(F("LittleFS mount failed"));
    }

    wsSock.onEvent(onWsEvent);
    server.addHandler(&wsSock);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();

    Serial.print(F("WebSocket server started on ws://"));
    Serial.print(WiFi.localIP());
    Serial.println(F("/ws"));

    Serial.println(F("Ready."));
}

// ================================================================

void loop() {
    ArduinoOTA.handle();
    if (ota_in_progress) {
        return;
    }
    unsigned long now = millis();

    radarScanPins();

    if (WiFi.status() != WL_CONNECTED)
        wifi_connect();

    if (!g_time_synced)
        sync_ntp();

    if (g_time_synced && now - g_last_ntp_sync >= NTP_INTERVAL_S * 1000UL)
        sync_ntp();

    if (g_sleep_timer_s > 0) {
        uint32_t elapsed = (millis() - g_sleep_timer_start) / 1000;
        if (elapsed >= g_sleep_timer_total) {
            g_sleep_timer_s = 0;
            g_sleep_dimming = true;
            g_sleep_dim_start = millis();
            g_sleep_start_bri = g_state.brightness;
            Serial.println(F("sleep: timer expired, starting dim"));
        } else {
            g_sleep_timer_s = g_sleep_timer_total - elapsed;
        }
    }

    if (now - g_last_ldr >= LDR_INTERVAL) {
        read_ldr();
        g_last_ldr = now;
    }

    if (now - g_last_radar >= RADAR_INTERVAL) {
        poll_radar();
        g_last_radar = now;
    }

    unsigned long fc = radar.getFrameCount();
    if (fc != g_radar_last_fc) {
        g_radar_last_fc = fc;
        g_radar_last_frame_ms = now;
    }

    if (g_radar_online && now - g_radar_last_frame_ms >= 5000UL) {
        Serial.println(F("radar: stream stalled 5s, restarting"));
        g_radar_online = false;
    }

    if (!g_radar_online && now - g_last_radar_reinit >= 2000UL) {
        g_last_radar_reinit = now;
        radarSerial.end();
        delay(50);
        radarSerial.begin(256000, SERIAL_8N1, g_radar_rx_pin, g_radar_tx_pin);
        g_radar_online = radar.begin();
        g_radar_last_frame_ms = millis();
        Serial.printf("radar: reinit %s\n", g_radar_online ? "online" : "still offline");
    }

    compute_output();

    if (now - g_last_led >= LED_INTERVAL) {
        write_leds();
        if (g_state.brightness > 0) {
            float hours = LED_INTERVAL / 3600000.0f;
            float kw = (g_state.brightness / 255.0f) * 0.003f;
            g_state.total_kwh += kw * hours;
        }
        g_last_led = now;
    }

    if (now - g_last_hb >= HEARTBEAT_INTERVAL) {
        heartbeat();
        g_last_hb = now;
    }

    if (now - g_last_ws >= WS_BROADCAST_INTERVAL) {
        wsBroadcast();
        g_last_ws = now;
    }

    ArduinoOTA.handle();

    if (now - g_last_energy_save >= 300000UL) {
        saveEnergy();
        g_last_energy_save = now;
    }
}

// ================================================================
//  WiFi
// ================================================================

void wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print(F("WiFi "));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000UL) {
        delay(400);
        Serial.print(F("."));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F(" ok"));
        Serial.print(F("  IP ")); Serial.println(WiFi.localIP());
    } else {
        Serial.println(F(" fail, retrying"));
    }
}

// ================================================================
//  NTP
// ================================================================

void sync_ntp() {
    unsigned long now = millis();
    if (now - g_last_ntp_attempt < 10000UL) return;
    g_last_ntp_attempt = now;

    if (!getLocalTime(&g_timeinfo)) {
        Serial.println(F("NTP miss"));
        g_time_synced  = false;
        g_last_ntp_sync = 0;
        return;
    }

    g_time_synced    = true;
    g_last_ntp_sync  = now;

    Serial.printf("NTP  %02d:%02d:%02d\n",
                  g_timeinfo.tm_hour, g_timeinfo.tm_min, g_timeinfo.tm_sec);
}

// ================================================================
//  LDR
// ================================================================

void read_ldr() {
    uint16_t raw = analogRead(LDR_PIN);

    ldr_sum -= ldr_buf[ldr_i];
    ldr_buf[ldr_i] = raw;
    ldr_sum += raw;
    ldr_i = (ldr_i + 1) % LDR_WINDOW;

    g_state.ldr_raw = ldr_sum / LDR_WINDOW;
    uint16_t thr = g_config.dark_threshold;
    if (g_state.is_dark)
        g_state.is_dark = g_state.ldr_raw < (uint16_t)(thr + LDR_HYSTERESIS);
    else
        g_state.is_dark = g_state.ldr_raw < thr;
}

// ================================================================
//  LD2410C
// ================================================================

static bool g_radar_scan_done = false;

void radarScanPins() {
    if (g_radar_scan_done) return;
    g_radar_scan_done = true;

    int bestPin = -1;
    unsigned long bestBytes = 0;

    const uint8_t scanPins[] = {LD2410_RX_PIN, LD2410_TX_PIN}; // 16, 17
    for (uint8_t i = 0; i < sizeof(scanPins); i++) {
        uint8_t rx = scanPins[i];
        uint8_t tx = scanPins[(i + 1) % 2];
        radarSerial.end();
        delay(50);
        radarSerial.begin(256000, SERIAL_8N1, rx, tx);
        unsigned long bytes = 0;
        uint8_t hex[32];
        uint8_t hn = 0;
        unsigned long t = millis() + 1000;
        while (millis() < t) {
            int a = radarSerial.available();
            if (a > 0) {
                bytes += a;
                while (a-- > 0) {
                    uint8_t b = radarSerial.read();
                    if (hn < sizeof(hex)) hex[hn++] = b;
                }
            }
            delay(10);
        }
        Serial.printf("radar scan: RX=GPIO%u -> %lu bytes", rx, bytes);
        if (hn) {
            Serial.print(F(" ["));
            for (uint8_t k = 0; k < hn; k++) Serial.printf("%02X ", hex[k]);
            Serial.print(F("]"));
        }
        Serial.println();
        if ((long)bytes > (long)bestBytes) {
            bestBytes = bytes;
            bestPin = rx;
        }
    }

    radarSerial.end();
    delay(50);

    int rxPin = LD2410_RX_PIN;
    int txPin = LD2410_TX_PIN;
    if (bestPin >= 0 && bestBytes >= 32 && bestPin != LD2410_RX_PIN) {
        rxPin = bestPin;
        txPin = LD2410_RX_PIN;
        Serial.printf("radar scan: using RX=GPIO%u TX=GPIO%u (non-standard wiring)\n", rxPin, txPin);
    } else {
        Serial.printf("radar scan: using RX=GPIO%u TX=GPIO%u\n", rxPin, txPin);
    }

    radarSerial.begin(256000, SERIAL_8N1, rxPin, txPin);
    g_radar_rx_pin = rxPin;
    g_radar_tx_pin = txPin;
    g_radar_online = radar.begin();
    g_radar_last_frame_ms = millis();
    Serial.printf("radar: begin %s\n", g_radar_online ? "online" : "OFFLINE");
}

void poll_radar() {
    int avail = radarSerial.available();
    g_radar_rx_bytes += avail;
    while (g_radar_probe_n < sizeof(g_radar_probe) && avail-- > 0)
        g_radar_probe[g_radar_probe_n++] = radarSerial.read();

    radar.check();

    unsigned long fc = radar.getFrameCount();
    bool fresh = (fc != g_radar_poll_fc);
    g_radar_poll_fc = fc;
    if (!fresh) return;

    Presence cur = Presence::NONE;
    if (radar.presenceDetected()) {
        if (radar.movingTargetDetected())      cur = Presence::MOVING;
        else if (radar.stationaryTargetDetected()) cur = Presence::STATIONARY;
    }

    presence_hist[presence_i] = cur;
    presence_i = (presence_i + 1) % DEBOUNCE;
}

void debounce_radar() {
    Presence p = presence_hist[0];
    for (uint8_t i = 1; i < DEBOUNCE; i++)
        if (presence_hist[i] != p) return;

    confirmed = p;
    g_state.presence = confirmed;
}

// ================================================================
//  Lamp logic
// ================================================================

void compute_output() {
    debounce_radar();

    // sleep dim overrides all mode logic during dimming phase
    if (g_sleep_dimming) {
        uint32_t elapsed_s = (millis() - g_sleep_dim_start) / 1000;
        if (elapsed_s >= SLEEP_DIM_RAMP_S) {
            g_sleep_dimming = false;
            g_manual_bri_active = false;
            g_manual_cct_active = false;
            g_rgb_active = false;
            setLampMode(LampMode::FORCE_OFF);
            g_state.brightness = 0;
            g_state.color_temp = DIM_FLOOR_CCT;
            Serial.println(F("sleep: dim complete"));
            return;
        }
        float p = ease_in_cubic((float)elapsed_s / SLEEP_DIM_RAMP_S);
        if (g_sleep_start_bri > 0)
            g_state.brightness = g_sleep_start_bri - (uint8_t)(g_sleep_start_bri * p);
        g_state.color_temp = DIM_FLOOR_CCT;
        return;
    }

    if (g_state.mode == LampMode::FORCE_ON) {
        g_state.brightness = g_manual_brightness;
        g_state.color_temp = g_manual_cct;
        return;
    }

    if (g_state.mode == LampMode::FORCE_OFF) {
        g_state.brightness = 0;
        return;
    }

    // AUTO
    if (confirmed != Presence::NONE)
        g_presence_last_seen = millis();

    bool presence_recent = confirmed != Presence::NONE ||
        (g_presence_last_seen != 0 && millis() - g_presence_last_seen < PRESENCE_HOLD_MS);

    bool should = g_state.is_dark && presence_recent;
    if (!should) {
        g_state.brightness = 0;
        g_manual_bri_active = false;
        g_manual_cct_active = false;
        g_rgb_active = false;
        return;
    }

    if (!g_time_synced) {
        g_state.brightness = FULL_BRIGHTNESS;
        g_state.color_temp = DEFAULT_CCT;
    } else {
        uint32_t now = g_timeinfo.tm_hour * 3600 + g_timeinfo.tm_min * 60 + g_timeinfo.tm_sec;
        uint32_t ws  = g_config.wake_start_hour * 3600 + g_config.wake_start_minute * 60;
        uint32_t bs  = g_config.bedtime_start_hour * 3600 + g_config.bedtime_start_minute * 60;
        uint32_t be  = (bs + g_config.bedtime_duration_s) % 86400;

        uint32_t e_ws = (now - ws + 86400) % 86400;
        uint32_t e_bs = (now - bs + 86400) % 86400;

        if (e_ws < g_config.wake_duration_s) {
            float p = (float)e_ws / g_config.wake_duration_s;
            g_state.brightness = DIM_FLOOR + (uint8_t)((FULL_BRIGHTNESS - DIM_FLOOR) * ease_out_cubic(p));
            g_state.color_temp = DIM_FLOOR_CCT + (uint16_t)((WAKE_PEAK_CCT - DIM_FLOOR_CCT) * p);
        } else if (e_bs < g_config.bedtime_duration_s) {
            float p = ease_in_cubic((float)e_bs / g_config.bedtime_duration_s);
            g_state.brightness = FULL_BRIGHTNESS - (uint8_t)((FULL_BRIGHTNESS - DIM_FLOOR) * p);
            g_state.color_temp = DEFAULT_CCT - (uint16_t)((DEFAULT_CCT - DIM_FLOOR_CCT) * p);
        } else {
            uint32_t gap = (ws - be + 86400) % 86400;
            uint32_t e_be = (now - be + 86400) % 86400;
            if (gap > 0 && e_be < gap) {
                g_state.brightness = DIM_FLOOR;
                g_state.color_temp = DIM_FLOOR_CCT;
            } else {
                g_state.brightness = FULL_BRIGHTNESS;
                g_state.color_temp = DEFAULT_CCT;
            }
        }
    }

    if (g_manual_bri_active) g_state.brightness = g_manual_brightness;
    if (g_manual_cct_active) g_state.color_temp = g_manual_cct;
}

// ================================================================
//  LED driver
// ================================================================

void write_leds() {
    static uint8_t dbg_tick = 0;
    if (g_state.brightness == 0) {
        FastLED.setBrightness(0);
        FastLED.clear(true);
        return;
    }

    FastLED.setBrightness(g_state.brightness);
    if (g_rgb_active) {
        if (++dbg_tick >= 30) { dbg_tick = 0;
            Serial.printf("DEBUG write_leds: RGB r=%u g=%u b=%u bri=%u\n", g_rgb_r, g_rgb_g, g_rgb_b, g_state.brightness);
        }
        fill_solid(leds, LED_COUNT, CRGB(g_rgb_r, g_rgb_g, g_rgb_b));
    } else {
        if (++dbg_tick >= 30) { dbg_tick = 0;
            Serial.printf("DEBUG write_leds: CCT cct=%u bri=%u\n", g_state.color_temp, g_state.brightness);
        }
        fill_solid(leds, LED_COUNT, kelvin_to_rgb(g_state.color_temp));
    }
    FastLED.show();
}

// ================================================================
//  Heartbeat (serial for now; replace with dashboard transport)
// ================================================================

void heartbeat() {
    Serial.print(F("[hb] m="));
    Serial.print((uint8_t)g_state.mode);
    Serial.print(F(" p="));
    Serial.print((uint8_t)g_state.presence);
    Serial.print(F(" dark="));
    Serial.print(g_state.is_dark);
    Serial.print(F(" bri="));
    Serial.print(g_state.brightness);
    Serial.print(F(" cct="));
    Serial.print(g_state.color_temp);
    Serial.print(F(" ldr="));
    Serial.print(g_state.ldr_raw);
    Serial.print(F(" dthr="));
    Serial.print(g_config.dark_threshold);
    Serial.print(F(" win="));
    Serial.print(isInScheduleWindow());
    Serial.print(F(" rf="));
    Serial.print(radar.getFrameCount());
    Serial.print(F(" rs="));
    Serial.print(radar.getStatus());
    Serial.print(F(" rok="));
    Serial.print(g_radar_online);
    Serial.print(F(" rrx="));
    Serial.print(g_radar_rx_bytes);
    Serial.print(F(" out="));
    Serial.print(digitalRead(LD2410_OUT));
    if (!g_radar_probe_printed && g_radar_probe_n >= sizeof(g_radar_probe)) {
        g_radar_probe_printed = true;
        Serial.print(F("\nradar probe: "));
        for (uint8_t i = 0; i < g_radar_probe_n; i++)
            Serial.printf("%02X ", g_radar_probe[i]);
        Serial.println();
    }
    Serial.print(F(" t="));
    if (g_time_synced)
        Serial.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    else
        Serial.print(F("--:--"));
    Serial.println();
}

// ================================================================
//  WebSocket
// ================================================================

#define WS_BUF_LEN 512

const char *presenceStr(Presence p) {
    switch (p) {
        case Presence::MOVING:     return "moving";
        case Presence::STATIONARY: return "stationary";
        default:                   return "none";
    }
}

const char *modeStr(LampMode m) {
    switch (m) {
        case LampMode::FORCE_ON:  return "force_on";
        case LampMode::FORCE_OFF: return "force_off";
        default:                  return "auto";
    }
}

LampMode parseMode(const char *s) {
    if (!s) return LampMode::AUTO;
    if (strcmp(s, "force_on") == 0)  return LampMode::FORCE_ON;
    if (strcmp(s, "force_off") == 0) return LampMode::FORCE_OFF;
    return LampMode::AUTO;
}

bool isInScheduleWindow() {
    if (!g_time_synced) return false;
    uint32_t now = g_timeinfo.tm_hour * 3600 + g_timeinfo.tm_min * 60 + g_timeinfo.tm_sec;
    uint32_t ws  = g_config.wake_start_hour * 3600 + g_config.wake_start_minute * 60;
    uint32_t be  = (g_config.bedtime_start_hour * 3600 + g_config.bedtime_start_minute * 60
                     + g_config.bedtime_duration_s) % 86400;
    uint32_t gap = (ws - be + 86400) % 86400;
    if (gap == 0) return true;
    uint32_t e_be = (now - be + 86400) % 86400;
    return e_be >= gap;
}

void setLampMode(LampMode mode) {
    g_state.mode = mode;
}

int buildStateJSON(char *buf, size_t len) {
    uint32_t ts = 0;
    if (g_time_synced) {
        time_t now;
        time(&now);
        ts = (uint32_t)now;
    }
    return snprintf(buf, len,
        "{\"dark\":%s,\"presence\":\"%s\",\"mode\":\"%s\","
        "\"brightness\":%u,\"cct\":%u,\"dark_threshold\":%u,"
        "\"ldr_raw\":%u,\"energy_kwh\":%.6f,\"cost_myr\":%.6f,"
        "\"color_src\":\"%s\",\"rgb_r\":%u,\"rgb_g\":%u,\"rgb_b\":%u,"
        "\"sleep_timer_s\":%lu,\"uptime_s\":%lu,\"in_window\":%s,\"timestamp\":%lu,\"fw\":\"%s\","
        "\"radar_ok\":%s,\"radar_frames\":%lu,\"radar_status\":%u,\"radar_rx\":%lu,"
        "\"radar_mdist\":%lu,\"radar_sdist\":%lu,\"radar_msig\":%u,\"radar_ssig\":%u,"
        "\"radar_out\":%u}",
        g_state.is_dark ? "true" : "false",
        presenceStr(g_state.presence),
        modeStr(g_state.mode),
        g_state.brightness,
        g_state.color_temp,
        g_config.dark_threshold,
        g_state.ldr_raw,
        g_state.total_kwh,
        g_state.total_kwh * 0.27f,
        g_rgb_active ? "rgb" : "cct",
        g_rgb_active ? g_rgb_r : 0,
        g_rgb_active ? g_rgb_g : 0,
        g_rgb_active ? g_rgb_b : 0,
        (unsigned long)g_sleep_timer_s,
        (unsigned long)(millis() / 1000),
        isInScheduleWindow() ? "true" : "false",
        (unsigned long)ts,
        FW_VERSION,
        g_radar_online ? "true" : "false",
        (unsigned long)radar.getFrameCount(),
        (unsigned)radar.getStatus(),
        (unsigned long)g_radar_rx_bytes,
        (unsigned long)radar.movingTargetDistance(),
        (unsigned long)radar.stationaryTargetDistance(),
        (unsigned)radar.movingTargetSignal(),
        (unsigned)radar.stationaryTargetSignal(),
        (unsigned)digitalRead(LD2410_OUT));
}

void wsSendState(AsyncWebSocketClient *client) {
    char buf[WS_BUF_LEN];
    buildStateJSON(buf, sizeof(buf));
    client->text(buf);
}

void wsBroadcast() {
    if (wsSock.count() == 0) return;
    char buf[WS_BUF_LEN];
    buildStateJSON(buf, sizeof(buf));
    wsSock.textAll(buf);
}

void wsHandleCommand(uint8_t *data, size_t len, AsyncWebSocketClient *client) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);

    if (err) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"error\":\"invalid json: %s\"}", err.c_str());
        client->text(buf);
        return;
    }

    const char *cmd = doc["cmd"];
    if (!cmd) {
        client->text("{\"error\":\"missing cmd field\"}");
        return;
    }

    if (strcmp(cmd, "set_bedtime") == 0) {
        uint8_t  sh  = doc["start_h"]    | 0;
        uint8_t  sm  = doc["start_m"]    | 0;
        uint16_t dur = doc["duration_s"] | 0;
        if (sh > 23 || sm > 59 || dur == 0 || dur > 43200) {
            client->text("{\"error\":\"bedtime: hours 0-23, minutes 0-59, duration 1-43200s\"}");
            return;
        }
        setBedtimeWindow(sh, sm, dur);
        Serial.printf("ws: set_bedtime %02d:%02d +%us\n", sh, sm, dur);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_bedtime\"}");
    }
    else if (strcmp(cmd, "set_wake") == 0) {
        uint8_t  sh  = doc["start_h"]    | 0;
        uint8_t  sm  = doc["start_m"]    | 0;
        uint16_t dur = doc["duration_s"] | 0;
        if (sh > 23 || sm > 59 || dur == 0 || dur > 43200) {
            client->text("{\"error\":\"wake: hours 0-23, minutes 0-59, duration 1-43200s\"}");
            return;
        }
        setWakeWindow(sh, sm, dur);
        Serial.printf("ws: set_wake %02d:%02d +%us\n", sh, sm, dur);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_wake\"}");
    }
    else if (strcmp(cmd, "set_dark_threshold") == 0) {
        uint16_t val = doc["value"] | 0;
        if (val > 4095) {
            client->text("{\"error\":\"threshold must be 0-4095\"}");
            return;
        }
        setDarkThreshold(val);
        Serial.printf("ws: set_dark_threshold %u\n", val);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_dark_threshold\"}");
    }
    else if (strcmp(cmd, "override") == 0) {
        const char *mode = doc["mode"];
        if (!mode || (strcmp(mode, "auto") != 0
                   && strcmp(mode, "force_on") != 0
                   && strcmp(mode, "force_off") != 0)) {
            client->text("{\"error\":\"mode must be auto, force_on, or force_off\"}");
            return;
        }
        setLampMode(parseMode(mode));
        Serial.printf("ws: override %s\n", mode);
        client->text("{\"result\":\"ok\",\"cmd\":\"override\"}");
    }
    else if (strcmp(cmd, "set_brightness") == 0) {
        uint16_t val = doc["value"] | 0;
        if (val > 255) {
            client->text("{\"error\":\"brightness must be 0-255\"}");
            return;
        }
        g_manual_brightness = (uint8_t)val;
        g_manual_bri_active = true;
        Serial.printf("ws: set_brightness %u\n", val);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_brightness\"}");
    }
    else if (strcmp(cmd, "set_cct") == 0) {
        uint16_t val = doc["value"] | 0;
        if (val < 2000 || val > 6500) {
            client->text("{\"error\":\"cct must be 2000-6500K\"}");
            return;
        }
        g_manual_cct = val;
        g_rgb_active = false;
        g_manual_cct_active = true;
        Serial.printf("ws: set_cct %u\n", val);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_cct\"}");
    }
    else if (strcmp(cmd, "set_rgb") == 0) {
        uint16_t r = doc["r"] | 0;
        uint16_t g = doc["g"] | 0;
        uint16_t b = doc["b"] | 0;
        Serial.printf("DEBUG set_rgb RAW: r=%u g=%u b=%u\n", r, g, b);
        if (r > 255 || g > 255 || b > 255) {
            client->text("{\"error\":\"rgb values must be 0-255\"}");
            return;
        }
        g_rgb_active = true;
        g_manual_cct_active = false;
        g_rgb_r = (uint8_t)r;
        g_rgb_g = (uint8_t)g;
        g_rgb_b = (uint8_t)b;
        Serial.printf("DEBUG set_rgb STORED: r=%u g=%u b=%u active=%d\n", g_rgb_r, g_rgb_g, g_rgb_b, g_rgb_active);
        Serial.printf("ws: set_rgb %u,%u,%u\n", r, g, b);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_rgb\"}");
    }
    else if (strcmp(cmd, "start_sleep_timer") == 0) {
        uint16_t mins = doc["minutes"] | 0;
        if (mins == 0 || mins > 120) {
            client->text("{\"error\":\"sleep timer must be 1-120 minutes\"}");
            return;
        }
        g_sleep_timer_total = mins * 60;
        g_sleep_timer_start = millis();
        g_sleep_timer_s = g_sleep_timer_total;
        g_sleep_dimming = false;
        Serial.printf("ws: start_sleep_timer %umin\n", mins);
        client->text("{\"result\":\"ok\",\"cmd\":\"start_sleep_timer\"}");
    }
    else if (strcmp(cmd, "cancel_sleep_timer") == 0) {
        g_sleep_timer_s = 0;
        g_sleep_dimming = false;
        Serial.println(F("ws: cancel_sleep_timer"));
        client->text("{\"result\":\"ok\",\"cmd\":\"cancel_sleep_timer\"}");
    }
    else if (strcmp(cmd, "set_gate_params") == 0) {
        uint8_t gate = doc["gate"] | 0;
        uint8_t moving = doc["moving"] | 0;
        uint8_t stationary = doc["stationary"] | 0;
        if (moving > 100 || stationary > 100) {
            client->text("{\"error\":\"thresholds must be 0-100\"}");
            return;
        }
        radar.setGateParameters(gate, moving, stationary);
        Serial.printf("ws: set_gate_params gate=%u m=%u s=%u\n", gate, moving, stationary);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_gate_params\"}");
    }
    else if (strcmp(cmd, "reset_energy") == 0) {
        g_state.total_kwh = 0.0f;
        saveEnergy();
        Serial.println(F("ws: reset_energy"));
        client->text("{\"result\":\"ok\",\"cmd\":\"reset_energy\"}");
    }
    else {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"error\":\"unknown command: %s\"}", cmd);
        client->text(buf);
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("ws: client %u connected\n", client->id());
        wsSendState(client);
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("ws: client %u disconnected\n", client->id());
    }
    else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->opcode == WS_TEXT && len > 0) {
            data[len] = 0;
            wsHandleCommand(data, len, client);
        }
    }
}

// ================================================================
//  Energy persistence
// ================================================================

void saveEnergy() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, false);
    prefs.putFloat("energy", g_state.total_kwh);
    prefs.end();
}

void loadEnergy() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, true);
    g_state.total_kwh = prefs.getFloat("energy", 0.0f);
    prefs.end();
    Serial.printf("cfg: energy loaded %.6f kWh\n", g_state.total_kwh);
}
