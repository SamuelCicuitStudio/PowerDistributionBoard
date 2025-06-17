#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================================================
// Debugging and Logging Configuration
// ==================================================

#define DEBUGMODE                      true
#define ENABLE_SERIAL_DEBUG

#ifdef ENABLE_SERIAL_DEBUG
  #define DEBUG_PRINT(x)              if (DEBUGMODE) Serial.print(x)
  #define DEBUG_PRINTF                Serial.printf
  #define DEBUG_PRINTLN(x)            if (DEBUGMODE) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTF(...)
  #define DEBUG_PRINTLN(x)
#endif

#define CONFIG_PARTITION               "config"    // NVS partition name

// ==================================================
// Device Configuration Keys & Defaults for Preferences
// ==================================================

// ---------- Wi-Fi & Storage ----------
#define DEVICE_WIFI_HOTSPOT_NAME_KEY   "APNAM"     // Hotspot SSID key
#define DEVICE_AP_AUTH_PASS_KEY        "APPSS"     // Hotspot password key
#define RESET_FLAG                     "RTFLG"     // Preferences reset flag

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
#define DESIRED_OUTPUT_VOLTAGE_KEY     "DOUTV"    // User-defined target output voltage
#define TEMP_SENSOR_COUNT_KEY          "TMPCNT"    // Number of temperature sensors detected

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
#define OUT10_ACCESS_KEY              "OUT10F"

// ==================================================
// Default Values for Preferences
// ==================================================

// ---------- Wi-Fi Defaults ----------
#define DEVICE_WIFI_HOTSPOT_NAME       "PDis_"   // Default SSID
#define DEVICE_AP_AUTH_PASS_DEFAULT    "1234567890"     // Default password

// ---------- Timing & Behavior Defaults ----------
#define DEFAULT_ON_TIME                10               // ms
#define DEFAULT_OFF_TIME               9                // ms
#define DEFAULT_INRUSH_DELAY           100              // ms
#define DEFAULT_LED_FEEDBACK           true             // true = LED feedback enabled
#define DEFAULT_TEMP_THRESHOLD         75.0f            // °C
#define DEFAULT_CHARGE_RESISTOR_OHMS   10000.0f         // Ohms
#define DEFAULT_AC_FREQUENCY           50               // Hz
#define DEFAULT_AC_VOLTAGE             230.0f           // Volts
#define DEFAULT_DC_VOLTAGE             325.0f           // Volts
#define DEFAULT_DESIRED_OUTPUT_VOLTAGE 180.0f     // Volts (safe default power level)
#define DEFAULT_TEMP_SENSOR_COUNT      4           // Default to 4 sensor unless discovered otherwise


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
#define DEFAULT_USER_ID                "user0"          // Default customer username
#define DEFAULT_USER_PASS              "admin123"               // Blank password (set via UI)

// ==================================================
// APMODE Definitions
// ==================================================

#define LOCAL_IP                       IPAddress(192, 168, 4, 1)
#define GATEWAY                        IPAddress(192, 168, 4, 1)
#define SUBNET                         IPAddress(255, 255, 255, 0)
#define INACTIVITY_TIMEOUT_MS 300000  // 5 minutes
// ==================================================
// Time Configuration
// ==================================================

#define SERIAL_BAUD_RATE               115200

// ==================================================
// Switch Configuration
// ==================================================

#define SW_USER_BOOT_PIN               0                    // Boot button pin
#define POWER_ON_SWITCH_PIN            6                    // Physical power button

// ==================================================
// LED Configuration
// ==================================================

#define READY_LED_PIN                  16                   // System ready indicator
#define POWER_OFF_LED_PIN              2                    // Power off indicator LED

// ==================================================
// Floor Heater LED Indicators
// ==================================================

// 8 LEDs connected to a shift register (74HC595)
#define SHIFT_SER_PIN                  10                   // Serial data input (SER)
#define SHIFT_SCK_PIN                  11                   // Shift clock (SCK)
#define SHIFT_RCK_PIN                  12                   // Latch clock (RCK)

// 2 LEDs controlled directly via GPIO
#define FL06_LED_PIN                   15
#define FL08_LED_PIN                   9

// ==================================================
// Sensor & Detection Pins
// ==================================================

#define DETECT_12V_PIN                 4                    // Detect 12V input presence
#define ACS_LOAD_CURRENT_VOUT_PIN      5                    // Analog output from ACS781 current sensor
#define CAPACITOR_ADC_PIN              13                   // ADC input for monitoring capacitor voltage
#define CHARGE_THRESHOLD_PERCENT       85.0f                // Percentage threshold for capacitor charge
#define ONE_WIRE_BUS                   3                    // DS18B20 temperature sensor bus
#define FLAG_INPUT_PIN                 3                    // Input pin for external flag signal

// ==================================================
// Nichrome Wire Control - Opto Enable Pins (active low)
// ==================================================

#define ENA01_E_PIN                    20
#define ENA02_E_PIN                    38
#define ENA03_E_PIN                    8
#define ENA04_E_PIN                    18
#define ENA05_E_PIN                    17
#define ENA06_E_PIN                    39
#define ENA07_E_PIN                    40
#define ENA08_E_PIN                    7
#define ENA09_E_PIN                    41
#define ENA10_E_PIN                    42

// ==================================================
// PWM Control Configuration
// ==================================================

#define INA_OPT_PWM_PIN                1                    // INA_OPT controls nichrome overdrive
#define INA_OPT_PWM_CHANNEL            1

#define FAN_PWM_PIN                    14                   // FAN output
#define FAN_PWM_CHANNEL                2

#define PWM_FREQ                       50000                // 50 kHz for all PWM signals
#define PWM_RESOLUTION                 8                    // 8-bit resolution (0–255)
#define PWM_DUTY_CYCLE                 173                  // Default duty (68%)

// ==================================================
// Capacitor Bank Charging Control
// ==================================================

#define RELAY_CONTROL_PIN              21                   // Relay controlling capacitor input power
#define INA_RELAY_BYPASS_PIN           47                   // INA controls bypass MOSFET for inrush resistor

#endif // CONFIG_H
