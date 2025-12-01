#include "SwitchManager.h"
#include "RGBLed.h"

SwitchManager* SwitchManager::instance = nullptr;

// Constructor
SwitchManager::SwitchManager() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Switch Manager                #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();
    pinMode(POWER_ON_SWITCH_PIN, INPUT_PULLUP);
    pinMode(SW_USER_BOOT_PIN, INPUT_PULLUP);
    instance = this;
}

void SwitchManager::detectTapOrHold() {
    uint8_t tapCount = 0;
    unsigned long pressStart = 0;
    unsigned long lastTapTime = 0;

    while (true) {
        // BOOT pin hold -> full reset
        if (digitalRead(SW_USER_BOOT_PIN) == LOW) {
            pressStart = millis();
            while (digitalRead(SW_USER_BOOT_PIN) == LOW) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            unsigned long pressDuration = millis() - pressStart;
            if (pressDuration >= HOLD_THRESHOLD_MS) {
                RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
                DEBUG_PRINTLN("[Switch] BOOT hold detected -> reset");
                DEVTRAN->requestResetFlagAndRestart();
                tapCount = 0;
                continue;
            }
        }

        if (digitalRead(POWER_ON_SWITCH_PIN) == LOW) {
            pressStart = millis();

            // Wait until button is released
            while (digitalRead(POWER_ON_SWITCH_PIN) == LOW) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            unsigned long pressDuration = millis() - pressStart;

            // HOLD
            if (pressDuration >= HOLD_THRESHOLD_MS) {
                RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
                DEBUGGSTART();
                DEBUG_PRINTLN("[Switch] Long press detected");
                DEBUG_PRINTLN("###########################################################");
                DEBUG_PRINTLN("#                   Resetting device                      #");
                DEBUG_PRINTLN("###########################################################");
                DEBUGGSTOP();
                DEVTRAN->requestResetFlagAndRestart();
                tapCount = 0;
            } else {
                // TAP
                tapCount++;
                lastTapTime = millis();
                RGB->postOverlay(OverlayEvent::WAKE_FLASH);
            }

            // Triple tap -> Wi-Fi AP begin
            if (tapCount == 3) {
                if ((millis() - lastTapTime) <= TAP_WINDOW_MS) {
                    RGB->postOverlay(OverlayEvent::WIFI_AP_);
                    DEBUG_PRINTLN("[Switch] Triple tap detected");
                    WIFI->restartWiFiAP();
                    tapCount = 0;
                } else {
                    tapCount = 0;
                }
            }
        }

        // Timeout to reset tap sequence
        if ((millis() - lastTapTime) > TAP_TIMEOUT_MS && tapCount > 0) {
            if (tapCount == 1) {
                DEVTRAN->ensureLoopTask(); // ensure Device task is running

                DeviceState st = DEVTRAN->getStateSnapshot().state;
                if (st == DeviceState::Shutdown) {
                    DEVTRAN->requestWake();
                    RGB->postOverlay(OverlayEvent::WAKE_FLASH);
                } else if (st == DeviceState::Idle) {
                    DEVTRAN->requestRun();
                    RGB->postOverlay(OverlayEvent::PWR_START);
                } else if (st == DeviceState::Running) {
                    DEVTRAN->requestStop();
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                }

                tapCount = 0;
                DEBUG_PRINTLN("[Switch] One tap detected");
            } else {
                tapCount = 0;
                DEBUG_PRINTLN("[Switch] Tap timeout");
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
        SwitchTask,
        "SwitchTask",
        SWITCH_TASK_STACK_SIZE,
        nullptr,
        SWITCH_TASK_PRIORITY, nullptr,
        SWITCH_TASK_CORE
    );
}
