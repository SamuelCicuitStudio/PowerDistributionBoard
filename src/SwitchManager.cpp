#include "SwitchManager.h"
#include "RGBLed.h"   // <-- added for overlays/patterns

SwitchManager* SwitchManager::instance = nullptr;

// Constructor
SwitchManager::SwitchManager(){
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Switch Manager                #");
    DEBUG_PRINTLN("###########################################################");
    // Print pin mapping
    DEBUG_PRINTLN("================ Switch Pin Map ==================");
    DEBUG_PRINTF("POWER_ON_SWITCH_PIN = GPIO %d (Boot / Mode button)\n", POWER_ON_SWITCH_PIN);
    DEBUG_PRINTLN("==================================================");
    DEBUGGSTOP();
    pinMode(POWER_ON_SWITCH_PIN, INPUT_PULLUP);
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
                // Replaced blink(...) with RGB overlay
                RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
                DEBUGGSTART();
                DEBUG_PRINTLN("Long press detected 🕒");
                DEBUG_PRINTLN("###########################################################");
                DEBUG_PRINTLN("#                   Resetting device 🔄                   #");
                DEBUG_PRINTLN("###########################################################");
                DEBUGGSTOP();
                CONF->PutBool(RESET_FLAG, true);   // Set the reset flag
                CONF->RestartSysDelayDown(3000);   // Delayed restart
                tapCount = 0;                      // Cancel any tap sequence
            }
            // Otherwise → count as tap
            else {
                tapCount++;
                lastTapTime = millis();

                // Visual ping for a short tap
                RGB->postOverlay(OverlayEvent::WAKE_FLASH);
            }

            // Triple-tap detection
            if (tapCount == 3) {
                if ((millis() - lastTapTime) <= TAP_WINDOW_MS) {
                    // Replaced blink(...) with a Wi-Fi overlay hint
                    RGB->postOverlay(OverlayEvent::WIFI_AP_);

                    DEBUG_PRINTLN("Triple tap detected 🖱️🖱️🖱️");
                    WIFI->begin();   // begin() itself will show STA/AP overlays too
                    tapCount = 0;
                } else {
                    tapCount = 0;
                }
            }
        }

        // Timeout to reset tap sequence
        if ((millis() - lastTapTime) > TAP_TIMEOUT_MS && tapCount > 0) {
            if (tapCount == 1) {
                DEVICE->startLoopTask(); // idempotent: ensures the Device task is running

                // OFF (= Shutdown) → Tap#1 requests WAKE
                if (DEVICE->currentState == DeviceState::Shutdown) {
                xEventGroupSetBits(gEvt, EVT_WAKE_REQ);
                RGB->postOverlay(OverlayEvent::WAKE_FLASH);
                }
                // IDLE → Tap#1 requests RUN
                else if (DEVICE->currentState == DeviceState::Idle) {
                xEventGroupSetBits(gEvt, EVT_RUN_REQ);
                RGB->postOverlay(OverlayEvent::PWR_START);
                }
                // RUN → Tap#1 requests STOP
                else if (DEVICE->currentState == DeviceState::Running) {
                xEventGroupSetBits(gEvt, EVT_STOP_REQ);
                RGB->postOverlay(OverlayEvent::RELAY_OFF);
                }

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
        SWITCH_TASK_PRIORITY, nullptr,
        SWITCH_TASK_CORE
    );
}
