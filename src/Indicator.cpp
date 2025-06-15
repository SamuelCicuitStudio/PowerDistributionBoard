#include "Indicator.h"

void Indicator::begin() {
  DEBUG_PRINTLN("###########################################################");
  DEBUG_PRINTLN("#                  Starting Indicator                     #");
  DEBUG_PRINTLN("###########################################################");

  pinMode(SHIFT_SER_PIN, OUTPUT);
  pinMode(SHIFT_SCK_PIN, OUTPUT);
  pinMode(SHIFT_RCK_PIN, OUTPUT);

  pinMode(FL06_LED_PIN, OUTPUT);
  pinMode(FL08_LED_PIN, OUTPUT);

  clearAll();

  DEBUG_PRINTLN("Indicator: LED pins initialized and cleared 🔧");
}

void Indicator::setLED(uint8_t flIndex, bool state) {
  switch (flIndex) {
    case 1:  setShiftLED(0, state); break;  // Q0 → FL1
    case 2:  setShiftLED(2, state); break;  // Q2 → FL2
    case 3:  setShiftLED(4, state); break;  // Q4 → FL3
    case 4:  setShiftLED(6, state); break;  // Q6 → FL4
    case 5:  setShiftLED(1, state); break;  // Q1 → FL5
    case 6:  digitalWrite(FL06_LED_PIN, state); break; // GPIO → FL6
    case 7:  setShiftLED(3, state); break;  // Q3 → FL7
    case 8:  digitalWrite(FL08_LED_PIN, state); break; // GPIO → FL8
    case 9:  setShiftLED(7, state); break;  // Q7 → FL9
    case 10: setShiftLED(5, state); break;  // Q5 → FL10
    default: 
      DEBUG_PRINTF("Indicator: Invalid FL index %d ❌\n", flIndex);
      return;
  }
  DEBUG_PRINTF("Indicator: FL%02d set to %s 💡\n", flIndex, state ? "ON" : "OFF");
}

void Indicator::setShiftLED(uint8_t qIndex, bool state) {
  if (qIndex > 7) return;

  if (state)
    shiftState |= (1 << qIndex);
  else
    shiftState &= ~(1 << qIndex);

  updateShiftRegister();
  DEBUG_PRINTF("Indicator: Q%d updated to %s ⚙️\n", qIndex, state ? "ON" : "OFF");
}

void Indicator::updateShiftRegister() {
  digitalWrite(SHIFT_RCK_PIN, LOW);
  shiftOut(SHIFT_SER_PIN, SHIFT_SCK_PIN, MSBFIRST, shiftState);
  digitalWrite(SHIFT_RCK_PIN, HIGH);
  DEBUG_PRINTLN("Indicator: Shift register latched ✅");
}

void Indicator::clearAll() {
  shiftState = 0;
  updateShiftRegister();
  digitalWrite(FL06_LED_PIN, LOW);
  digitalWrite(FL08_LED_PIN, LOW);
  DEBUG_PRINTLN("Indicator: All LEDs turned OFF 📴");
}
