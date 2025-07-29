#ifndef CONFIG_H
#define CONFIG_H

/*
 * Project Configuration Header - TX Master ESP32
 * 
 * Original code base: https://github.com/MoritzNelle/TX_Passive_Thermal_GCT
 * Enhanced by Josias Kern using GitHub Copilot (GPT-4)
 * 
 * This file contains all configurable parameters for the TX Master ESP32
 */

// ===== DEVICE CONFIGURATION =====
#define MASTER_DEVICE_ID        "TX_MASTER_001"
#define FIRMWARE_VERSION        "1.2.0"
#define BUILD_DATE              __DATE__ " " __TIME__

// ===== TIMING CONFIGURATION =====
#define SEND_TIMEOUT_MS         1000        // Timeout for servant response
#define LOG_INTERVAL_MS         10000       // Log interval (>=10000ms)
#define PING_CHECK_INTERVAL_MS  1000        // Connection check interval
#define TEMP_UPDATE_INTERVAL_MS 10000       // Temperature display update

// ===== WIFI & NTP CONFIGURATION =====
#define WIFI_SSID               "VodafoneMobileWiFi-A8E1"
#define WIFI_PASSWORD           "I5IJ4ij4"
#define NTP_SERVER              "pool.ntp.org"
#define GMT_OFFSET_SEC          7200        // GMT+2 for Amsterdam summer time
#define DAYLIGHT_OFFSET_SEC     0           // Already included in GMT offset
#define WIFI_CONNECTION_TIMEOUT 20          // WiFi connection attempts
#define NTP_SYNC_TIMEOUT        10          // NTP sync attempts

// ===== HARDWARE CONFIGURATION =====
#define CS_PIN                  5           // SD Card Chip Select
#define LED_PIN                 4           // Status LED
#define BUTTON_PIN              0           // Button input
#define LCD_ADDRESS             0x27        // I2C LCD address
#define LCD_COLS                20          // LCD columns
#define LCD_ROWS                4           // LCD rows

// ===== WATCHDOG CONFIGURATION =====
#define WATCHDOG_TIMEOUT_SEC    30          // Watchdog timeout

// ===== SERVANT ESP32 MAC ADDRESSES =====
// Update these with your actual servant device MAC addresses
#define SERVANT_1_MAC           {0x48, 0xE7, 0x29, 0x8C, 0x79, 0x68}
#define SERVANT_2_MAC           {0x48, 0xE7, 0x29, 0x8C, 0x73, 0x18}
#define SERVANT_3_MAC           {0x4C, 0x11, 0xAE, 0x65, 0xBD, 0x54}
#define SERVANT_4_MAC           {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50}

// ===== FILE CONFIGURATION =====
#define SD_FILENAME             "/data_master.csv"
#define CSV_HEADER              "timestamp,target_no,sensor_no,temperature"

// ===== ESP-NOW ACTION IDs =====
#define ACTION_CONNECTION_TEST  1001
#define ACTION_START_LOGGING    1002
#define ACTION_STOP_LOGGING     1003
#define ACTION_TEMP_REQUEST     3001
#define ACTION_TEMP_RESPONSE    2001

// ===== DEBUG CONFIGURATION =====
#ifdef DEBUG
    #define DEBUG_PRINT(x)      Serial.print(x)
    #define DEBUG_PRINTLN(x)    Serial.println(x)
    #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H
