#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "Device.h"
#include "Config.h"
#include "RGBLed.h"
#include "HeaterManager.h"

// ================= Build-time Wi-Fi mode selection =================
// If 1 → start in Station (STA) mode using creds/macros below.
// If 0 → start in Access Point (AP) mode.
#define WIFI_START_IN_STA 1

// 1 = fixed hostname "powerboard"
// 0 = dynamic hostname from config (DEVICE_WIFI_HOTSPOT_NAME_KEY / DEVICE_WIFI_HOTSPOT_NAME)
#define DEVICE_HOSTNAME_MODE 1

#if DEVICE_HOSTNAME_MODE == 0
  #define DEVICE_HOSTNAME CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME).c_str()   // http://PDis_XXXXXX.local
#else
  #define DEVICE_HOSTNAME "powerboard" // -> http://powerboard.local/login
#endif


#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID "pboard"
#endif

#ifndef WIFI_STA_PASS
#define WIFI_STA_PASS "1234567890"
#endif

// Connection timeout before falling back to AP (ms)
#ifndef WIFI_STA_CONNECT_TIMEOUT_MS
#define WIFI_STA_CONNECT_TIMEOUT_MS 12000
#endif

// --- Lightweight status snapshot for HTTP handlers ---
// Updated periodically in a background task so HTTP handlers stay cheap.
struct StatusSnapshot {
    float capVoltage = 0.0f;
    float current    = 0.0f;

    float temps[MAX_TEMP_SENSORS] = {0};                    // DS18B20s (cached)
    float wireTemps[HeaterManager::kWireCount] = {0};       // virtual wire temps
    bool  outputs[HeaterManager::kWireCount]   = {false};   // output states

    bool  relayOn   = false;
    bool  acPresent = false;

    uint32_t updatedMs = 0; // last refresh (millis)
};

// ==================================================================

class WiFiManager {
public:
    // ===== Singleton API =====
    static void Init();
    static WiFiManager* Get();
    static WiFiManager* instance; // kept for compatibility

    // ===== Public API =====
    void begin();                 // init Wi-Fi + routes + timers + snapshot task
    void restartWiFiAP();         // disable and re-begin()
    void StartWifiAP();           // start SoftAP and register routes
    bool StartWifiSTA();          // start STA and register routes (true on success)
    void disableWiFiAP();         // fully stop Wi-Fi/AP

    void resetTimer();            // reset inactivity timer
    void startInactivityTimer();  // spawn inactivity watchdog task
    void heartbeat();             // spawn/refresh heartbeat task

    void onUserConnected();
    void onAdminConnected();
    void onDisconnected();

    bool isUserConnected() const;
    bool isAdminConnected() const;
    bool isAuthenticated(AsyncWebServerRequest* request);

    void handleRoot(AsyncWebServerRequest* request);

    // Web server instance
    AsyncWebServer server;

    // RTOS tasks / handles
    static void inactivityTask(void* param);
    TaskHandle_t inactivityTaskHandle = nullptr;
    TaskHandle_t heartbeatTaskHandle  = nullptr;
    unsigned long lastActivityMillis  = 0;

    // Simple WiFi state flags
    bool keepAlive      = false;
    bool WifiState      = false;
    bool prev_WifiState = false;

private:
    // ===== Construction via Init() only =====
    explicit WiFiManager();

    // ================= Concurrency plumbing =================
    SemaphoreHandle_t _mutex = nullptr;

    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // ===== WiFi-level auth state =====
public:
    enum WiFiStatus : uint8_t {
        NotConnected = 0,
        UserConnected,
        AdminConnected
    };

private:
    WiFiStatus wifiStatus = NotConnected;

    // ===== Snapshot task + storage =====
    TaskHandle_t      snapshotTaskHandle = nullptr;
    SemaphoreHandle_t _snapMtx           = nullptr;
    StatusSnapshot    _snap; // guarded by _snapMtx

    static void snapshotTask(void* param);
    void startSnapshotTask(uint32_t periodMs = 250);
    bool getSnapshot(StatusSnapshot& out);

    // ================= Control command queue =================
    enum CtrlType : uint8_t {
        CTRL_REBOOT,
        CTRL_SYS_RESET,
        CTRL_LED_FEEDBACK_BOOL,
        CTRL_ON_TIME_MS,
        CTRL_OFF_TIME_MS,
        CTRL_RELAY_BOOL,
        CTRL_OUTPUT_BOOL,       // i1=index(1..10), b1=state
        CTRL_DESIRED_V,         // f1
        CTRL_AC_FREQ,           // i1
        CTRL_CHARGE_RES,        // f1
        CTRL_DC_VOLT,           // f1
        CTRL_ACCESS_BOOL,       // i1=index(1..10), b1=flag
        CTRL_MODE_IDLE,
        CTRL_SYSTEM_START,
        CTRL_SYSTEM_SHUTDOWN,
        CTRL_BYPASS_BOOL,
        CTRL_FAN_SPEED,         // i1=0..100
        CTRL_BUZZER_MUTE,       // b1
        CTRL_TARGET_RES,        // f1
        CTRL_WIRE_RES,          // i1=index(1..10), f1=ohms
        CTRL_WIRE_OHM_PER_M,    // f1
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

// Convenience macro (mirrors CONF)
#define WIFI WiFiManager::Get()

#endif // WIFI_MANAGER_H
