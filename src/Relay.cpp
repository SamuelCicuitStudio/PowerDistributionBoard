#include "Relay.h"

void Relay::begin() {
    Serial.println("###########################################################");
    Serial.println("#                  Starting Relay Manager 🔌              #");
    Serial.println("###########################################################");

    pinMode(RELAY_CONTROL_PIN, OUTPUT);
    digitalWrite(RELAY_CONTROL_PIN, LOW);  // Start OFF
    state = false;
    DEBUG_PRINTLN("[Relay] Initialized and OFF 🚫");
}

void Relay::turnOn() {
    digitalWrite(RELAY_CONTROL_PIN, HIGH);
    state = true;
    DEBUG_PRINTLN("[Relay] Turned ON ⚡");
}

void Relay::turnOff() {
    digitalWrite(RELAY_CONTROL_PIN, LOW);
    state = false;
    DEBUG_PRINTLN("[Relay] Turned OFF ⛔");
}

bool Relay::isOn() const {
    return state;
}
