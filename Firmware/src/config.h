#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FIRMWARE_VERSION "v4.1.3"

// Global I2C Mutex for multi-core thread safety
extern SemaphoreHandle_t i2cMutex;

// ==========================================
// HARDWARE PIN DEFINITIONS
// ==========================================

// Display Pins (Matches platformio.ini)
#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS 15
#define PIN_TFT_DC 2
#define PIN_TFT_BL 27

// Touch Screen (GT911 via I2C)
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 26
#define TOUCH_RST 25

// Display Reset (Matches platformio.ini)
#define PIN_TFT_RST -1

// Native Resolution (Portrait)
#define TOUCH_WIDTH 320
#define TOUCH_HEIGHT 480

// Touch Calibration / Mapping
// Common for Landscape on this board: Swap XY=true, Invert Y=true
#define TOUCH_SWAP_XY true
#define TOUCH_INVERT_X true
#define TOUCH_INVERT_Y false

// SD Card Pins (SPI)
#define PIN_SD_CS 5
#define PIN_SD_MOSI 23
#define PIN_SD_SCLK 18
#define PIN_SD_MISO 19

// Peripherals
#define PIN_RGB_RED 4
#define PIN_RGB_GREEN 16
#define PIN_RGB_BLUE 17
#define LED_COMMON_ANODE // Uncomment if using Common Anode (Active LOW) RGB LED
// #define PIN_LIGHT_SENSOR 34
// #define PIN_LIGHT_SENSOR 34
// #define PIN_SPEAKER 26
#define PIN_RPM_INPUT 35 // User requested 35

// GPS / UART
// GPS at GPIO 22 (RX only - no TX needed, device only receives from GPS)
// UART0 (GPIO 1/3) reserved for PC Debugging
#define PIN_GPS_RX 22
#define PIN_GPS_TX 21 // Restore standard TX pin
#define GPS_BAUD 115200
#define PREDICTIVE_DELTA_LIMIT 5.0 // Max delta seconds for bar display
#define GPS_SPEED_DEADZONE 2.0     // km/h threshold to ignore GPS noise
#define GPS_MIN_SATS                                                           \
  4 // Minimum satellites for reliable 3D fix (4 required for racing accuracy)
#define GPS_MAX_PDOP 5.0 // Maximum Dilution of Precision (lower is better)
#define DEFAULT_BEACON_WIDTH 50 // Default Beacon Width in meters
// #define PIN_LIGHT_SENSOR 34
#define PIN_BATTERY 34 // Enabled per user request
#define BATTERY_VOLTAGE_MAX 4.2
#define BATTERY_VOLTAGE_MIN 3.0
#define BATTERY_DIVIDER_RATIO                                                  \
  2.44 // Reverted from 3.30 which caused 5.6V readings

// ==========================================
// VOLTMETER MOTOR (12V) — Pembagi Tegangan
// ==========================================
// WIRING:
//   Aki 12V (+) → Resistor R1 (atas) → titik tengah → GPIO36 (ADC1_CH0)
//   Titik tengah juga tersambung ke Resistor R2 → GND
//   GPIO36 hanya terima V ≤ ~3.2V; JANGAN sambungkan 12V langsung!
// R1=33kΩ, R2=5.6kΩ → Faktor = (33000+5600)/5600 = 6.89
// ≈ 14.2V motor → 2.06V aman; 22V max ≈ 3.19V
#define PIN_VOLTMETER 36
#define VOLTMETER_RATIO 6.89f // (R1+R2)/R2, sesuaikan dgn resistor Anda
#define VOLTMETER_SAMPLES 16  // jumlah sampel untuk rata-rata

// ==========================================
// SYSTEM CONSTANTS
// ==========================================
// Landscape Mode
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320
#define STATUS_BAR_HEIGHT 25 // Fixed height for status bar (includes line gap)

// Touch Feel Standardization
#define TOUCH_DEBOUNCE_MS 150   // Default for menus and buttons
#define TOUCH_KEYBOARD_MS 100   // Fast response for keyboard
#define TOUCH_LONG_PRESS_MS 500 // For critical/hold actions

#define SERIAL_DEBUG_BAUD 115200

// ==========================================
// DEBUG CONFIGURATION
// ==========================================
#define ENABLE_DEBUG_LOGS 0 // Set to 1 to enable diagnostic logs

#if ENABLE_DEBUG_LOGS
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) ((void)0)
#define DEBUG_PRINTLN(...) ((void)0)
#define DEBUG_PRINTF(...) ((void)0)
#endif

// UI Colors (Much Racing / Dark Theme)
#define COLOR_BG 0x0000        // Black
#define COLOR_TEXT 0xFFFF      // White
#define COLOR_ACCENT 0x04DF    // Blue-Green (RaceBox Teal)
#define COLOR_PRIMARY 0x04DF   // Primary UI color (same as accent)
#define COLOR_HIGHLIGHT 0xF800 // Red
#define COLOR_SECONDARY 0x7BEF // Greyish

// ==========================================
// UI FONT CONFIGURATION
// ==========================================
// Font Sizes (1=Tiny, 2=Small, 3=Medium, 4=Large, 6=Huge, 7=Massive)
// Optimized for 480x320 display
#define FONT_SIZE_STATUS_BAR 2  // Increased from 1 for better visibility
#define FONT_SIZE_SPLASH_TEXT 1 // Reduced to 1 for smaller text

// Menu & Navigation
#define FONT_SIZE_MENU_TITLE 4 // Increased from 3 for prominence
#define FONT_SIZE_MENU_ITEM 4  // Increased from 3 for better readability

// Speedometer & Racing
#define FONT_SIZE_LAP_SPEED 7 // Increased from 6 for main display
#define FONT_SIZE_LAP_TIME 3  // Increased from 2
#define FONT_SIZE_LAP_LIST 2  // Kept at 2 (fits more items)
#define FONT_SIZE_LAP_BEST 4  // Increased from 3

// General UI Elements
#define FONT_SIZE_HEADER 3 // Increased from 2
#define FONT_SIZE_BUTTON 3 // Increased from 2
#define FONT_SIZE_BODY 2   // Increased from 1
#define FONT_SIZE_LABEL 2  // Increased from 1
#define FONT_SIZE_VALUE 3  // Increased from 2

// API Configuration
#define API_URL "https://web-muchracing.vercel.app/api/device/sync"
#define MQTT_BROKER "broker.emqx.io"
#define MQTT_PORT 1883
#define MQTT_TOPIC_PREFIX "muchracing/telemetry/"

#endif
