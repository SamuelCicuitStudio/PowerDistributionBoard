/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#include "SleepTimer.h"
#include "Device.h"
#include "Buzzer.h"
#include <esp_sleep.h>
#include <WiFi.h>

// ---------------------- Singleton core (like NVS) ----------------------
SleepTimer* SleepTimer::s_instance = nullptr;

void SleepTimer::Init() {
    (void)SleepTimer::Get();
}

SleepTimer* SleepTimer::Get() {
    if (!s_instance) {
        s_instance = new SleepTimer();
    }
    return s_instance;
}

// --------------------------- Implementation ----------------------------
SleepTimer::SleepTimer() {
    lastActivityTime = millis();          // Initialize with current time
    isSleepMode      = false;
    _mutex           = xSemaphoreCreateMutex();
}

void SleepTimer::begin() {
    // Intentionally empty (API compatibility)
}

void SleepTimer::reset() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(100));
    if (!g.ok()) return;
    lastActivityTime = millis();
}

bool SleepTimer::timerTaskRunning() const {
    if (!_timerTask) return false;
    eTaskState s = eTaskGetState(_timerTask);
    return (s != eDeleted && s != eInvalid);
}

void SleepTimer::checkInactivity() {
    bool shouldSleep = false;

    {
        MutexGuard g(_mutex, pdMS_TO_TICKS(50));
        if (!g.ok()) return;

        const unsigned long now = millis();
        const bool timeoutReached =
            ((now - lastActivityTime) >= SLEEP_TIMER_MS);  // rollover-safe

        // Decide under lock, execute sleep outside lock
        if (timeoutReached && !isSleepMode && !_sleepInProgress) {
            _sleepInProgress = true;   // mark intent
            shouldSleep      = true;
        }
    }

    if (shouldSleep) {
        goToSleep();  // clears _sleepInProgress internally
    }
}

void SleepTimer::timerLoop() {
    if (timerTaskRunning()) return;

    xTaskCreate(
        [](void* parameter) {
            auto* self = static_cast<SleepTimer*>(parameter);
            for (;;) {
                self->checkInactivity();
                vTaskDelay(pdMS_TO_TICKS(1000)); // check every 1s
            }
        },
        "SleepTimerLoop",
        2048,
        this,
        1,
        &_timerTask
    );
}

void SleepTimer::goToSleep() {
    // Phase 1: mark state under mutex
    {
        MutexGuard g(_mutex, pdMS_TO_TICKS(250));
        if (!g.ok()) return;

        // Another context might have already entered sleep
        if (isSleepMode) {
            _sleepInProgress = false;
            return;
        }

        isSleepMode = true;
        DEBUG_PRINTLN("[SLEEP] Inactivity timeout reached. Preparing to sleep...");
    }

    // ---------------------------------------------------------------------
    // Current Master behavior:
    //  - Do NOT actually enter sleep.
    //  - Only mark / log and release the in-progress flag.
    // ---------------------------------------------------------------------
    {
        MutexGuard g(_mutex, pdMS_TO_TICKS(50));
        if (g.ok()) {
            _sleepInProgress = false;
        }
    }
    // Ensure hardware is safe/off before sleep
    if (DEVICE) {
        DEVICE->prepareForDeepSleep();
    }
    if (RGB) RGB->setOff();
    if (BUZZ) BUZZ->setMuted(true);

    // Fully power down WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Configure wake sources: BOOT pin or POWER_ON switch (both active low)
    const uint64_t wakeMask =
        (1ULL << SW_USER_BOOT_PIN) |
        (1ULL << POWER_ON_SWITCH_PIN);
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

    DEBUG_PRINTLN("[SLEEP] Entering deep sleep (wake on BOOT or POWER_ON_SWITCH)...");
    esp_deep_sleep_start();
}
