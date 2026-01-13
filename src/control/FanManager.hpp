/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef FAN_MANAGER_H
#define FAN_MANAGER_H

#include <Utils.hpp>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ================= Defaults (override in Config.h if you like) =================
#ifndef FAN_PWM_FREQ
#define FAN_PWM_FREQ       10000   // 10 kHz (quiet)
#endif

#ifndef FAN_PWM_RESOLUTION
#define FAN_PWM_RESOLUTION 8       // 8-bit (0..255)
#endif

// Back-compat: if older code defined FAN1/FAN2 pins/channels, honor them first
#if defined(FAN1_PWM_PIN) && !defined(FAN_CAP_PWM_PIN)
  #define FAN_CAP_PWM_PIN FAN1_PWM_PIN
#endif
#if defined(FAN1_PWM_CHANNEL) && !defined(FAN_CAP_PWM_CHANNEL)
  #define FAN_CAP_PWM_CHANNEL FAN1_PWM_CHANNEL
#endif
#if defined(FAN2_PWM_PIN) && !defined(FAN_HS_PWM_PIN)
  #define FAN_HS_PWM_PIN FAN2_PWM_PIN
#endif
#if defined(FAN2_PWM_CHANNEL) && !defined(FAN_HS_PWM_CHANNEL)
  #define FAN_HS_PWM_CHANNEL FAN2_PWM_CHANNEL
#endif

// If nothing provided, choose sensible defaults
#ifndef FAN_CAP_PWM_PIN
#define FAN_CAP_PWM_PIN       14
#endif
#ifndef FAN_CAP_PWM_CHANNEL
#define FAN_CAP_PWM_CHANNEL   2
#endif
#ifndef FAN_HS_PWM_PIN
#define FAN_HS_PWM_PIN        42
#endif
#ifndef FAN_HS_PWM_CHANNEL
#define FAN_HS_PWM_CHANNEL    3     // NOTE: different channel than capacitor fan
#endif

// ============================== Fan Manager ===============================
class FanManager {
public:
    // ===== Singleton API =====
    static void Init();
    static FanManager* Get();

    // ===== Lifecycle =====
    void begin();  // idempotent

    // ===== Back-compat (controls the CAPACITOR/BOARD fan only) =====
    void setSpeedPercent(uint8_t pct);    // legacy â†’ CAP fan
    void stop();                          // legacy â†’ CAP fan
    uint8_t getSpeedPercent() const;      // legacy â†’ CAP fan

    // ===== New dual-fan API =====
    void setCapSpeedPercent(uint8_t pct);     // 0..100 %
    void stopCap();
    uint8_t getCapSpeedPercent() const;

    void setHeatsinkSpeedPercent(uint8_t pct); // 0..100 %
    void stopHeatsink();
    uint8_t getHeatsinkSpeedPercent() const;

private:
    // ----- Singleton internals -----
    FanManager();
    ~FanManager() = default;
    FanManager(const FanManager&) = delete;
    FanManager& operator=(const FanManager&) = delete;
    static FanManager* s_instance;

    // Which fan
    enum class FanSel : uint8_t { Cap = 0, Heatsink = 1 };

    // ===================== Command model =====================
    enum CmdType : uint8_t { CMD_SET_SPEED, CMD_STOP };
    struct Cmd { CmdType type; uint8_t pct; FanSel which; };

    // ===================== Internal state ====================
    // Last duties actually applied to hardware (0..255)
    volatile uint8_t currentDuty_[2] = {0,0}; // [Cap, Heatsink]

    bool started_ = false;

    // Mutex protects currentDuty_ and LEDC writes
    SemaphoreHandle_t _mutex = nullptr;

    // Queue + worker task to serialize updates
    QueueHandle_t _queue = nullptr;
    TaskHandle_t  _taskHandle = nullptr;

    // ===================== Helpers =====================
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }
    inline void unlock() const { if (_mutex) xSemaphoreGive(_mutex); }

    void sendCmd(const Cmd &cmd);

    static void taskTrampoline(void* pv);
    void taskLoop();
    void handleCmd(const Cmd &cmd);

    // Low-level: apply a duty to the selected fan
    void hwApplySpeedPercent(FanSel which, uint8_t pct);
    void hwApplyStop(FanSel which);
};

// Convenience macro (pointer style)
#define FAN FanManager::Get()

#endif // FAN_MANAGER_H


