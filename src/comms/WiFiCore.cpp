#include <WiFiManager.hpp>
#include <DeviceTransport.hpp>
#include <Utils.hpp>
#include <RTCManager.hpp>
#include <ESPmDNS.h>
#include <time.h>

namespace {
static bool syncTimeFromNtp(uint32_t timeoutMs = 2500) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    const uint32_t start = millis();
    tm info{};
    while ((millis() - start) < timeoutMs) {
        if (getLocalTime(&info, 500)) {
            const time_t now = mktime(&info);
            if (RTC) {
                RTC->setUnixTime(static_cast<unsigned long>(now));
            }
            DEBUG_PRINTF("[WiFi] NTP sync ok (epoch=%lu)\n",
                         static_cast<unsigned long>(now));
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    DEBUG_PRINTLN("[WiFi] NTP sync failed");
    return false;
}
} // namespace

WiFiManager* WiFiManager::instance = nullptr;

void WiFiManager::Init() {
    if (!instance) {
        instance = new WiFiManager();
    }
}

WiFiManager* WiFiManager::Get() {
    return instance;
}

WiFiManager::WiFiManager()
    : server(80) {}

void WiFiManager::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager                #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    if (!instance) instance = this;

    // Create mutex for WiFiManager shared state
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }

    // Control queue + worker task (serializes /control side-effects)
    if (_ctrlQueue == nullptr) {
        _ctrlQueue = xQueueCreate(24, sizeof(ControlCmd));
    }
    if (_ctrlTask == nullptr) {
        xTaskCreate(
            controlTaskTrampoline,
            "WiFiCtrlTask",
            4096,
            this,
            1,
            &_ctrlTask
        );
    }

    // Initialize WiFi state
    if (lock()) {
        wifiStatus     = WiFiStatus::NotConnected;
        keepAlive      = false;
        WifiState      = false;
        prev_WifiState = false;
        unlock();
    }

#if WIFI_START_IN_STA
    if (!StartWifiSTA()) {
        DEBUG_PRINTLN("[WiFi] STA connect failed falling back to AP");
        StartWifiAP();
    }
#else
    StartWifiAP();
#endif

    // Start snapshot updater (after routes/server started in AP/STA functions)
    startSnapshotTask(250); // ~4Hz; safe & cheap
    startStateStreamTask(); // SSE push for device state
    startEventStreamTask(); // SSE push for warnings/errors
    startLiveStreamTask();  // batched live stream for UI playback

    BUZZ->bipWiFiConnected();
}

void WiFiManager::StartWifiAP() {
    if (lock()) {
        keepAlive      = false;
        WifiState      = true;
        prev_WifiState = false;
        unlock();
    }

    DEBUG_PRINTLN("[WiFi] Starting Access Point");

    // Clean reset WiFi state
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));

    const String ap_ssid = CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY,
                                           DEVICE_WIFI_HOTSPOT_NAME);
    const String ap_pass = CONF->GetString(DEVICE_AP_AUTH_PASS_KEY,
                                           DEVICE_AP_AUTH_PASS_DEFAULT);

    // AP mode
    WiFi.mode(WIFI_AP);

    // Configure AP IP (do this BEFORE/for softAP start)
    if (!WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("[WiFi] Failed to set AP config");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }

    // Start AP (limit to a single client)
    if (!WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 1, 0, 1)) {
        DEBUG_PRINTLN("[WiFi] Failed to start AP");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }

#if defined(DEVICE_HOSTNAME)
    // Set hostname for the AP interface (correct API for SoftAP)
    WiFi.softAPsetHostname(DEVICE_HOSTNAME);
#endif

    const IPAddress apIp = WiFi.softAPIP();
    DEBUG_PRINTF("[WiFi] AP Started: %s\n", ap_ssid.c_str());
    DEBUG_PRINT("[WiFi] AP IP Address: ");
    DEBUG_PRINTLN(apIp.toString());

    // (Re)start mDNS for this interface (non-fatal if it fails)
    MDNS.end();
#if defined(DEVICE_HOSTNAME)
    if (MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        DEBUG_PRINTF("[mDNS] AP responder at http://%s.local/login\n", DEVICE_HOSTNAME);
    } else {
        DEBUG_PRINTLN("[mDNS] [WARN] Failed to start mDNS in AP mode (non-fatal)");
    }
#endif

    // Web server + routes
    registerRoutes_();
    server.begin();
    startInactivityTimer();

    RGB->postOverlay(OverlayEvent::WIFI_AP_);
}

bool WiFiManager::StartWifiSTA() {
    if (lock()) {
        keepAlive      = false;
        WifiState      = true;
        prev_WifiState = false;
        unlock();
    }

    DEBUG_PRINTLN("[WiFi] Starting Station (STA) mode");
    #if OVERIDE_STA
        String ssid = WIFI_STA_SSID;
        String pass = WIFI_STA_PASS;
    #else
        String ssid = CONF->GetString(STA_SSID_KEY,"Nothing");
        String pass = CONF->GetString(STA_PASS_KEY,"Nothing");
    #endif

    // Clean reset WiFi state (important when switching from AP)
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Go STA
    WiFi.mode(WIFI_STA);

#if defined(DEVICE_HOSTNAME)
    // Set hostname for STA *before* begin()
    WiFi.setHostname(DEVICE_HOSTNAME);
#endif

    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait for connection or timeout
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - t0) < WIFI_STA_CONNECT_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINTLN("[WiFi] STA connect timeout");
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return false;
    }

    IPAddress ip = WiFi.localIP();
    DEBUG_PRINTF("[WiFi] STA Connected. SSID=%s, IP=%s\n",
                 ssid.c_str(),
                 ip.toString().c_str());

    syncTimeFromNtp();

    // ---- mDNS: expose http://powerboard.local on this LAN ----
    MDNS.end(); // ensure clean
#if defined(DEVICE_HOSTNAME)
    if (MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        DEBUG_PRINTF("[mDNS] STA responder at http://%s.local/login -> %s\n",
                     DEVICE_HOSTNAME,
                     ip.toString().c_str());
    } else {
        DEBUG_PRINTLN("[mDNS] [WARN] Failed to start mDNS in STA mode ");
    }
#endif

    // Start web server and routes
    registerRoutes_();
    server.begin();
    startInactivityTimer();
    startLiveStreamTask();

    RGB->postOverlay(OverlayEvent::WIFI_STATION);
    return true;
}

void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("[WiFi] Disabling WiFi ...");
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (lock()) {
        WifiState           = false;
        prev_WifiState      = true;
        inactivityTaskHandle = nullptr;
        unlock();
    }

    RGB->postOverlay(OverlayEvent::WIFI_LOST);
    DEBUG_PRINTLN("[WiFi] WiFi disabled");
}

void WiFiManager::resetTimer() {
    if (lock()) { lastActivityMillis = millis(); unlock(); }
}

void WiFiManager::inactivityTask(void* param) {
    auto* self = static_cast<WiFiManager*>(param);
    for (;;) {
        bool wifiOn;
        unsigned long last;
        if (self->lock()) {
            wifiOn = self->WifiState;
            last   = self->lastActivityMillis;
            self->unlock();
        } else {
            wifiOn = self->WifiState;
            last   = self->lastActivityMillis;
        }

        if (wifiOn && (millis() - last > INACTIVITY_TIMEOUT_MS)) {
            DEBUG_PRINTLN("[WiFi] Inactivity timeout");
            self->disableWiFiAP();
            vTaskDelete(nullptr);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void WiFiManager::startInactivityTimer() {
    resetTimer();
    if (inactivityTaskHandle == nullptr) {
        xTaskCreate(
            WiFiManager::inactivityTask,
            "WiFiInactivity",
            2048,
            this,
            1,
            &inactivityTaskHandle
        );
        DEBUG_PRINTLN("[WiFi] Inactivity timer started ");
    }
}

void WiFiManager::heartbeat() {
    if (heartbeatTaskHandle != nullptr) return;

    DEBUG_PRINTLN("[WiFi] Heartbeat Create ");
    BUZZ->bip();

    xTaskCreate(
        [](void* param) {
            WiFiManager* self = static_cast<WiFiManager*>(param);
            const uint32_t intervalMs = 6000;
            const TickType_t interval = pdMS_TO_TICKS(intervalMs);
            const uint8_t maxMissed = 3;
            const uint32_t activityGraceMs = intervalMs * 2;
            uint8_t missed = 0;

            for (;;) {
                vTaskDelay(interval);

                bool user  = self->isUserConnected();
                bool admin = self->isAdminConnected();
                bool ka    = false;
                uint32_t last = 0;
                bool setupPending = false;

                if (self->lock()) {
                    ka = self->keepAlive;
                    last = self->lastActivityMillis;
                    self->unlock();
                } else {
                    ka = self->keepAlive;
                    last = self->lastActivityMillis;
                }
                if (CONF) {
                    setupPending = !CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
                }

                if (!user && !admin) {
                    DEBUG_PRINTLN("[WiFi] Heartbeat deleted  (no clients)");
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }

                const uint32_t now = millis();
                const bool recent = (now - last) <= activityGraceMs;

                bool busy = false;
                if (DEVTRAN) {
                    const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
                    if (snap.state == DeviceState::Running) {
                        busy = true;
                    }
                    Device::WireTargetStatus st{};
                    if (DEVTRAN->getWireTargetStatus(st) && st.active) {
                        busy = true;
                    }
                }

                if (!ka && !recent) {
                    if (!busy && !setupPending) {
                        missed++;
                    } else {
                        missed = 0;
                    }
                } else {
                    missed = 0;
                }

                if (missed >= maxMissed) {
                    DEBUG_PRINTLN("[WiFi]  Heartbeat timeout  disconnecting");
                    self->onDisconnected();
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    DEBUG_PRINTLN("[WiFi] Heartbeat deleted");
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }

                if (self->lock()) {
                    self->keepAlive = false;
                    self->unlock();
                } else {
                    self->keepAlive = false;
                }
            }
        },
        "HeartbeatTask",
        2048,
        this,
        1,
        &heartbeatTaskHandle
    );
}

void WiFiManager::restartWiFiAP() {
    disableWiFiAP();
    vTaskDelay(pdMS_TO_TICKS(100));
    begin();
}

