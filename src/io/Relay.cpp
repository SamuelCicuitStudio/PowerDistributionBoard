#include "io/Relay.h"

Relay::Relay()
: state(false),
  _mutex(nullptr)
{
}

void Relay::begin() {
    // Create mutex first so everything after this is protected
    _mutex = xSemaphoreCreateMutex();
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Relay Manager ðŸ”Œ              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    pinMode(RELAY_CONTROL_PIN, OUTPUT);

    // Force known safe state: OFF
    turnOff();

    DEBUG_PRINTLN("[Relay] Initialized OFF â›”");
}

void Relay::turnOn() {
    if (!lock()) return;

    // Original behavior: LOW = OFF (energize relay). :contentReference[oaicite:2]{index=2}
    digitalWrite(RELAY_CONTROL_PIN, HIGH);
    state = true;

    unlock();

    DEBUG_PRINTLN("[Relay] Turned ON");
}

void Relay::turnOff() {
    if (!lock()) return;

    // Original behavior: HIGH = OFF :contentReference[oaicite:3]{index=3}
    digitalWrite(RELAY_CONTROL_PIN, LOW);
    state = false;

    unlock();

    DEBUG_PRINTLN("[Relay] Turned OFF â›”");
}

bool Relay::isOn() const {
    bool current;

    if (!lock()) {
        current = state; // fallback if mutex couldn't be taken
    } else {
        current = state;
        unlock();
    }

    return current;
}
