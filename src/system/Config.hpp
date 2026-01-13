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
#include <IPAddress.h>
#include <ConfigNVS.hpp>

// Calibration data storage (model calibration history)
#define CALIB_MODEL_JSON_FILE          "/CalibModle.json"
#define CALIB_HISTORY_DIR              "/calib_history"
#define CALIB_HISTORY_PREFIX           "/calib_history/"
#define CALIB_HISTORY_EXT              ".json"

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
// (Blink task runs on any core — no fixed core needed)

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
