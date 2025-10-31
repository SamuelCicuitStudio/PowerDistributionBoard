#ifndef FAN_MANAGER_H
#define FAN_MANAGER_H

#include "Utils.h"

// FanManager
// ----------
// Thread-safe, ordered fan control with a global singleton accessor.
// Usage:
//   FanManager::Init();        // (once at boot, optional but clearer)
//   FAN->begin();              // start PWM + worker task
//   FAN->setSpeedPercent(60);  // anywhere in code
//
// NOTE: The queue/RTOS design from your current class is preserved.

class FanManager {
public:
    // ===== Singleton API (mirrors NVS::Init / NVS::Get) =====
    static void Init();                 // ensure the singleton exists
    static FanManager* Get();           // always returns a valid pointer

    // ===== Lifecycle =====
    void begin();                       // idempotent; safe to call more than once

    // ===== Public API (non-blocking; enqueues commands) =====
    void setSpeedPercent(uint8_t pct);  // 0..100%
    void stop();
    uint8_t getSpeedPercent() const;    // returns last applied speed (0..100)

private:
    // ----- Singleton internals -----
    FanManager();                       // ctor is private (singleton)
    ~FanManager() = default;
    FanManager(const FanManager&) = delete;
    FanManager& operator=(const FanManager&) = delete;
    static FanManager* s_instance;

    // ===================== Command model =====================
    enum CmdType : uint8_t { CMD_SET_SPEED, CMD_STOP };
    struct Cmd { CmdType type; uint8_t pct; };

    // ===================== Internal state ====================
    // Last duty actually applied to hardware (0..255)
    uint8_t currentDuty = 0;

    // One-time init guard for begin()
    bool started_ = false;

    // Mutex protects currentDuty and actual ledcWrite() section.
    SemaphoreHandle_t _mutex = nullptr;

    // Queue + worker task to serialize actual fan updates.
    QueueHandle_t _queue = nullptr;
    TaskHandle_t  _taskHandle = nullptr;

    // ===================== Helpers =====================
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }
    inline void unlock() const { if (_mutex) xSemaphoreGive(_mutex); }

    // Push command to queue (non-blocking). Newest wins if full.
    void sendCmd(const Cmd &cmd);

    // RTOS task plumbing
    static void taskTrampoline(void* pv);
    void taskLoop();
    void handleCmd(const Cmd &cmd);

    // Low-level / critical section: apply a duty to hardware.
    void hwApplySpeedPercent(uint8_t pct);
    void hwApplyStop();
};

// Convenience macro (pointer style), like CONF for NVS.
#define FAN FanManager::Get()

#endif // FAN_MANAGER_H
