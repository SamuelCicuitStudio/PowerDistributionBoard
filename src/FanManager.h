#ifndef FAN_MANAGER_H
#define FAN_MANAGER_H

#include "Utils.h"
#include <Arduino.h>

// --------- Defaults (only used if not provided by your Config.h) ----------
#define FAN_PWM_FREQ       10000 // 10 kHz is quiet for most fans
#define FAN_PWM_RESOLUTION 8     // 8-bit (0..255)
// Backward-compatible aliases in case older code used these names:
#define PWM_FREQ FAN_PWM_FREQ
#define PWM_RESOLUTION FAN_PWM_RESOLUTION

// FanManager
// ----------
// Thread-safe, ordered fan control with a global singleton accessor.
// Usage:
//   FanManager::Init();        // (once)
//   FAN->begin();              // start PWM + worker task
//   FAN->setSpeedPercent(60);  // anywhere in code

class FanManager {
public:
    // ===== Singleton API =====
    static void Init();                 // ensure the singleton exists
    static FanManager* Get();           // always returns a valid pointer

    // ===== Lifecycle =====
    void begin();                       // idempotent

    // ===== Public API (non-blocking; enqueues commands) =====
    void setSpeedPercent(uint8_t pct);  // 0..100%
    void stop();
    uint8_t getSpeedPercent() const;    // last applied (0..100)

private:
    // ----- Singleton internals -----
    FanManager();
    ~FanManager() = default;
    FanManager(const FanManager&) = delete;
    FanManager& operator=(const FanManager&) = delete;
    static FanManager* s_instance;

    // ===================== Command model =====================
    enum CmdType : uint8_t { CMD_SET_SPEED, CMD_STOP };
    struct Cmd { CmdType type; uint8_t pct; };

    // ===================== Internal state ====================
    // Last duty actually applied to hardware (0..255)
    volatile uint8_t currentDuty = 0;

    // One-time init guard for begin()
    bool started_ = false;

    // Mutex protects currentDuty and ledcWrite() critical section.
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

// Convenience macro (pointer style)
#define FAN FanManager::Get()

#endif // FAN_MANAGER_H
