#ifndef SWITCH_MANAGER_H
#define SWITCH_MANAGER_H

#include "Arduino.h"
#include "NVSManager.h"
#include "Utils.h"
#include "WiFiManager.h"
#include "RGBLed.h"
// ===============================
// Constants for Tap/Hold Detection
// ===============================

//#define TAP_WINDOW_MS         1200              // Max time window for 3 taps
//#define HOLD_THRESHOLD_MS     3000              // Max duration for short hold


class SwitchManager {
public:
    // Constructor
    SwitchManager();

    /**
     * Detect user interaction:
     * - Returns true for 3 quick taps
     * - Returns false for a short press-and-hold
     */
    void detectTapOrHold();
    void TapDetect();
    // RTOS-compatible task that uses detectTapOrHold()
    static void SwitchTask(void* pvParameters); // <-- make it static
    static SwitchManager* instance;

};


#endif // SWITCH_MANAGER_H
