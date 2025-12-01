/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
// ===== Core Arduino / C++ =====
#include <Arduino.h>
#include <cstring>
#include <algorithm>
#include <time.h>
#include <pgmspace.h>

// ===== ESP32 & RTOS =====
#include "esp_err.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "esp_log.h"
#include <stdarg.h>
// ===== WiFi & Network =====
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPAsyncWebServer.h>
// ===== File System & JSON =====
#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include "system/Utils.h"
#include <math.h> // for lroundf
#include "esp_wifi.h"

#define CONFIG_PARTITION               "config"    // NVS partition name

// ==================================================
// Device Configuration Keys & Defaults for Preferences
// ==================================================

// ---------- Wi-Fi & Storage ----------
#define DEVICE_WIFI_HOTSPOT_NAME_KEY   "APNAM"     // Hotspot SSID key
#define DEVICE_AP_AUTH_PASS_KEY        "APPSS"     // Hotspot password key
#define RESET_FLAG                     "RTFLG"     // Preferences reset flag

#define STA_SSID_KEY                   "WIFSSD"    // Station Mode SSID key
#define STA_PASS_KEY                   "WIFPASS"   // Station Mode password key
// ---------- Authentication (Max 1 Admin, 1 User) ----------
#define ADMIN_ID_KEY                   "ADMID"     // Admin login username
#define ADMIN_PASS_KEY                 "ADMPW"     // Admin login password
#define USER_ID_KEY                    "USRID"     // Single customer login username
#define USER_PASS_KEY                  "USRPW"     // Single customer login password

// ---------- Preference Keys ----------
#define ON_TIME_KEY                    "ONTIM"     // ON time duration key
#define OFF_TIME_KEY                   "OFFTIM"    // OFF time duration key
#define INRUSH_DELAY_KEY               "INSHDY"    // Inrush delay duration key
#define LED_FEEDBACK_KEY               "LEDFB"     // LED feedback toggle key
#define TEMP_THRESHOLD_KEY             "TMPTH"     // Over-temperature shutdown key
#define CHARGE_RESISTOR_KEY            "CHRES"     // Charge resistor value key
#define AC_FREQUENCY_KEY               "ACFRQ"     // AC line frequency key
#define AC_VOLTAGE_KEY                 "ACVLT"     // AC line voltage key
#define DC_VOLTAGE_KEY                 "DCVLT"     // Target DC output voltage key
#define DESIRED_OUTPUT_VOLTAGE_KEY     "DOUTV"     // User-defined target output voltage
#define TEMP_SENSOR_COUNT_KEY          "TMNT"    // Number of temperature sensors detected
#define RTC_CURRENT_EPOCH_KEY          "RCUR"    // Last known epoch persisted
#define RTC_PRESLEEP_EPOCH_KEY         "RSLP"   // Epoch saved before deep sleep
#define RTC_DEFAULT_EPOCH              0ULL

// ---------- Device Identity & Versions (NVS keys, <= 6 chars) ----------
#define DEV_ID_KEY                     "DEVID"     // Human-readable device id
#define DEV_SW_KEY                     "DVSWV"     // Software version
#define DEV_HW_KEY                     "DVHWV"     // Hardware version

// Version values (update as you rev firmware / hardware)
#define DEVICE_SW_VERSION              "1.7.0"
#define DEVICE_HW_VERSION              "2.2.5"

// Compile-time guard: enforce max length for new NVS keys (<= 6 chars)
#define ASSERT_NVS_KEY_LEN(k) static_assert(sizeof(k) - 1 <= 6, "NVS key must be <= 6 chars")
ASSERT_NVS_KEY_LEN(DEV_ID_KEY);
ASSERT_NVS_KEY_LEN(DEV_SW_KEY);
ASSERT_NVS_KEY_LEN(DEV_HW_KEY);

// ---------- Output Access Flags ----------
#define OUT01_ACCESS_KEY               "OUT1F"
#define OUT02_ACCESS_KEY               "OUT2F"
#define OUT03_ACCESS_KEY               "OUT3F"
#define OUT04_ACCESS_KEY               "OUT4F"
#define OUT05_ACCESS_KEY               "OUT5F"
#define OUT06_ACCESS_KEY               "OUT6F"
#define OUT07_ACCESS_KEY               "OUT7F"
#define OUT08_ACCESS_KEY               "OUT8F"
#define OUT09_ACCESS_KEY               "OUT9F"
#define OUT10_ACCESS_KEY               "OUT10F"
// ---------- Nichrome Wire Resistance (Ohms) ----------
#define DEFAULT_WIRE_RES_OHMS  44.0f   // default for all 10 wires

// 6-char NVS keys per wire
#define R01OHM_KEY  "R01OHM"
#define R02OHM_KEY  "R02OHM"
#define R03OHM_KEY  "R03OHM"
#define R04OHM_KEY  "R04OHM"
#define R05OHM_KEY  "R05OHM"
#define R06OHM_KEY  "R06OHM"
#define R07OHM_KEY  "R07OHM"
#define R08OHM_KEY  "R08OHM"
#define R09OHM_KEY  "R09OHM"
#define R10OHM_KEY  "R10OHM"

#define IDLE_CURR_KEY "IIDLE"
#define DEFAULT_IDLE_CURR 0.0f
// ---------- Target Resistance for all  Output (Ohms) ----------
#define DEFAULT_TARG_RES_OHMS  14.0f  // default target for all outputs
// 6-char NVS keys (one per output)
#define R0XTGT_KEY  "R0XTGT"  // OUTXX target resistance
#define WIRE_OHM_PER_M_KEY               "WOPERM"    // float: Î© per meter for installed nichrome
#define DEFAULT_WIRE_OHM_PER_M           2.0f        // 2 Î©/m nichrome (your current wire)
// NVS keys for persistent statistics (<= 6 chars each)

// Totals (lifetime)
#define PT_KEY_TOTAL_ENERGY_WH          "STWH"    // total energy [Wh]
#define PT_KEY_TOTAL_SESSIONS           "STCNT"   // total sessions
#define PT_KEY_TOTAL_SESSIONS_OK        "STCSOK"  // total successful sessions

// Last session snapshot
#define PT_KEY_LAST_SESS_ENERGY_WH      "LSEWH"   // last session energy [Wh]
#define PT_KEY_LAST_SESS_DURATION_S     "LSDUR"   // last session duration [s]
#define PT_KEY_LAST_SESS_PEAK_W         "LSPKW"   // last session peak power [W]
#define PT_KEY_LAST_SESS_PEAK_A         "LSPKA"   // last session peak current [A]
// --- DS18B20 identity keys (hex string of 8 bytes) ---
#define TSB0ID_KEY  "TSB0ID"  // Board sensor #0 ROM
#define TSB1ID_KEY  "TSB1ID"  // Board sensor #1 ROM
#define TSHSID_KEY  "TSHSID"  // Heatsink ROM
#define TSMAP_KEY   "TSMEP"
// Default values for all stats
#define PT_DEF_TOTAL_ENERGY_WH          0.0f
#define PT_DEF_TOTAL_SESSIONS           0
#define PT_DEF_TOTAL_SESSIONS_OK        0

#define PT_DEF_LAST_SESS_ENERGY_WH      0.0f
#define PT_DEF_LAST_SESS_DURATION_S     0
#define PT_DEF_LAST_SESS_PEAK_W         0.0f
#define PT_DEF_LAST_SESS_PEAK_A         0.0f

// ==================================================
// Default Values for Preferences
// ==================================================

// ---------- Wi-Fi Defaults ----------
#define DEVICE_WIFI_HOTSPOT_NAME       "PDis_"         // Default SSID
#define DEVICE_AP_AUTH_PASS_DEFAULT    "1234567890"    // Default password
#define DEFAULT_STA_SSID               "nothing"       // Optional default station SSID (blank)
#define DEFAULT_STA_PASS               "nothing"       // Optional default station password (blank)

// ---------- Timing & Behavior Defaults ----------
#define DEFAULT_ON_TIME                10               // ms
#define DEFAULT_OFF_TIME               9                // ms
#define DEFAULT_INRUSH_DELAY           100              // ms
#define DEFAULT_LED_FEEDBACK           true             // true = LED feedback enabled
#define DEFAULT_TEMP_THRESHOLD         75.0f            // Â°C
#define DEFAULT_CHARGE_RESISTOR_OHMS   10000.0f         // Ohms
#define DEFAULT_AC_FREQUENCY           50               // Hz
#define DEFAULT_AC_VOLTAGE             230.0f           // Volts
#define DEFAULT_DC_VOLTAGE             325.0f           // Volts
#define DEFAULT_DESIRED_OUTPUT_VOLTAGE 180.0f           // Volts (safe default power level)
#define DEFAULT_TEMP_SENSOR_COUNT      12               // Default to 12 sensors unless discovered otherwise

// ---------- Output Access Defaults ----------
#define DEFAULT_OUT01_ACCESS           true
#define DEFAULT_OUT02_ACCESS           true
#define DEFAULT_OUT03_ACCESS           false
#define DEFAULT_OUT04_ACCESS           true
#define DEFAULT_OUT05_ACCESS           false
#define DEFAULT_OUT06_ACCESS           true
#define DEFAULT_OUT07_ACCESS           false
#define DEFAULT_OUT08_ACCESS           true
#define DEFAULT_OUT09_ACCESS           false
#define DEFAULT_OUT10_ACCESS           false

// ---------- Authentication Defaults ----------
#define DEFAULT_ADMIN_ID               "admin"          // Default admin username
#define DEFAULT_ADMIN_PASS             "admin123"       // Default admin password
#define DEFAULT_USER_ID                "user"           // Default customer username
#define DEFAULT_USER_PASS              "user123"        // Default customer password

// ==================================================
// APMODE Definitions
// ==================================================

#define LOCAL_IP                       IPAddress(192, 168, 4, 1)
#define GATEWAY                        IPAddress(192, 168, 4, 1)
#define SUBNET                         IPAddress(255, 255, 255, 0)
#define INACTIVITY_TIMEOUT_MS          180000  // 3 minutes

// ==================================================
// Switch Configuration
// ==================================================

#define SW_USER_BOOT_PIN               0                    // Boot button pin (IO0 / BOOT)
#define POWER_ON_SWITCH_PIN            6                    // Physical power button (IO6)

// ==================================================
// LED Configuration
// ==================================================

#define READY_LED_PIN                  16                   // READY indicator (schematic READY on IO16)
#define POWER_OFF_LED_PIN              2                    // OFF indicator (schematic OFF on IO2)
#define LED_R3_LED_PIN                 46                   // R3 indicator (IO46)

// ==================================================
// Floor Heater LED Indicators
// ==================================================

// 8 LEDs connected to a shift register (74HC595)
#define SHIFT_SER_PIN                  10                   // Serial data input (SER)
#define SHIFT_SCK_PIN                  8                    // Shift clock (SCK)
#define SHIFT_RCK_PIN                  9                    // Latch clock (RCK)

// 2 LEDs controlled directly via GPIO
#define FL06_LED_PIN                   18
#define FL08_LED_PIN                   11

// ==================================================
// Sensor & Detection Pins
// ==================================================

#define DETECT_12V_PIN                 4                    // Detect 12V input presence (IO4)
#define ACS_LOAD_CURRENT_VOUT_PIN      5                    // ACS781 current sensor VOUT (IO5)
#define CAPACITOR_ADC_PIN              15                   // Capacitor voltage ADC (IO15)
#define CHARGE_THRESHOLD_PERCENT       85.0f                // Percentage threshold for capacitor charge
#define ONE_WIRE_BUS                   3                    // DS18B20 temperature sensor bus (IO3)
#define FLAG_INPUT_PIN                 3                    // External flag input (IO3)

// ==================================================
// Nichrome Wire Control - Opto Enable Pins (active low)
// ==================================================

#define ENA01_E_PIN                    47
#define ENA02_E_PIN                    45   // updated to match schematic
#define ENA03_E_PIN                    12
#define ENA04_E_PIN                    13
#define ENA05_E_PIN                    7
#define ENA06_E_PIN                    17
#define ENA07_E_PIN                    39
#define ENA08_E_PIN                    38
#define ENA09_E_PIN                    41
#define ENA10_E_PIN                    40

// ==================================================
// PWM Control Configuration
// ==================================================

#define FAN1_PWM_PIN                   14                   // FAN1 output (IO14)
#define FAN1_PWM_CHANNEL               4                   // dedicated LEDC channel for FAN1
#define FAN2_PWM_PIN                   42                   // FAN2 output (IO42)
#define FAN2_PWM_CHANNEL               5                   // dedicated LEDC channel for FAN2

#define PWM_DUTY_CYCLE                 173                  // Default duty (68%)

// --- LEDC channel allocation (keep unique per peripheral) ---
#define BUZZER_PWM_CHANNEL             0
#define RGB_R_PWM_CHANNEL              1
#define RGB_G_PWM_CHANNEL              2
#define RGB_B_PWM_CHANNEL              3

// RGB LED PWM settings (used by RGBLed.cpp)
#define RGB_PWM_FREQ                   5000
#define RGB_PWM_RESOLUTION             8

static_assert(FAN1_PWM_CHANNEL != FAN2_PWM_CHANNEL, "FAN LEDC channels must differ");
static_assert(BUZZER_PWM_CHANNEL != RGB_R_PWM_CHANNEL &&
              BUZZER_PWM_CHANNEL != RGB_G_PWM_CHANNEL &&
              BUZZER_PWM_CHANNEL != RGB_B_PWM_CHANNEL, "Buzzer LEDC channel must be unique");
static_assert(BUZZER_PWM_CHANNEL != FAN1_PWM_CHANNEL &&
              BUZZER_PWM_CHANNEL != FAN2_PWM_CHANNEL, "Buzzer LEDC channel must be unique vs fans");
static_assert(RGB_R_PWM_CHANNEL != FAN1_PWM_CHANNEL &&
              RGB_R_PWM_CHANNEL != FAN2_PWM_CHANNEL, "RGB R channel must be unique vs fans");
static_assert(RGB_G_PWM_CHANNEL != FAN1_PWM_CHANNEL &&
              RGB_G_PWM_CHANNEL != FAN2_PWM_CHANNEL, "RGB G channel must be unique vs fans");
static_assert(RGB_B_PWM_CHANNEL != FAN1_PWM_CHANNEL &&
              RGB_B_PWM_CHANNEL != FAN2_PWM_CHANNEL, "RGB B channel must be unique vs fans");

// ==================================================
// Capacitor Bank Charging Control
// ==================================================

#define RELAY_CONTROL_PIN              21                   // Relay controlling capacitor input power (IO21)

// ==================================================
// Additional I/O
// ==================================================
#define BUZZER_PIN                     1      // Buzzer control output (IO1)
#define BUZLOW_KEY                     "BUZLOW"   // bool (activeLow)
#define BUZMUT_KEY                     "BUZMUT"   // bool (muted)

// --- Default configuration values ---
#define BUZLOW_DEFAULT                 false   // Default: buzzer active HIGH
#define BUZMUT_DEFAULT                 false   // Default: buzzer not muted

// ==================================================
//  RTOS CONFIGURATION: Task Priorities
// ==================================================
#define DEVICE_LOOP_TASK_PRIORITY         1
#define TASK_MONITOR_TASK_PRIORITY        1
#define TEMP_MONITOR_TASK_PRIORITY        1
#define LED_UPDATE_TASK_PRIORITY          4
#define CAP_VOLTAGE_TASK_PRIORITY         1
#define SWITCH_TASK_PRIORITY              1
#define TEMP_SENSOR_TASK_PRIORITY         1
#define BLINK_TASK_PRIORITY               1

// ==================================================
//  RTOS CONFIGURATION: Core Assignments
// ==================================================
#define DEVICE_LOOP_TASK_CORE             APP_CPU_NUM
#define TASK_MONITOR_TASK_CORE            APP_CPU_NUM
#define TEMP_MONITOR_TASK_CORE            APP_CPU_NUM
#define LED_UPDATE_TASK_CORE              PRO_CPU_NUM
#define CAP_VOLTAGE_TASK_CORE             APP_CPU_NUM
#define SWITCH_TASK_CORE                  PRO_CPU_NUM
#define TEMP_SENSOR_TASK_CORE             APP_CPU_NUM
// (Blink task runs on any core â€” no fixed core needed)

// ==================================================
//  RTOS CONFIGURATION: Stack Sizes (in words = 4 bytes)
// ==================================================
#define DEVICE_LOOP_TASK_STACK_SIZE       8192
#define TASK_MONITOR_TASK_STACK_SIZE      8192
#define TEMP_MONITOR_TASK_STACK_SIZE      8192
#define LED_UPDATE_TASK_STACK_SIZE        15360
#define CAP_VOLTAGE_TASK_STACK_SIZE       4096
#define SWITCH_TASK_STACK_SIZE            8192
#define TEMP_SENSOR_TASK_STACK_SIZE       8192
#define BLINK_TASK_STACK_SIZE             4096

// ==================================================
//  RTOS CONFIGURATION: Task Delay Intervals & Timing (ms)
// ==================================================
#define TEMP_MONITOR_TASK_DELAY_MS        2000    // 2s temperature check
#define LED_UPDATE_TASK_DELAY_MS          2000    // 2s LED feedback
#define CAP_VOLTAGE_TASK_DELAY_MS         200     // 200ms ADC sampling

#define SWITCH_TASK_LOOP_DELAY_MS         20      // Polling loop
#define SWITCH_TASK_CALL_DELAY_MS         500     // Re-check cycle
#define TAP_TIMEOUT_MS                    1500
#define HOLD_THRESHOLD_MS                 3000
#define TAP_WINDOW_MS                     1200

// ***********************************************
// Device operational states
// ***********************************************
enum class DeviceState {
    Idle,
    Running,
    Error,
    Shutdown
};

// ***********************************************
// Wi-Fi connection levels
// ***********************************************
enum class WiFiStatus {
    NotConnected,
    UserConnected,
    AdminConnected
};

// ***********************************************
// Globals shared with other modules
// NOTE: WiFiManager now updates wifiStatus under its own mutex,
// and Device updates StartFromremote under its own mutex. We
// keep them volatile here because multiple tasks read them.
extern volatile WiFiStatus wifiStatus;
extern volatile bool StartFromremote;

#endif // CONFIG_H


