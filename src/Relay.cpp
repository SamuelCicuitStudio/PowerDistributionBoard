#include "Relay.h"

void Relay::begin() {
    Serial.println("###########################################################");
    Serial.println("#                  Starting Relay Manager 🔌              #");
    Serial.println("###########################################################");

    pinMode(RELAY_CONTROL_PIN, OUTPUT);
    turnOff();
}

void Relay::turnOn() {
    digitalWrite(RELAY_CONTROL_PIN, LOW);
    state = true;
    DEBUG_PRINTLN("[Relay] Turned ON ⚡");
}

void Relay::turnOff() {
    digitalWrite(RELAY_CONTROL_PIN, HIGH);
    state = false;
   DEBUG_PRINTLN("[Relay] Turned OFF ⛔");
}

bool Relay::isOn() const {
    return !state;
}
