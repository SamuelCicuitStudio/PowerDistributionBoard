#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "Device.h"
#include "Config.h"
#include "RGBLed.h"
#include "HeaterManager.h"

// ================= Build-time Wi-Fi mode selection =================
// If 1 → start in Station (STA) mode using saved creds or the macros below.
// If 0 → start in Access Point (AP) mode.
#define WIFI_START_IN_STA 1
#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID "FASTWEB-ES28CD"
#endif
#ifndef WIFI_STA_PASS
#define WIFI_STA_PASS "9PFSLSA349"
#endif

// Connection timeout before falling back to AP (ms)
#ifndef WIFI_STA_CONNECT_TIMEOUT_MS
#define WIFI_STA_CONNECT_TIMEOUT_MS 12000
#endif

// ==================================================================

class WiFiManager {
public:
    // ===== Singleton API (like NVS/CONF style) =====
    // Create the singleton once (pass your Device*).
    static void Init();
    // Get the singleton pointer (nullptr until Init()).
    static WiFiManager* Get();

    // Kept for backward-compatibility with any existing code.
    static WiFiManager* instance;

    // ===== Public API (unchanged) =====
    void begin();                // init Wi-Fi + routes + timers
    void restartWiFiAP();        // turn off then begin() again
    void StartWifiAP();          // start SoftAP and register routes
    bool StartWifiSTA();         // start Station mode and register routes (true on success)
    void disableWiFiAP();        // stop Wi-Fi

    void resetTimer();           // reset inactivity timer
    void startInactivityTimer(); // start watchdog task
    void heartbeat();            // start/refresh heartbeat task

    void onUserConnected();
    void onAdminConnected();
    void onDisconnected();

    bool isUserConnected() const;
    bool isAdminConnected() const;
    bool isAuthenticated(AsyncWebServerRequest* request);

    void handleRoot(AsyncWebServerRequest* request);

    // Web server
    AsyncWebServer server;

    // RTOS tasks/handles
    static void inactivityTask(void* param);
    TaskHandle_t inactivityTaskHandle = nullptr;
    TaskHandle_t heartbeatTaskHandle  = nullptr;
    unsigned long lastActivityMillis  = 0;

    // State
    bool keepAlive      = false;
    bool WifiState      = false;
    bool prev_WifiState = false;

private:
    // ----- Enforce singleton construction via Init() -----
    explicit WiFiManager();

    // ================= Concurrency plumbing =================
    SemaphoreHandle_t _mutex = nullptr;

    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // ================= Control command queue =================
    enum CtrlType : uint8_t {
        CTRL_REBOOT,
        CTRL_SYS_RESET,
        CTRL_LED_FEEDBACK_BOOL,
        CTRL_ON_TIME_MS,
        CTRL_OFF_TIME_MS,
        CTRL_RELAY_BOOL,
        CTRL_OUTPUT_BOOL,        // i1=index(1..10), b1=state
        CTRL_DESIRED_V,          // f1
        CTRL_AC_FREQ,            // i1
        CTRL_CHARGE_RES,         // f1
        CTRL_DC_VOLT,            // f1
        CTRL_ACCESS_BOOL,        // i1=index(1..10), b1=flag
        CTRL_MODE_IDLE,
        CTRL_SYSTEM_START,
        CTRL_SYSTEM_SHUTDOWN,
        CTRL_BYPASS_BOOL,
        CTRL_FAN_SPEED ,          // i1=0..100
        CTRL_BUZZER_MUTE,      // b1 = true/false
        CTRL_TARGET_RES,
        CTRL_WIRE_RES,
        CTRL_WIRE_OHM_PER_M,
    };

    struct ControlCmd {
        CtrlType type;
        int32_t  i1 = 0;
        int32_t  i2 = 0;
        float    f1 = 0.0f;
        bool     b1 = false;
    };

    QueueHandle_t  _ctrlQueue = nullptr;
    TaskHandle_t   _ctrlTask  = nullptr;

    static void controlTaskTrampoline(void* pv);
    void controlTaskLoop();
    void handleControl(const ControlCmd& c);

    void sendCmd(const ControlCmd& c); // non-blocking enqueue

    // Routes registration shared by AP/STA
    void registerRoutes_();
};

// Convenience pointer macro (mirrors CONF)
#define WIFI WiFiManager::Get()

#endif // WIFI_MANAGER_H
