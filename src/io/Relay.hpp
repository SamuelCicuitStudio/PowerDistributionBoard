/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef RELAY_H
#define RELAY_H

#include <Config.hpp>   // <- adjust if your pin defs live in a different header
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <Utils.hpp>
/*
 * Relay
 *
 * Thread-safe relay controller with mutex protection.
 *
 * Behavior (based on your implementation):
 *  - LOW  = relay ON  (energized, active path)
 *  - HIGH = relay OFF (safe state)
 *
 * Public API:
 *   begin()     -> must be called once before use
 *   turnOn()    -> energize the relay
 *   turnOff()   -> de-energize the relay
 *   isOn()      -> read current cached state (true = ON)
 *
 * Concurrency:
 *   All state changes are wrapped with a mutex so multiple tasks
 *   (web handler, Device manager, safety task, etc.) can safely
 *   request relay changes without racing.
 */
class Relay {
public:
    Relay();

    // Initialize hardware pin, create mutex, force OFF state.
    void begin();

    // Turn relay ON (writes LOW to RELAY_CONTROL_PIN).
    void turnOn();

    // Turn relay OFF (writes HIGH to RELAY_CONTROL_PIN).
    void turnOff();

    // Return current logical state.
    // true  -> relay ON (energized)
    // false -> relay OFF
    bool isOn() const;

private:
    // Try to take mutex (with short timeout).
    // Returns true if acquired, false if not.
    inline bool lock() const {
        if (_mutex == nullptr) return false;
        // small timeout so we don't deadlock caller forever
        return (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE);
    }

    // Release mutex if owned.
    inline void unlock() const {
        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
    }

private:
    // Cached logical relay state.
    // true  => ON (pin driven LOW)
    // false => OFF (pin driven HIGH)
    bool state;

    // Mutex to protect state changes and GPIO writes.
    mutable SemaphoreHandle_t _mutex;
};

#endif // RELAY_H


