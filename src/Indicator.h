#ifndef INDICATOR_H
#define INDICATOR_H

#include "Utils.h"

// ==================================================
// Floor Heater LED Mapping and Control Overview
// ==================================================
/*
  This class manages 10 floor heater indicator LEDs:

  ▶ 8 LEDs via 74HC595 shift register:
     - Q0 → FL1
     - Q1 → FL5
     - Q2 → FL2
     - Q3 → FL7
     - Q4 → FL3
     - Q5 → FL10
     - Q6 → FL4
     - Q7 → FL9

  ▶ 2 LEDs via GPIO:
     - FL06 → GPIO 18
     - FL08 → GPIO 11

  Usage:
    setLED(1, true);   // Turn on FL1
    setLED(6, false);  // Turn off FL6
*/

class Indicator {
public:
  void begin();
  void setLED(uint8_t flIndex, bool state);  // flIndex: 1–10 (FL1–FL10)
  void clearAll();
  // Plays a quick 10-LED chaser once (< ~2.2s total), then returns.
  void startupChaser();

  uint8_t shiftState = 0;
  void updateShiftRegister();
  void setShiftLED(uint8_t qIndex, bool state);  // Internal
  void shiftOutFast(uint8_t data);
};

#endif // INDICATOR_H
