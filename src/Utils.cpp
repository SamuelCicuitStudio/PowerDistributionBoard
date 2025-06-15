#include "Utils.h"

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
        1024,                // Stack size
        params,              // Parameters
        1,                   // Priority
        nullptr              // Task handle
    );
}
