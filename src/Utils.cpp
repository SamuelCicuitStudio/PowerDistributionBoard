#include "Utils.h"

volatile WiFiStatus wifiStatus = WiFiStatus::NotConnected;
volatile bool StartFromremote =  false;


// RTOS task that blinks a pin and restores its original state
void BlinkTask(void* parameter) {
    BlinkParams* params = static_cast<BlinkParams*>(parameter);
    
    DEBUG_PRINTF("[BLINK] Pin %u blink for %dms (original state: %s)\n", params->pin, params->durationMs, params->originalState ? "HIGH" : "LOW");

    pinMode(params->pin, OUTPUT);  // Ensure output mode
    digitalWrite(params->pin, !params->originalState);  // Toggle state
    vTaskDelay(pdMS_TO_TICKS(params->durationMs));      // Wait
    digitalWrite(params->pin, params->originalState);   // Restore state

    delete params;     // Free allocated memory
    vTaskDelete(NULL); // Self-terminate task
}

// Creates the RTOS blink task
void blink(uint8_t pin, int durationMs) {
    pinMode(pin, INPUT);  // Temporarily set as input to read safely
    bool currentState = digitalRead(pin);
    pinMode(pin, OUTPUT); // Return to output mode

    auto* params = new BlinkParams{pin, durationMs, currentState};

    xTaskCreate(
        BlinkTask,           // Task function
        "BlinkTask",         // Task name
        2048,                // Stack size
        params,              // Parameters
        1,                   // Priority
        nullptr              // Task handle
    );
}

void disableAllPins() {
    DEBUG_PRINTLN("[DEBUG] Disabling all system control pins...");

    // Disable Nichrome wire control (active HIGH)
    DEBUG_PRINTLN("Disabling Nichrome wire control...");
    digitalWrite(ENA01_E_PIN, LOW);
    digitalWrite(ENA02_E_PIN, LOW);
    digitalWrite(ENA03_E_PIN, LOW);
    digitalWrite(ENA04_E_PIN, LOW);
    digitalWrite(ENA05_E_PIN, LOW);
    digitalWrite(ENA06_E_PIN, LOW);
    digitalWrite(ENA07_E_PIN, LOW);
    digitalWrite(ENA08_E_PIN, LOW);
    digitalWrite(ENA09_E_PIN, LOW);
    digitalWrite(ENA10_E_PIN, LOW);

    // LEDs off
    DEBUG_PRINTLN("Turning off LEDs...");
    digitalWrite(READY_LED_PIN, LOW);
    digitalWrite(POWER_OFF_LED_PIN, LOW);
    digitalWrite(FL06_LED_PIN, LOW);
    digitalWrite(FL08_LED_PIN, LOW);

    // Capacitor charge control
    DEBUG_PRINTLN("Disabling capacitor charge controls...");
    digitalWrite(RELAY_CONTROL_PIN, LOW);
    digitalWrite(INA_RELAY_BYPASS_PIN, LOW);

    // PWM outputs stop (set LOW)
    DEBUG_PRINTLN("Stopping PWM outputs...");
    digitalWrite(INA_OPT_PWM_PIN, LOW);
    digitalWrite(FAN_PWM_PIN, LOW);

    // Shift register control lines (safe state LOW)
    DEBUG_PRINTLN("Resetting shift register control lines...");
    digitalWrite(SHIFT_SER_PIN, LOW);
    digitalWrite(SHIFT_SCK_PIN, LOW);
    digitalWrite(SHIFT_RCK_PIN, LOW);

    DEBUG_PRINTLN("[DEBUG] All control pins disabled.");
}

