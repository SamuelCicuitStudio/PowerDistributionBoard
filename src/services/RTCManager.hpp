/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef RTCMANAGER_H
#define RTCMANAGER_H


#include <NVSManager.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <Utils.hpp>
#include <Config.hpp> 
/**
 * @file RTCManager.h
 * @brief Singleton system time manager (Unix epoch + formatted date/time).
 *
 * Usage:
 *  - Call RTCManager::Init(&timeInfo) once at boot (idempotent).
 *  - Use RTCManager::Get() to access the global instance.
 *  - Use RTCManager::TryGet() when RTC might not be initialized yet.
 *
 * API:
 *  - setUnixTime(), getUnixTime()
 *  - getTime(), getDate()
 *  - update()
 *  - setRTCTime(...)
 *
 * Notes:
 *  - Thread-safe via FreeRTOS mutex.
 *  - Persists current epoch in NVS (keys from Config).
 *  - Caches "HH:MM" and "YYYY-MM-DD" strings.
 */
class RTCManager {
public:
    // Singleton interface
    static void        Init(struct tm* timeinfo = nullptr);  // idempotent
    static RTCManager* Get();                                // lazy-create
    static RTCManager* TryGet();                             // may be nullptr

    // Constructor kept public for backward compatibility
    explicit RTCManager(struct tm* timeinfo);

    // Existing API (logic unchanged)
    void           setUnixTime(unsigned long timestamp);
    unsigned long  getUnixTime();
    String         getTime();
    String         getDate();
    void           update();
    void           setRTCTime(int year,
                              int month,
                              int day,
                              int hour,
                              int minute,
                              int second);

private:
    // Singleton storage
    static RTCManager* s_instance;

    // RAII mutex guard
    struct MutexGuard {
        explicit MutexGuard(SemaphoreHandle_t mtx,
                            TickType_t to = portMAX_DELAY)
            : _mtx(mtx), _ok(false)
        {
            if (_mtx) {
                _ok = (xSemaphoreTake(_mtx, to) == pdTRUE);
            }
        }

        ~MutexGuard() {
            if (_ok && _mtx) {
                xSemaphoreGive(_mtx);
            }
        }

        bool ok() const { return _ok; }

    private:
        SemaphoreHandle_t _mtx;
        bool              _ok;
    };

    // Shared state
    struct tm*      timeinfo;       ///< Working tm buffer
    String          formattedTime;  ///< "HH:MM"
    String          formattedDate;  ///< "YYYY-MM-DD"
    SemaphoreHandle_t _mutex;       ///< Protects all members + system time ops
};

// Convenience macro (same idea as LOG)
#define RTC RTCManager::Get()

#endif // RTCMANAGER_H


