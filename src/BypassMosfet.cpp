#include "BypassMosfet.h"

void BypassMosfet::begin() {
    // Create mutex first so we are protected even during init
    _mutex = xSemaphoreCreateMutex();


        DEBUGGSTART();
        DEBUG_PRINTLN("###########################################################");
        DEBUG_PRINTLN("#              Starting Bypass MOSFET Manager 🧲          #");
        DEBUG_PRINTLN("###########################################################");
        DEBUGGSTOP();

    pinMode(INA_RELAY_BYPASS_PIN, OUTPUT);

    // Safe startup state: MOSFET OFF (LOW) just like before. :contentReference[oaicite:2]{index=2}
    if (lock()) {
        digitalWrite(INA_RELAY_BYPASS_PIN, LOW);  // LOW = MOSFET OFF (safe)
        state = false;
        unlock();
    }

    DEBUG_PRINTLN("[BypassMosfet] Initialized and OFF 🛑");
}

void BypassMosfet::enable() {
    if (!lock()) return;

    // HIGH = MOSFET ON (bypass active) same as original. :contentReference[oaicite:3]{index=3}
    digitalWrite(INA_RELAY_BYPASS_PIN, HIGH);
    state = true;

    unlock();

    DEBUG_PRINTLN("[BypassMosfet] Bypass enabled ⚡");
}

void BypassMosfet::disable() {
    if (!lock()) return;

    // LOW = MOSFET OFF, same as original. :contentReference[oaicite:4]{index=4}
    digitalWrite(INA_RELAY_BYPASS_PIN, LOW);
    state = false;

    unlock();

    DEBUG_PRINTLN("[BypassMosfet] Bypass disabled 🔌");
}

bool BypassMosfet::isEnabled() const {
    bool current;

    if (!lock()) {
        // fallback: if mutex failed (shouldn't happen), just read directly
        current = state;
    } else {
        current = state;
        unlock();
    }

    return current;
}
