#include "SwitchManager.h"

SwitchManager* SwitchManager::instance = nullptr;
// Constructor
SwitchManager::SwitchManager(ConfigManager* Conf,WiFiManager * wifi) : Conf(Conf), wifi(wifi){
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Switch Manager                #");
    DEBUG_PRINTLN("###########################################################");
    // Print pin mapping
    DEBUG_PRINTLN("================ Switch Pin Map ==================");
    DEBUG_PRINTF("POWER_ON_SWITCH_PIN = GPIO %d (Boot / Mode button)\n", POWER_ON_SWITCH_PIN);
    DEBUG_PRINTLN("==================================================");
    pinMode(POWER_ON_SWITCH_PIN,INPUT_PULLUP);
    instance = this;  // Set the static instance pointer
}


void SwitchManager::detectTapOrHold() {
    uint8_t tapCount = 0;
    unsigned long pressStart = 0;
    unsigned long lastTapTime = 0;

    while (true) {
        if (digitalRead(POWER_ON_SWITCH_PIN) == LOW) {
            pressStart = millis();

            // Wait until button is released
            while (digitalRead(POWER_ON_SWITCH_PIN) == LOW) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            unsigned long pressDuration = millis() - pressStart;

            // If press lasted longer than threshold → HOLD
            if (pressDuration >= HOLD_THRESHOLD_MS) {
                blink(POWER_OFF_LED_PIN, 100);
                DEBUG_PRINTLN("Long press detected 🕒");
                DEBUG_PRINTLN("###########################################################");
                DEBUG_PRINTLN("#                   Resetting device 🔄                   #");
                DEBUG_PRINTLN("###########################################################");
                Conf->PutBool(RESET_FLAG, true);   // Set the reset flag
                Conf->RestartSysDelayDown(3000);   // Delayed restart
                tapCount = 0;                      // Cancel any tap sequence
            }
            // Otherwise → count as tap
            else {
                tapCount++;
                lastTapTime = millis();
            }

            // Triple-tap detection
            if (tapCount == 3) {
                if ((millis() - lastTapTime) <= TAP_WINDOW_MS) {
                    blink(POWER_OFF_LED_PIN, 100);
                    DEBUG_PRINTLN("Triple tap detected 🖱️🖱️🖱️");
                    wifi->begin();
                    tapCount = 0;
                } else {
                    tapCount = 0;
                }
            }
        }

        // Timeout to reset tap sequence
        if ((millis() - lastTapTime) > TAP_TIMEOUT_MS && tapCount > 0) {
            if (tapCount == 1) {
                if (wifi->dev->currentState != DeviceState::Running)
                    wifi->dev->startLoopTask();

                if (wifi->dev->currentState == DeviceState::Running)
                    wifi->dev->currentState = DeviceState::Idle;

                tapCount = 0;
                DEBUG_PRINTLN("One tap detected 🖱️");
            } else {
                tapCount = 0;
                DEBUG_PRINTLN("Tap timeout ⏱️");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SWITCH_TASK_LOOP_DELAY_MS));
    }
}


// Global C-style FreeRTOS task function
void SwitchManager::SwitchTask(void* pvParameters) {
    for (;;) {
        if (SwitchManager::instance != nullptr) {
            SwitchManager::instance->detectTapOrHold();
        }
        vTaskDelay(pdMS_TO_TICKS(SWITCH_TASK_CALL_DELAY_MS));
    }
}

// Member function: launch RTOS task
void SwitchManager::TapDetect() {
    xTaskCreatePinnedToCore(
        SwitchTask,      // Pass the global function, not a member function
        "SwitchTask",
        SWITCH_TASK_STACK_SIZE,
        nullptr,         // No parameter needed since we use the static instance
        SWITCH_TASK_PRIORITY,nullptr,
        SWITCH_TASK_CORE
    );
}
