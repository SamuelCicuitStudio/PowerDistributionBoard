#ifndef INDICATOR_H
#define INDICATOR_H

#include "Utils.h"
#include "NVSManager.h"
#include "Config.h"

// ==================================================
// Floor Heater LED Mapping and Control Overview
// ==================================================
//
// This class manages 10 floor heater indicator LEDs:
//
// ▶ 8 LEDs via 74HC595 shift register:
//    - Q0 → FL1
//    - Q1 → FL5
//    - Q2 → FL2
//    - Q3 → FL7
//    - Q4 → FL3
//    - Q5 → FL10
//    - Q6 → FL4
//    - Q7 → FL9
//
// ▶ 2 LEDs via GPIO:
//    - FL06 → FL06_LED_PIN
//    - FL08 → FL08_LED_PIN
//
// Public API (setLED, clearAll, startupChaser) does not touch HW directly.
// We push commands into a FreeRTOS queue; a single worker task drains the
// queue, grabs the mutex, and applies changes to hardware.

class Indicator {
public:
    Indicator();

    // Must be called once at boot:
    // - Initializes GPIO pins.
    // - Creates mutex + queue.
    // - Starts the worker task.
    // - Queues the animated startup chaser, then applies config feedback.
    void begin();

    // Request: set floor LED [1..10] ON/OFF (enqueued, non-blocking).
    void setLED(uint8_t flIndex, bool state);

    // Request: turn everything OFF (enqueued, non-blocking).
    void clearAll();

    // Request: run startup animation (enqueued, non-blocking).
    void startupChaser();

    // --- Backward-compatibility helpers (now also enqueued) ---
    void updateShiftRegister();
    void setShiftLED(uint8_t qIndex, bool state);
    void shiftOutFast(uint8_t data);

    // Public fields (updated by worker task while holding the mutex).
    uint8_t shiftState;
    bool    feedback;

private:
    // ---------------- Internal command model ----------------
    enum CmdType : uint8_t {
        CMD_SET_LED,          // index = FL#, state = on/off
        CMD_CLEAR_ALL,        // no args
        CMD_STARTUP_CHASER,   // no args
        CMD_SET_SHIFT_LED,    // index = Q# (0..7), state = on/off
        CMD_UPDATE_SHIFTREG,  // refresh 74HC595 latch from shiftState
        CMD_SHIFT_RAW         // rawData = byte pushed to shift register
    };

    struct Cmd {
        CmdType type;
        uint8_t index;     // FL index or Q index depending on type
        bool    state;     // desired state for SET_LED / SET_SHIFT_LED
        uint8_t rawData;   // for CMD_SHIFT_RAW
    };

    // RTOS plumbing
    TaskHandle_t      _taskHandle;
    QueueHandle_t     _queue;
    SemaphoreHandle_t _mutex;

    // Lock helpers (used by the worker when touching HW/shared state)
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // Push a command into the queue (non-blocking).
    // NOTE: No mutex here — FreeRTOS queues are thread-safe.
    // If full, we drop the oldest so the newest state wins.
    void sendCmd(const Cmd &cmd);

    // Task wrapper + loop
    static void taskTrampoline(void* pv);
    void taskLoop();
    void handleCmd(const Cmd &cmd);

    // ---------------- Low-level HW ops ----------------
    // These run ONLY in the worker task while _mutex is held.
    void hwSetLED(uint8_t flIndex, bool state);
    void hwSetShiftLED(uint8_t qIndex, bool state);
    void hwUpdateShiftRegister();
    void hwShiftOutFast(uint8_t data);
    void hwClearAll();
    void hwStartupChaser();
};

#endif // INDICATOR_H
