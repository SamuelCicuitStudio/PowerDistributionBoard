#include "comms/SwitchManager.h"
#include "control/RGBLed.h"
#include "control/HeaterManager.h"
#include "services/NVSManager.h"
#include "sensing/NtcSensor.h"

SwitchManager* SwitchManager::instance = nullptr;

// Constructor
SwitchManager::SwitchManager() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Switch Manager                #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();
    // POWER_ON_SWITCH_PIN is shared with the NTC divider; no pullups here.
    pinMode(SW_USER_BOOT_PIN, INPUT_PULLUP);
    instance = this;
}

static inline void updatePowerSample() {
    if (NTC) {
        NTC->update();
    }
}

static inline bool powerPressed() {
    if (NTC) {
        return NTC->isPressed();
    }
    return digitalRead(POWER_ON_SWITCH_PIN) == LOW;
}

static void forceStopAndRestartNow() {
    // Best-effort immediate safety before restart.
    // (Do not set RESET_FLAG here; this is a "force stop + restart", not a factory reset.)
    if (WIRE) {
        WIRE->disableAll();
    }

    // Ensure relay is driven to OFF. Relay::turnOff() writes LOW.
    pinMode(RELAY_CONTROL_PIN, OUTPUT);
    digitalWrite(RELAY_CONTROL_PIN, LOW);

    // Ask device state machine to stop if it is responsive.
    if (DEVTRAN) {
        DEVTRAN->requestStop();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    CONF->simulatePowerDown();
}

void SwitchManager::detectTapOrHold() {
    uint8_t tapCount = 0;
    unsigned long pressStart = 0;
    unsigned long lastTapTime = 0;

    while (true) {
        updatePowerSample();
        // BOOT pin long-hold -> FACTORY RESET (persist RESET_FLAG then restart)
        if (digitalRead(SW_USER_BOOT_PIN) == LOW) {
            pressStart = millis();
            while (digitalRead(SW_USER_BOOT_PIN) == LOW) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            unsigned long pressDuration = millis() - pressStart;
            if (pressDuration >= HOLD_THRESHOLD_MS) {
                RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
                DEBUG_PRINTLN("[Switch] BOOT hold detected -> factory reset");
                CONF->PutBool(RESET_FLAG, true);
                vTaskDelay(pdMS_TO_TICKS(50));
                ESP.restart();
                tapCount = 0;
                continue;
            }
        }

        if (powerPressed()) {
            pressStart = millis();

            // Wait until button is released
            while (powerPressed()) {
                updatePowerSample();
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            unsigned long pressDuration = millis() - pressStart;

            // HOLD (POWER button) -> force stop + restart (no factory reset)
            if (pressDuration >= HOLD_THRESHOLD_MS) {
                RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
                DEBUGGSTART();
                DEBUG_PRINTLN("[Switch] POWER hold detected -> force stop + restart");
                DEBUG_PRINTLN("###########################################################");
                DEBUG_PRINTLN("#                Forcing stop and restart                 #");
                DEBUG_PRINTLN("###########################################################");
                DEBUGGSTOP();
                forceStopAndRestartNow();
                tapCount = 0;
            } else {
                // TAP (POWER button) -> RUN / OFF (toggle)
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
                if (st == DeviceState::Running || st == DeviceState::Error) {
                    DEVTRAN->requestStop();
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                } else {
                    DEVTRAN->requestRun();
                    RGB->postOverlay(OverlayEvent::PWR_START);
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
    xTaskCreate(
        SwitchTask,
        "SwitchTask",
        SWITCH_TASK_STACK_SIZE,
        nullptr,
        SWITCH_TASK_PRIORITY, nullptr
    );
}
