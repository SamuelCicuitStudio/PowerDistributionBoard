#include <Arduino.h>             // Core Arduino library for ESP32
 
#define DEFAULT_CHARGE_RESISTOR_OHMS 10000.0f  // 10 kΩ
#define DEFAULT_AC_FREQUENCY          50        // Default to 50 Hz
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
// Capacitor Bank Charging Control
// ==================================================

#define ENA_E_PIN                      35                   // Enable pin for capacitor bank charging control (UCC27524DR)
#define INA_E_PIN                      36                   // Enable pin for capacitor bank charging control (UCC27524DR)
#define OPT_E_PIN                      1
#define PWM_CH     0         // PWM channel (0–15 for ESP32)
#define PWM_FREQ   2000      // 2 kHz
#define PWM_RES    8         // 8-bit resolution (0–255)
// Pin lists
const int nichromePins[10] = {
    ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
    ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA010_E_PIN
};
const int floorLedPins[10] = {
    FL01_LED_PIN, FL02_LED_PIN, FL03_LED_PIN, FL04_LED_PIN, FL05_LED_PIN,
    FL06_LED_PIN, FL07_LED_PIN, FL08_LED_PIN, FL09_LED_PIN, FL10_LED_PIN
};

    volatile  bool  systemOn =false,systemOnwifi =false;
    volatile  bool  ledFeedbackEnabled  = false;
    uint32_t             onTime  = 3000;
    uint32_t             offTime   = 200;

    // Voltage calibration
    uint8_t                AcFreq = 50;            // frequency
    volatile  float        measuredVoltage =0.0f;            // Volts
    int                  calibMax = 1;                   // ADC raw peak
    float                chargeResistorOhs = DEFAULT_CHARGE_RESISTOR_OHMS;          // Ω from config
    volatile  bool                 lastState;
// ==============================
// Debounce Timing
// ==============================
const unsigned long DEBOUNCE_TIME_MS = 200;
volatile unsigned long lastWifiRestartTime = 0;
volatile unsigned long lastPowerSwitchTime = 0;
volatile unsigned long last230VACCheckTime = 0;

void setup() {
    Serial.begin(115200);  ///< Initialize serial communication for debugging

    // 2) Bypass driver pins (UCC27524): ENA_E_PIN = enable, INA_E_PIN = PWM input
    //pinMode(ENA_E_PIN, OUTPUT);   digitalWrite(ENA_E_PIN, LOW);
    pinMode(INA_E_PIN, OUTPUT);   digitalWrite(INA_E_PIN, LOW);// DEACTIVATE THE BYPASS RESISTOR
    pinMode(OPT_E_PIN, OUTPUT); //  digitalWrite(OPT_E_PIN, HIGH);

    // 1) Nichrome outputs & floor LEDs off
    for (auto p : nichromePins)   pinMode(p, OUTPUT), digitalWrite(p, LOW);
    for (auto p : floorLedPins)   pinMode(p, OUTPUT), digitalWrite(p, LOW);

      // Set up the PWM channel
    ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);

    // Attach the pin to the PWM channel
    ledcAttachPin(OPT_E_PIN, PWM_CH);

    // Send 50% duty cycle
    ledcWrite(PWM_CH, 75);  // 128 = 50% of 255

    // 5) Ready / Power-off LEDs
    pinMode(READY_LED_PIN,    OUTPUT); digitalWrite(READY_LED_PIN,    LOW);
    pinMode(POWER_OFF_LED_PIN, OUTPUT); digitalWrite(POWER_OFF_LED_PIN, HIGH);

    // 6) Inputs
    pinMode(POWER_ON_SWITCH_PIN, INPUT);
    lastState = digitalRead(POWER_ON_SWITCH_PIN);
    pinMode(DETECT_12V_PIN, INPUT_PULLDOWN);

    // ADC peak calibration over one AC cycle using millis()
    unsigned long periodMs = 1000UL / AcFreq;      // Duration of one AC cycle in milliseconds
    unsigned long startTime = millis();
    calibMax = 1;

    while (millis() - startTime < periodMs) {
        int raw = analogRead(CAPACITOR_ADC_PIN);
        if (raw > calibMax) {
            calibMax = raw;
        }
        delay(1);  // pause ~1 ms between samples (similar to vTaskDelay(1))
    }

}

void loop() {
    // Small delay to avoid multiple triggers in quick succession (debounce)
    // Wait for gate-drive rail (12 V)

    Serial.println("Waiting for 12V detection...");
    while (digitalRead(DETECT_12V_PIN) == LOW) {
        delay(500);
    }
    Serial.println("DETECTED 12V");

    constexpr int SAMPLES = 20;

    while (true) {
        Serial.println("Starting voltage measurement cycle...");
        long sum = 0;
        for (int i = 0; i < SAMPLES; ++i) {
            int rawValue = analogRead(CAPACITOR_ADC_PIN);
            sum += rawValue;
            Serial.print("  Sample ");
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.println(rawValue);
            delay(2);
        }

        float avg = sum / float(SAMPLES);
        Serial.print("Average ADC value: ");
        Serial.println(avg);

        measuredVoltage =
            (avg / 4095.0f) * 3.3f * ((470000.0f + 4700.0f) / 4700.0f);
        Serial.print("Calculated voltage (V): ");
        Serial.println(measuredVoltage);

        float pct = (avg / float(calibMax)) * 100.0f;
        Serial.print("Percentage of calibrated max: ");
        Serial.print(pct);
        Serial.println(" %");

        if (pct >= 80.0f) {
            Serial.println("Voltage >= 80% of max. System READY.");
            digitalWrite(READY_LED_PIN,    HIGH);
            digitalWrite(POWER_OFF_LED_PIN, LOW);

            // Now allow user to press START
            Serial.println("Waiting for user to press START...");
            while (digitalRead(POWER_ON_SWITCH_PIN)) {
                delay(255);
            }
            Serial.println("System Started");
            delay(2222);
            systemOn =  true;
            ledFeedbackEnabled = true;
            // Replace the FreeRTOS task loop with a simple blocking loop using delay()
            while (true) {
                if (systemOn) {
                    for (int i = 0; i < 10; ++i) {
                        // Turn on the nichrome heater and optional LED feedback
                        digitalWrite(nichromePins[i], HIGH);
                        if (ledFeedbackEnabled) {
                            digitalWrite(floorLedPins[i], HIGH);
                        }

                        // Keep it on for onTime milliseconds
                        delay(onTime);

                        // Turn off the nichrome heater and optional LED feedback
                        digitalWrite(nichromePins[i], LOW);
                        if (ledFeedbackEnabled) {
                            digitalWrite(floorLedPins[i], LOW);
                        }

                        // Keep it off for offTime milliseconds
                        delay(offTime);
                    }
                } else {
                    // If the system is off, pause here for 1 second before checking again
                    delay(1000);
                }
            }

            // Exit this one-shot voltage monitor
            break;
        } else {
            Serial.println("Voltage < 80% of max. System NOT ready.");
            digitalWrite(READY_LED_PIN,    LOW);
            digitalWrite(POWER_OFF_LED_PIN, HIGH);
        }

        Serial.println("Voltage check complete. Retrying in 300 ms...");
        delay(300);
    }
}
