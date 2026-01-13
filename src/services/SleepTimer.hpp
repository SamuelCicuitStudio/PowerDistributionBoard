/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef SLEEPTIMER_H
#define SLEEPTIMER_H

/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#include <NVSManager.hpp>
#include <RGBLed.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifndef SLEEP_TIMER_MS
#define SLEEP_TIMER_MS (10UL * 60UL * 1000UL) // default 10 minutes (unused unless timerLoop is used)
#endif

/** 
 * @brief Inactivity-based sleep supervisor (Singleton, same pattern as NVS).
 *
 * Usage (like NVS):
 *   SleepTimer::Init();
 *   SLEEP->timerLoop();        // macro below
 *   SLEEP->reset();
 *
 * Current behavior:
 * - For Master: goToSleep() only marks/logs; no actual sleep entry.
 */
class SleepTimer {
public:
    // ------------------ Singleton access (like NVS) ------------------
    static void Init();          // Ensure construction once (optional but recommended)
    static SleepTimer* Get();    // Lazy global accessor

    // ------------------------ Public API -----------------------------
    void begin();                // Kept for API compatibility (no-op)
    void reset();
    void checkInactivity();
    void timerLoop();
    void goToSleep();

    // ------------------ Legacy public fields (kept) ------------------
    unsigned long inactivityTimeout = 0;  ///< Unused placeholder (legacy).
    unsigned long lastActivityTime  = 0;  ///< Last activity timestamp (ms).
    bool          isSleepMode       = false;

private:
    // Singleton core
    SleepTimer();                                // now private (singleton)
    SleepTimer(const SleepTimer&) = delete;
    SleepTimer& operator=(const SleepTimer&) = delete;
    static SleepTimer* s_instance;

    // ------------------------- Internals -----------------------------
    struct MutexGuard {
        explicit MutexGuard(SemaphoreHandle_t mtx,
                            TickType_t timeout = portMAX_DELAY)
            : _mtx(mtx), _ok(false)
        {
            if (_mtx) _ok = (xSemaphoreTake(_mtx, timeout) == pdTRUE);
        }
        ~MutexGuard() { if (_ok && _mtx) xSemaphoreGive(_mtx); }
        bool ok() const { return _ok; }
    private:
        SemaphoreHandle_t _mtx;
        bool              _ok;
    };

    SemaphoreHandle_t _mutex = nullptr;   // protects shared state/sleep seq
    TaskHandle_t _timerTask   = nullptr;  // avoid multiple timerLoop tasks
    bool _sleepInProgress     = false;    // avoid concurrent goToSleep()
    bool timerTaskRunning() const;
};

// ------------- Ergonomic global accessor (like CONF â†’ SLEEP) -------------
#ifndef SLEEP
#define SLEEP (SleepTimer::Get())
#endif

#endif // SLEEPTIMER_H


