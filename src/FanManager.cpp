#include "FanManager.h"

void FanManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Fan Manager 🌀                 #");
    DEBUG_PRINTLN("###########################################################");

    ledcSetup(FAN_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
    stop();  // Default to 0%
    DEBUG_PRINTLN("[FanManager] Initialized with 68% speed 🌀");
}

void FanManager::setSpeedPercent(uint8_t pct) {
    pct = constrain(pct, 0, 100);
    currentDuty = static_cast<uint8_t>((pct / 100.0f) * 255.0f);
    ledcWrite(FAN_PWM_CHANNEL, currentDuty);
    DEBUG_PRINTF("[FanManager] Fan speed set to %u%% (duty %u) 🌀\n", pct, currentDuty);
}

void FanManager::stop() {
    currentDuty = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
    DEBUG_PRINTLN("[FanManager] Fan stopped ⛔");
}

uint8_t FanManager::getSpeedPercent() const {
    return static_cast<uint8_t>((currentDuty / 255.0f) * 100.0f + 0.5f); // Rounded %
}
