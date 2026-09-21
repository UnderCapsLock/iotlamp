#pragma once

// LD2410C 24GHz mmWave Presence Radar (UART)
// Sensor TX -> GPIO16 (ESP32 RX), Sensor RX -> GPIO17 (ESP32 TX)
#define LD2410_RX_PIN GPIO_NUM_16
#define LD2410_TX_PIN GPIO_NUM_17
#define LD2410_OUT    GPIO_NUM_4

// LDR voltage divider (ambient light)
#define LDR_PIN GPIO_NUM_34

// HS-F12A 12x WS2812 RGB LED ring
#define LED_PIN       5
#define LED_COUNT     12
#define LED_TYPE      WS2812
#define LED_COLOR_ORDER GRB

// Timing constants (ms)
#define LDR_INTERVAL         200
#define RADAR_INTERVAL        50
#define LED_INTERVAL          33
#define WS_BROADCAST_INTERVAL 1000
#define NTP_INTERVAL_S        3600
#define HEARTBEAT_INTERVAL    30000
