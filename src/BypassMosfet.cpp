#include "BypassMosfet.h"

void BypassMosfet::begin() {
    Serial.println("###########################################################");
    Serial.println("#              Starting Bypass MOSFET Manager 🧲          #");
    Serial.println("###########################################################");

    pinMode(INA_RELAY_BYPASS_PIN, OUTPUT);
    digitalWrite(INA_RELAY_BYPASS_PIN, LOW);  // LOW = MOSFET OFF (safe)
    state = false;

    DEBUG_PRINTLN("[BypassMosfet] Initialized and OFF 🛑");
}

void BypassMosfet::enable() {
    digitalWrite(INA_RELAY_BYPASS_PIN, HIGH);  // HIGH = MOSFET ON (bypass active)
    state = true;
    DEBUG_PRINTLN("[BypassMosfet] Bypass enabled ⚡");
}

void BypassMosfet::disable() {
    digitalWrite(INA_RELAY_BYPASS_PIN, LOW);   // LOW = MOSFET OFF
    state = false;
    DEBUG_PRINTLN("[BypassMosfet] Bypass disabled 🔌");
}

bool BypassMosfet::isEnabled() const {
    return state;
}
