#ifndef CONFIG_H
#define CONFIG_H

// ==================================================
// Device Configuration Keys for ESP32 Preferences
// ==================================================

#define DEVICE_WIFI_HOTSPOT_NAME_KEY   "APNAM"             // Partition key for Hotspot name
#define DEVICE_WIFI_HOTSPOT_NAME       "Serto_PDis01"      // Default Hotspot name

#define DEVICE_AP_AUTH_PASS_KEY        "APPSS"             // Partition key: Default WiFi AP authentication password
#define DEVICE_AP_AUTH_PASS_DEFAULT    "1234567890"

#define RESET_FLAG                     "RTFLG"             // Flag indicating a device reset

#define CONFIG_PARTITION               "config"            // Partition name for device configuration data

#define LAST_TIME_SAVED                "LSTV"              // Last saved timestamp
#define CURRENT_TIME_SAVED             "CAVTT"             // Current saved timestamp

#define SLAVE_CONFIG_PATH              "/config/SlaveConfig.json"  ///< Path for slave configuration

// ==================================================
// User Preferences for Cycle, Inrush & Temperature Management
// ==================================================

#define ON_TIME_KEY                    "ONTIM"             // Key for storing the ON time duration of cycles (in milliseconds)
#define OFF_TIME_KEY                   "OFFTIM"            // Key for storing the OFF time duration of cycles (in milliseconds)
#define INRUSH_DELAY_KEY               "INSHDY"            // Key for storing the delay used to control the inrush current (in milliseconds)
#define LED_FEEDBACK_KEY               "LEDFB"             // Key for enabling/disabling LED feedback during nichrome wire activation
#define TEMP_THRESHOLD_KEY             "TMPTH"             // Key for storing the over-temperature shutdown threshold (in Celsius)
#define CHARGE_RESISTOR_KEY            "CHRES"
#define AC_FREQUENCY_KEY              "ACFRQ"   // Key for line frequency (Hz)     
// Default Values for ON_TIME, OFF_TIME, INRUSH_DELAY, LED Feedback & Temperature Threshold
#define DEFAULT_ON_TIME                10                  // Default ON time for cycles in ms
#define DEFAULT_OFF_TIME               9                   // Default OFF time for cycles in ms
#define DEFAULT_INRUSH_DELAY           100                 // Default inrush delay in ms
#define DEFAULT_LED_FEEDBACK           true                // Default state of LED feedback (true = enabled)
#define DEFAULT_TEMP_THRESHOLD         75.0f               // Default temperature threshold for shutdown in °C
 
#define DEFAULT_CHARGE_RESISTOR_OHMS 10000.0f  // 10 kΩ
#define DEFAULT_AC_FREQUENCY          50        // Default to 50 Hz

// ==================================================
// APMODE Definitions
// ==================================================

#define LOCAL_IP                       IPAddress(192, 168, 4, 1)   // Fixed AP IP address
#define GATEWAY                        IPAddress(192, 168, 4, 1)   // Usually the same as LOCAL_IP
#define SUBNET                         IPAddress(255, 255, 255, 0) // Subnet mask for the AP

// ==================================================
// Time Configuration
// ==================================================

#define DEFAULT_CURRENT_TIME_SAVED     1736121600           // Default current time (Unix timestamp)
#define DEFAULT_LAST_TIME_SAVED        1736121600           // Default last saved time (Unix timestamp)
#define TIMEOFFSET                     3600                 // Time offset for UTC+1 (3600 seconds)
#define NTP_SERVER                     "pool.ntp.org"       // NTP server address for time synchronization
#define NTP_UPDATE_INTERVAL            60000                // Interval for NTP updates (ms)
#define SERIAL_BAUD_RATE               115200               // Serial communication baud rate
#define SLEEP_TIMER                    1800000              // WiFi Sleep timer interval in ms (30 min)

// ==================================================
// Debugging and Logging Configuration
// ==================================================

#define DEBUGMODE                      true                // Set to true to enable debugging mode
#define LOGFILE_PATH                   "/Log/log.json"      // Default log file path

// ==================================================
// Switch Configuration
// ==================================================

#define SW_USER_BOOT_PIN               0                    // Boot button
#define POWER_ON_SWITCH_PIN            6                    // Physical power button

// ==================================================
// LED Configuration
// ==================================================

#define READY_LED_PIN                  16                   // Indicates system ready
#define POWER_OFF_LED_PIN              2                    // Power OFF indicator
                   
// ==================================================
// Floor Heater LED Indicators
// ==================================================

#define FL01_LED_PIN                   48
#define FL02_LED_PIN                   47
#define FL03_LED_PIN                   21
#define FL04_LED_PIN                   14
#define FL05_LED_PIN                   13
#define FL06_LED_PIN                   12
#define FL07_LED_PIN                   11
#define FL08_LED_PIN                   10
#define FL09_LED_PIN                   9
#define FL10_LED_PIN                   15

// ==================================================
// Sensor & Detection Pins
// ==================================================

#define DETECT_12V_PIN                 4                    // Input pin to detect 12V presence
#define CAPACITOR_ADC_PIN              5                    // ADC pin to measure capacitor voltage
#define CHARGE_THRESHOLD_PERCENT     85.0f                  // ADC pin to measure capacitor voltage
#define ONE_WIRE_BUS                   3                    // tTemperature sensor
// ==================================================
// Nichrome Wire Control - Opto Enable Pins
// ==================================================

#define ENA01_E_PIN                    37
#define ENA02_E_PIN                    38
#define ENA03_E_PIN                    8
#define ENA04_E_PIN                    18
#define ENA05_E_PIN                    17
#define ENA06_E_PIN                    39
#define ENA07_E_PIN                    40
#define ENA08_E_PIN                    7
#define ENA09_E_PIN                    41
#define ENA010_E_PIN                   42

// ==================================================
// Nichrome Wire Control - PWM
// ==================================================

#define INA_OPT_PWM_PIN                1                    // PWM control for heating
#define INA_E_PIN                      2                    // PWM control for second channel (INA_E_PIN)
#define PWM_FREQ                       50000                 // PWM frequency set to 5 kHz
#define PWM_RESOLUTION                 8                    // 8-bit resolution for PWM (0-255)
#define PWM_CHANNEL                    1                    // PWM Channel 1 for INA_OPT_PWM_PIN control
#define PWM_DUTY_CYCLE                 173                  // Duty cycle set to 68% (173 out of 255)

// ==================================================
// Capacitor Bank Charging Control
// ==================================================

#define ENA_E_PIN                      35                   // Enable pin for capacitor bank charging control (UCC27524DR)
#define INA_E_PIN                      36                   // Enable pin for capacitor bank charging control (UCC27524DR)

#endif // CONFIG_H
