#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>
#include "pins.h"
#include "state.h"
#include "wifi_config.h"
#include "MyLD2410.h"
#include "config.h"
#include "logic.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <PubSubClient.h>

// #define RADAR_DEBUG 1

// ---------- brightness / color ----------

const uint8_t  DIM_FLOOR           = 10;
const uint16_t DIM_FLOOR_CCT       = 2200;
const uint16_t WAKE_PEAK_CCT      = 5000;
const uint16_t SLEEP_DIM_RAMP_S   = 30;
const uint16_t LDR_HYSTERESIS     = 200;

// ---------- defaults ----------

const uint8_t  FULL_BRIGHTNESS = 255;
const uint16_t DEFAULT_CCT     = 2700;
#define FW_VERSION "1.5.0"

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

// ---------- wifi provisioning / auth / diagnostics ----------

DNSServer     g_dns;
String        g_wifi_ssid;
String        g_wifi_pass;
bool          g_setup_ap = false;
uint8_t       g_wifi_fail_count = 0;
unsigned long g_last_wifi_attempt = 0;
bool          g_ldr_fault = false;

static const uint8_t WIFI_MAX_NETWORKS = 3;
String        g_wifi_list_ssid[WIFI_MAX_NETWORKS + 1];
String        g_wifi_list_pass[WIFI_MAX_NETWORKS + 1];
uint8_t       g_wifi_net_count = 0;
uint8_t       g_wifi_pref = 0;

struct WsClientAuth {
    uint32_t id;
    bool     authed;
};
WsClientAuth g_ws_auth[4] = {};

bool clientAuthed(uint32_t id) {
    if (strlen(WS_TOKEN) == 0) return true;
    for (auto &a : g_ws_auth) if (a.id == id) return a.authed;
    return false;
}

void setClientAuth(uint32_t id, bool authed) {
    for (auto &a : g_ws_auth) if (a.id == id) { a.authed = authed; return; }
    for (auto &a : g_ws_auth) if (a.id == 0) { a.id = id; a.authed = authed; return; }
    g_ws_auth[0] = { id, authed };
}

void clearClientAuth(uint32_t id) {
    for (auto &a : g_ws_auth) if (a.id == id) a.id = 0;
}

// ---------- mqtt (home assistant) ----------

WiFiClient   mqttNet;
PubSubClient mqtt(mqttNet);

char  g_device_id[16]        = "lp000000";
char  g_topic_state[48]      = "";
char  g_topic_ha_state[48]   = "";
char  g_topic_set[48]        = "";
char  g_topic_auto_set[48]   = "";
char  g_topic_avail[48]      = "";
char  g_topic_disc_light[64] = "";
char  g_topic_disc_switch[64] = "";
char  g_topic_disc_occ[64]   = "";
char  g_topic_disc_ldr[64]   = "";
char  g_topic_disc_dist[64]  = "";
char  g_topic_disc_energy[64] = "";
unsigned long g_last_mqtt_try = 0;
unsigned long g_last_mqtt_pub = 0;
char          g_mqtt_fp[80]   = "";

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
    uint8_t r, g, b;
    lamp::kelvin_rgb(kelvin, &r, &g, &b);
    return CRGB(r, g, b);
}

// ---------- forward decls ----------

void wifi_connect();
void loadWifiCreds();
void saveWifiNetwork(const String &ssid, const String &pass);
void startSetupAP();
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
lamp::ScheduleParams currentScheduleParams();
void setLampMode(LampMode mode);
const char *presenceStr(Presence p);
const char *modeStr(LampMode m);
void saveEnergy();
void loadEnergy();
void mqtt_loop();
void mqtt_connect();
void mqtt_publish_discovery();
void mqtt_publish_state();
void mqttCallback(char *topic, byte *payload, unsigned int length);

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
    loadWifiCreds();
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

    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(g_device_id, sizeof(g_device_id), "lp%02x%02x%02x", mac[3], mac[4], mac[5]);
        snprintf(g_topic_state, sizeof(g_topic_state), "lightplus/%s/state", g_device_id);
        snprintf(g_topic_ha_state, sizeof(g_topic_ha_state), "lightplus/%s/ha/state", g_device_id);
        snprintf(g_topic_set, sizeof(g_topic_set), "lightplus/%s/ha/set", g_device_id);
        snprintf(g_topic_auto_set, sizeof(g_topic_auto_set), "lightplus/%s/ha/auto/set", g_device_id);
        snprintf(g_topic_avail, sizeof(g_topic_avail), "lightplus/%s/availability", g_device_id);
        snprintf(g_topic_disc_light, sizeof(g_topic_disc_light), "homeassistant/light/%s_light/config", g_device_id);
        snprintf(g_topic_disc_switch, sizeof(g_topic_disc_switch), "homeassistant/switch/%s_auto/config", g_device_id);
        snprintf(g_topic_disc_occ, sizeof(g_topic_disc_occ), "homeassistant/binary_sensor/%s_occupancy/config", g_device_id);
        snprintf(g_topic_disc_ldr, sizeof(g_topic_disc_ldr), "homeassistant/sensor/%s_light_level/config", g_device_id);
        snprintf(g_topic_disc_dist, sizeof(g_topic_disc_dist), "homeassistant/sensor/%s_distance/config", g_device_id);
        snprintf(g_topic_disc_energy, sizeof(g_topic_disc_energy), "homeassistant/sensor/%s_energy/config", g_device_id);
        mqtt.setCallback(mqttCallback);
        mqtt.setBufferSize(1024);
        Serial.printf("mqtt: device id %s\n", g_device_id);
    }

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
    server.on("/manifest.webmanifest", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/manifest.webmanifest", "application/manifest+json");
    });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html",
            "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>LightPlus Setup</title></head>"
            "<body style='background:#0b0d12;color:#e8eaf0;font-family:system-ui,sans-serif;padding:24px;max-width:420px;margin:0 auto'>"
            "<h1 style='font-size:20px'>LightPlus Setup</h1>"
            "<p style='color:#8b93a7;font-size:14px'>Enter your WiFi network details. The lamp will restart and connect.</p>"
            "<form method='POST' action='/wifi'>"
            "<label style='display:block;margin:14px 0 6px;font-size:13px'>Network name (SSID)</label>"
            "<input name='ssid' required style='width:100%;padding:10px;border-radius:8px;border:1px solid #262b38;background:#14171f;color:#e8eaf0'/>"
            "<label style='display:block;margin:14px 0 6px;font-size:13px'>Password</label>"
            "<input name='pass' type='password' style='width:100%;padding:10px;border-radius:8px;border:1px solid #262b38;background:#14171f;color:#e8eaf0'/>"
            "<button type='submit' style='margin-top:18px;width:100%;padding:12px;border:none;border-radius:8px;background:#f5a742;color:#1a1205;font-weight:700'>Save &amp; restart</button>"
            "</form></body></html>");
    });
    server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        String s = request->arg("ssid");
        String p = request->arg("pass");
        if (s.length() == 0) { request->send(400, "text/plain", "SSID required"); return; }
        saveWifiNetwork(s, p);
        Serial.printf("wifi: saved credentials for '%s', restarting\n", s.c_str());
        request->send(200, "text/html",
            "<body style='background:#0b0d12;color:#e8eaf0;font-family:system-ui,sans-serif;padding:24px'>"
            "<h2>Saved</h2><p>Restarting...</p></body>");
        delay(600);
        ESP.restart();
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (g_setup_ap) request->redirect("/wifi");
        else request->send(LittleFS, "/index.html", "text/html");
    });
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (g_setup_ap) request->redirect("/wifi");
        else request->send(404, "text/plain", "not found");
    });
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

    if (g_setup_ap)
        g_dns.processNextRequest();

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
        g_radar_online = true;
    }

    if (g_radar_online && now - g_radar_last_frame_ms >= 5000UL) {
        Serial.println(F("radar: stream stalled 5s, restarting"));
        g_radar_online = false;
    }

    if (!g_radar_online && now - g_last_radar_reinit >= 5000UL) {
        g_last_radar_reinit = now;
        radarSerial.end();
        delay(50);
        radarSerial.begin(256000, SERIAL_8N1, g_radar_rx_pin, g_radar_tx_pin);
        g_radar_last_frame_ms = millis();
        g_radar_poll_fc = radar.getFrameCount();
        Serial.println(F("radar: uart restarted, waiting for data"));
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

    mqtt_loop();

    ArduinoOTA.handle();

    if (now - g_last_energy_save >= 300000UL) {
        saveEnergy();
        g_last_energy_save = now;
    }
}

// ================================================================
//  WiFi
// ================================================================

void loadWifiCreds() {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, true);

    String ssids[WIFI_MAX_NETWORKS + 1];
    String passes[WIFI_MAX_NETWORKS + 1];
    uint8_t n = 0;

    for (uint8_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        String s = prefs.getString(("w_ssid" + String(i)).c_str(), "");
        String p = prefs.getString(("w_pass" + String(i)).c_str(), "");
        if (s.length() == 0) continue;
        ssids[n] = s;
        passes[n] = p;
        n++;
        if (i == 0) { g_wifi_ssid = s; g_wifi_pass = p; }
    }

    if (n == 0) {
        String legacy = prefs.getString("w_ssid", "");
        String legacyPass = prefs.getString("w_pass", "");
        if (legacy.length() > 0) {
            ssids[n] = legacy;
            passes[n] = legacyPass;
            n++;
            g_wifi_ssid = legacy;
            g_wifi_pass = legacyPass;
            Serial.printf("wifi: using saved network '%s' (legacy key)\n", legacy.c_str());
        }
    }

    prefs.end();

    bool dup = false;
    for (uint8_t i = 0; i < n; i++)
        if (ssids[i] == WIFI_SSID) dup = true;

    if (!dup && strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
        ssids[n] = WIFI_SSID;
        passes[n] = WIFI_PASSWORD;
        n++;
        Serial.println(F("wifi: compile-time network added as fallback"));
    }

    for (uint8_t i = 0; i < n; i++) {
        if (ssids[i] == WIFI_SSID && i != 0) {
            String ts = ssids[0], tp = passes[0];
            ssids[0] = ssids[i];
            passes[0] = passes[i];
            ssids[i] = ts;
            passes[i] = tp;
            break;
        }
    }

    if (n == 0) {
        g_wifi_list_ssid[0] = WIFI_SSID;
        g_wifi_list_pass[0] = WIFI_PASSWORD;
        g_wifi_net_count = 1;
        g_wifi_ssid = WIFI_SSID;
        g_wifi_pass = WIFI_PASSWORD;
        Serial.println(F("wifi: using compile-time credentials"));
        return;
    }

    for (uint8_t i = 0; i < n; i++) {
        g_wifi_list_ssid[i] = ssids[i];
        g_wifi_list_pass[i] = passes[i];
    }
    g_wifi_net_count = n;
    Serial.printf("wifi: %u network(s) registered\n", n);
}

void saveWifiNetwork(const String &ssid, const String &pass) {
    Preferences prefs;
    prefs.begin(CONFIG_NAMESPACE, false);

    int out = 1;
    for (uint8_t i = 0; i < WIFI_MAX_NETWORKS && out < WIFI_MAX_NETWORKS; i++) {
        String s = prefs.getString(("w_ssid" + String(i)).c_str(), "");
        if (s.length() == 0 || s == ssid) continue;
        String p = prefs.getString(("w_pass" + String(i)).c_str(), "");
        prefs.putString(("w_ssid" + String(out)).c_str(), s);
        prefs.putString(("w_pass" + String(out)).c_str(), p);
        out++;
    }
    for (int i = out; i < WIFI_MAX_NETWORKS; i++) {
        prefs.remove(("w_ssid" + String(i)).c_str());
        prefs.remove(("w_pass" + String(i)).c_str());
    }
    prefs.putString("w_ssid0", ssid);
    prefs.putString("w_pass0", pass);
    prefs.remove("w_ssid");
    prefs.remove("w_pass");
    prefs.end();
}

void startSetupAP() {
    if (g_setup_ap) return;
    g_setup_ap = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("LightPlus-Setup");
    g_dns.start(53, "*", WiFi.softAPIP());
    Serial.println(F("wifi: setup AP 'LightPlus-Setup' active"));
    Serial.print(F("wifi: open http://"));
    Serial.println(WiFi.softAPIP());
}

void wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_fail_count = 0;
        return;
    }
    if (millis() - g_last_wifi_attempt < 15000UL) return;
    g_last_wifi_attempt = millis();

    for (uint8_t k = 0; k < g_wifi_net_count; k++) {
        uint8_t i = (g_wifi_pref + k) % g_wifi_net_count;
        Serial.printf("WiFi: trying '%s' ", g_wifi_list_ssid[i].c_str());
        WiFi.disconnect();
        delay(50);
        WiFi.begin(g_wifi_list_ssid[i].c_str(), g_wifi_list_pass[i].c_str());
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 8000UL) {
            delay(250);
            Serial.print('.');
        }
        if (WiFi.status() == WL_CONNECTED) {
            g_wifi_fail_count = 0;
            g_wifi_pref = i;
            Serial.println(F(" ok"));
            Serial.print(F("  IP ")); Serial.println(WiFi.localIP());
            return;
        }
        Serial.println(F(" fail"));
    }
    if (++g_wifi_fail_count >= 3) startSetupAP();
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
    g_ldr_fault = (g_state.ldr_raw <= 3 || g_state.ldr_raw >= 4092);
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

    const uint8_t scanPins[] = {LD2410_RX_PIN, LD2410_TX_PIN, LD2410_OUT}; // 16, 17, 4
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
        float p = lamp::ease_in_cubic((float)elapsed_s / SLEEP_DIM_RAMP_S);
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
        (g_presence_last_seen != 0 && millis() - g_presence_last_seen < (unsigned long)g_config.presence_hold_s * 1000UL);

    bool should = g_state.is_dark && presence_recent;
    if (!should) {
        g_manual_bri_active = false;
        g_manual_cct_active = false;
        g_rgb_active = false;
        if (g_config.presence_lost == 1 && g_state.is_dark) {
            g_state.brightness = DIM_FLOOR;
            g_state.color_temp = DIM_FLOOR_CCT;
        } else {
            g_state.brightness = 0;
        }
        return;
    }

    if (!g_time_synced) {
        g_state.brightness = FULL_BRIGHTNESS;
        g_state.color_temp = DEFAULT_CCT;
    } else {
        uint32_t now_s = g_timeinfo.tm_hour * 3600 + g_timeinfo.tm_min * 60 + g_timeinfo.tm_sec;
        lamp::LampOutput o = lamp::schedule_output(now_s, currentScheduleParams());
        g_state.brightness = o.brightness;
        g_state.color_temp = o.cct;
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

#define WS_BUF_LEN 768

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

lamp::ScheduleParams currentScheduleParams() {
    return lamp::ScheduleParams{
        g_config.bedtime_start_hour, g_config.bedtime_start_minute, g_config.bedtime_duration_s,
        g_config.wake_start_hour, g_config.wake_start_minute, g_config.wake_duration_s,
        DIM_FLOOR, DIM_FLOOR_CCT, FULL_BRIGHTNESS, DEFAULT_CCT, WAKE_PEAK_CCT
    };
}

bool isInScheduleWindow() {
    if (!g_time_synced) return false;
    uint32_t now_s = g_timeinfo.tm_hour * 3600 + g_timeinfo.tm_min * 60 + g_timeinfo.tm_sec;
    return lamp::schedule_window_active(now_s, currentScheduleParams());
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
        "\"bs_h\":%u,\"bs_m\":%u,\"bs_d\":%u,\"ws_h\":%u,\"ws_m\":%u,\"ws_d\":%u,"
        "\"ph_s\":%u,\"pl_act\":%u,\"ldr_fault\":%s,\"ap\":%s,"
        "\"id\":\"%s\",\"mqtt_en\":%u,\"mqtt_on\":%s,\"mqtt_host\":\"%s\",\"mqtt_port\":%u,\"wifi_nets\":%u,"
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
        g_config.bedtime_start_hour,
        g_config.bedtime_start_minute,
        g_config.bedtime_duration_s,
        g_config.wake_start_hour,
        g_config.wake_start_minute,
        g_config.wake_duration_s,
        g_config.presence_hold_s,
        g_config.presence_lost,
        g_ldr_fault ? "true" : "false",
        g_setup_ap ? "true" : "false",
        g_device_id,
        g_config.mqtt_enabled,
        mqtt.connected() ? "true" : "false",
        g_config.mqtt_host,
        g_config.mqtt_port,
        g_wifi_net_count,
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

    if (strcmp(cmd, "auth") == 0) {
        const char *tok = doc["token"] | "";
        if (strlen(WS_TOKEN) > 0 && strcmp(tok, WS_TOKEN) == 0) {
            setClientAuth(client->id(), true);
            client->text("{\"result\":\"ok\",\"cmd\":\"auth\"}");
        } else {
            client->text("{\"error\":\"bad token\"}");
        }
        return;
    }

    if (!clientAuthed(client->id())) {
        client->text("{\"error\":\"unauthorized\"}");
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
    else if (strcmp(cmd, "set_presence") == 0) {
        uint16_t hold = doc["hold_s"] | 0;
        const char *lost = doc["lost"] | "";
        if (hold < 1 || hold > 300) {
            client->text("{\"error\":\"hold_s must be 1-300\"}");
            return;
        }
        setPresenceBehavior(hold, strcmp(lost, "dim") == 0 ? 1 : 0);
        Serial.printf("ws: set_presence hold=%us lost=%s\n", hold, lost);
        client->text("{\"result\":\"ok\",\"cmd\":\"set_presence\"}");
    }
    else if (strcmp(cmd, "wifi_setup") == 0) {
        startSetupAP();
        Serial.println(F("ws: wifi_setup"));
        client->text("{\"result\":\"ok\",\"cmd\":\"wifi_setup\"}");
    }
    else if (strcmp(cmd, "set_mqtt") == 0) {
        uint8_t en = doc["enabled"] | 0;
        const char *host = doc["host"] | "";
        uint16_t port = doc["port"] | 1883;
        if (strlen(host) > 63) {
            client->text("{\"error\":\"host too long\"}");
            return;
        }
        char u[32], p[32];
        const char *user = doc["user"];
        const char *pass = doc["pass"];
        strlcpy(u, user ? user : g_config.mqtt_user, sizeof(u));
        strlcpy(p, pass ? pass : g_config.mqtt_pass, sizeof(p));
        setMqttConfig(en, host, port, u, p);
        if (mqtt.connected()) mqtt.disconnect();
        g_last_mqtt_try = 0;
        Serial.println(F("ws: set_mqtt"));
        client->text("{\"result\":\"ok\",\"cmd\":\"set_mqtt\"}");
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

    wsBroadcast();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("ws: client %u connected\n", client->id());
        setClientAuth(client->id(), strlen(WS_TOKEN) == 0);
        wsSendState(client);
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("ws: client %u disconnected\n", client->id());
        clearClientAuth(client->id());
    }
    else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->opcode == WS_TEXT && len > 0) {
            wsHandleCommand(data, len, client);
        }
    }
}

// ================================================================
//  MQTT (Home Assistant)
// ================================================================

void mqtt_connect() {
    mqtt.setServer(g_config.mqtt_host, g_config.mqtt_port);
    const char *user = g_config.mqtt_user[0] ? g_config.mqtt_user : nullptr;
    const char *pass = g_config.mqtt_pass[0] ? g_config.mqtt_pass : nullptr;
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "lightplus_%s", g_device_id);
    Serial.printf("mqtt: connecting to %s:%u\n", g_config.mqtt_host, g_config.mqtt_port);
    bool ok = mqtt.connect(clientId, user, pass, g_topic_avail, 0, true, "offline");
    if (!ok) {
        Serial.printf("mqtt: failed rc=%d\n", mqtt.state());
        return;
    }
    Serial.println(F("mqtt: connected"));
    mqtt.publish(g_topic_avail, "online", true);
    mqtt.subscribe(g_topic_set);
    mqtt.subscribe(g_topic_auto_set);
    mqtt_publish_discovery();
    mqtt_publish_state();
    g_mqtt_fp[0] = 0;
}

void mqtt_publish_discovery() {
    char buf[768];

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Light\",\"unique_id\":\"%s_light\",\"schema\":\"json\","
        "\"state_topic\":\"%s\",\"command_topic\":\"%s\","
        "\"supported_color_modes\":[\"color_temp\",\"rgb\"],\"brightness\":true,"
        "\"availability_topic\":\"%s\",\"icon\":\"mdi:ceiling-light\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\","
        "\"manufacturer\":\"LightPlus\",\"model\":\"ESP32 presence lamp\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_set, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_light, buf, true);

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Auto mode\",\"unique_id\":\"%s_auto\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ 'ON' if value_json.mode == 'auto' else 'OFF' }}\","
        "\"command_topic\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
        "\"icon\":\"mdi:auto-mode\",\"availability_topic\":\"%s\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_auto_set, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_switch, buf, true);

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Occupancy\",\"unique_id\":\"%s_occupancy\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ 'ON' if value_json.presence != 'none' else 'OFF' }}\","
        "\"device_class\":\"occupancy\",\"availability_topic\":\"%s\",\"icon\":\"mdi:motion-sensor\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_occ, buf, true);

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Light level\",\"unique_id\":\"%s_light_level\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.ldr_raw }}\",\"icon\":\"mdi:brightness-6\","
        "\"availability_topic\":\"%s\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_ldr, buf, true);

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Target distance\",\"unique_id\":\"%s_distance\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.radar_mdist }}\",\"unit_of_measurement\":\"cm\","
        "\"state_class\":\"measurement\",\"icon\":\"mdi:radar\",\"availability_topic\":\"%s\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_dist, buf, true);

    snprintf(buf, sizeof(buf),
        "{\"name\":\"Energy\",\"unique_id\":\"%s_energy\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.energy_kwh }}\",\"unit_of_measurement\":\"kWh\","
        "\"device_class\":\"energy\",\"state_class\":\"total_increasing\","
        "\"icon\":\"mdi:lightning-bolt\",\"availability_topic\":\"%s\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightPlus\",\"sw_version\":\"%s\"}}",
        g_device_id, g_topic_ha_state, g_topic_avail, g_device_id, FW_VERSION);
    mqtt.publish(g_topic_disc_energy, buf, true);
}

void mqtt_publish_state() {
    char buf[448];
    const uint32_t mired = 1000000UL / (g_state.color_temp ? g_state.color_temp : 2700);

    if (g_rgb_active) {
        snprintf(buf, sizeof(buf),
            "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"rgb\","
            "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},"
            "\"presence\":\"%s\",\"dark\":%s,\"ldr_raw\":%u,\"cct\":%u,"
            "\"energy_kwh\":%.5f,\"radar_mdist\":%lu,\"mode\":\"%s\",\"in_window\":%s}",
            g_state.brightness > 0 ? "ON" : "OFF", g_state.brightness,
            g_rgb_r, g_rgb_g, g_rgb_b,
            presenceStr(g_state.presence), g_state.is_dark ? "true" : "false",
            g_state.ldr_raw, g_state.color_temp,
            g_state.total_kwh, (unsigned long)radar.movingTargetDistance(),
            modeStr(g_state.mode), isInScheduleWindow() ? "true" : "false");
    } else {
        snprintf(buf, sizeof(buf),
            "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"color_temp\",\"color_temp\":%lu,"
            "\"presence\":\"%s\",\"dark\":%s,\"ldr_raw\":%u,\"cct\":%u,"
            "\"energy_kwh\":%.5f,\"radar_mdist\":%lu,\"mode\":\"%s\",\"in_window\":%s}",
            g_state.brightness > 0 ? "ON" : "OFF", g_state.brightness, (unsigned long)mired,
            presenceStr(g_state.presence), g_state.is_dark ? "true" : "false",
            g_state.ldr_raw, g_state.color_temp,
            g_state.total_kwh, (unsigned long)radar.movingTargetDistance(),
            modeStr(g_state.mode), isInScheduleWindow() ? "true" : "false");
    }
    mqtt.publish(g_topic_ha_state, buf, true);

    char rich[WS_BUF_LEN];
    buildStateJSON(rich, sizeof(rich));
    mqtt.publish(g_topic_state, rich, true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    if (strcmp(topic, g_topic_auto_set) == 0) {
        bool on = (length >= 2 && payload[0] == 'O' && payload[1] == 'N');
        setLampMode(on ? LampMode::AUTO : LampMode::FORCE_OFF);
        Serial.printf("mqtt: auto mode %s\n", on ? "on" : "off");
        mqtt_publish_state();
        wsBroadcast();
        return;
    }

    if (strcmp(topic, g_topic_set) != 0) return;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.println(F("mqtt: bad json"));
        return;
    }

    bool changed = false;

    if (doc.containsKey("cmd")) {
        const char *cmd = doc["cmd"] | "";
        if (strcmp(cmd, "override") == 0) {
            setLampMode(parseMode(doc["mode"] | "auto"));
            changed = true;
        } else if (strcmp(cmd, "set_brightness") == 0) {
            uint16_t v = doc["value"] | g_manual_brightness;
            if (v <= 255) { g_manual_brightness = (uint8_t)v; g_manual_bri_active = true; changed = true; }
        } else if (strcmp(cmd, "set_cct") == 0) {
            uint16_t v = doc["value"] | g_manual_cct;
            if (v >= 2000 && v <= 6500) {
                g_manual_cct = v;
                g_rgb_active = false;
                g_manual_cct_active = true;
                changed = true;
            }
        } else if (strcmp(cmd, "set_rgb") == 0) {
            uint16_t r = doc["r"] | 0, g = doc["g"] | 0, b = doc["b"] | 0;
            if (r <= 255 && g <= 255 && b <= 255) {
                g_rgb_r = (uint8_t)r;
                g_rgb_g = (uint8_t)g;
                g_rgb_b = (uint8_t)b;
                g_rgb_active = true;
                g_manual_cct_active = false;
                changed = true;
            }
        }
    } else {
        const char *st = doc["state"];
        if (st) {
            setLampMode(strcmp(st, "ON") == 0 ? LampMode::FORCE_ON : LampMode::FORCE_OFF);
            changed = true;
        }
        if (doc.containsKey("brightness")) {
            uint16_t v = doc["brightness"] | g_manual_brightness;
            if (v <= 255) { g_manual_brightness = (uint8_t)v; g_manual_bri_active = true; changed = true; }
        }
        if (doc.containsKey("color_temp")) {
            uint32_t m = doc["color_temp"] | 0;
            if (m > 0) {
                uint32_t k = 1000000UL / m;
                if (k < 2000) k = 2000;
                if (k > 6500) k = 6500;
                g_manual_cct = (uint16_t)k;
                g_rgb_active = false;
                g_manual_cct_active = true;
                changed = true;
            }
        }
        if (doc.containsKey("color")) {
            JsonObject c = doc["color"];
            g_rgb_r = c["r"] | 0;
            g_rgb_g = c["g"] | 0;
            g_rgb_b = c["b"] | 0;
            g_rgb_active = true;
            g_manual_cct_active = false;
            changed = true;
        }
    }

    if (changed) {
        Serial.println(F("mqtt: command applied"));
        mqtt_publish_state();
        wsBroadcast();
    }
}

void mqtt_loop() {
    if (!g_config.mqtt_enabled || g_config.mqtt_host[0] == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    if (mqtt.connected()) {
        mqtt.loop();
    } else if (millis() - g_last_mqtt_try >= 5000UL) {
        g_last_mqtt_try = millis();
        mqtt_connect();
    }

    if (mqtt.connected()) {
        char fp[80];
        snprintf(fp, sizeof(fp), "%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%lu",
                 (unsigned)g_state.mode, g_state.brightness, g_state.color_temp,
                 g_rgb_active ? 1 : 0, g_rgb_r, g_rgb_g, g_rgb_b,
                 (unsigned)g_state.presence, g_state.is_dark ? 1 : 0,
                 isInScheduleWindow() ? 1 : 0,
                 (unsigned long)(g_state.total_kwh * 100000.0f));
        if (strcmp(fp, g_mqtt_fp) != 0 || millis() - g_last_mqtt_pub >= 60000UL) {
            strlcpy(g_mqtt_fp, fp, sizeof(g_mqtt_fp));
            g_last_mqtt_pub = millis();
            mqtt_publish_state();
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
