#include "WiFiManager.h"
#include "Utils.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// ===== Singleton storage & accessors =====

WiFiManager* WiFiManager::instance = nullptr;

void WiFiManager::Init() {
    if (!instance) {
        instance = new WiFiManager();
    }
}

WiFiManager* WiFiManager::Get() {
    return instance;
}

// ===== Constructor: keep lightweight; real setup in begin() =====

WiFiManager::WiFiManager()
    : server(80) {}

// ========================== begin() ==========================

void WiFiManager::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
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
        xTaskCreatePinnedToCore(
            controlTaskTrampoline,
            "WiFiCtrlTask",
            4096,
            this,
            1,
            &_ctrlTask,
            APP_CPU_NUM
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
        DEBUG_PRINTLN("[WiFi] STA connect failed → falling back to AP 📡");
        StartWifiAP();
    }
#else
    StartWifiAP();
#endif

    // Start snapshot updater (after routes/server started in AP/STA functions)
    startSnapshotTask(250); // ~4Hz; safe & cheap

    BUZZ->bipWiFiConnected();
}

// ========================== AP / STA ==========================

void WiFiManager::StartWifiAP() {
    if (lock()) {
        keepAlive      = false;
        WifiState      = true;
        prev_WifiState = false;
        unlock();
    }

    DEBUG_PRINTLN("[WiFi] Starting Access Point ✅");

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
        DEBUG_PRINTLN("[WiFi] Failed to set AP config ❌");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }

    // Start AP
    if (!WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str())) {
        DEBUG_PRINTLN("[WiFi] Failed to start AP ❌");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }

#if defined(DEVICE_HOSTNAME)
    // Set hostname for the AP interface (correct API for SoftAP)
    WiFi.softAPsetHostname(DEVICE_HOSTNAME.c_str());
#endif

    const IPAddress apIp = WiFi.softAPIP();
    DEBUG_PRINTF("✅ AP Started: %s\n", ap_ssid.c_str());
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

    DEBUG_PRINTLN("[WiFi] Starting Station (STA) mode 🚏");

    String ssid = WIFI_STA_SSID;
    String pass = WIFI_STA_PASS;

    // Clean reset WiFi state (important when switching from AP)
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Go STA
    WiFi.mode(WIFI_STA);

#if defined(DEVICE_HOSTNAME)
    // Set hostname for STA *before* begin()
    WiFi.setHostname(DEVICE_HOSTNAME.c_str());
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
        DEBUG_PRINTLN("[WiFi] STA connect timeout ❌");
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return false;
    }

    IPAddress ip = WiFi.localIP();
    DEBUG_PRINTF("✅ STA Connected. SSID=%s, IP=%s\n",
                 ssid.c_str(),
                 ip.toString().c_str());

    // ---- mDNS: expose http://powerboard.local on this LAN ----
    MDNS.end(); // ensure clean
#if defined(DEVICE_HOSTNAME)
    if (MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        DEBUG_PRINTF("[mDNS] STA responder at http://%s.local -> %s\n",
                     DEVICE_HOSTNAME,
                     ip.toString().c_str());
    } else {
        DEBUG_PRINTLN("[mDNS] [WARN] Failed to start mDNS in STA mode ❌");
    }
#endif

    // Start web server and routes
    registerRoutes_();
    server.begin();
    startInactivityTimer();

    RGB->postOverlay(OverlayEvent::WIFI_STATION);
    return true;
}

// ======================= Route registration =======================

void WiFiManager::registerRoutes_() {
    // ---- Login page ----
    server.on("/login", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        handleRoot(request);
    });

    // ---- Heartbeat ----
    server.on("/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) {
            BUZZ->bipFault();
            request->redirect("/login");
            return;
        }
        if (lock()) {
            lastActivityMillis = millis();
            keepAlive = true;
            unlock();
        }
        request->send(200, "text/plain", "alive");
    });

    // ---- Login connect ----
    server.on("/connect", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            static String body;
            if (index == 0) body = "";
            body += String((char*)data, len);
            if (index + len != total) return;

            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, body)) {
                body = "";
                request->send(400, "application/json",
                              "{\"error\":\"Invalid JSON\"}");
                return;
            }
            body = "";

            const String username = doc["username"] | "";
            const String password = doc["password"] | "";
            if (username.isEmpty() || password.isEmpty()) {
                request->send(400, "application/json",
                              "{\"error\":\"Missing fields\"}");
                return;
            }

            if (wifiStatus != WiFiStatus::NotConnected) {
                request->send(403, "application/json",
                              "{\"error\":\"Already connected\"}");
                return;
            }

            String adminUser = CONF->GetString(ADMIN_ID_KEY, "");
            String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
            String userUser  = CONF->GetString(USER_ID_KEY, "");
            String userPass  = CONF->GetString(USER_PASS_KEY, "");

            if (username == adminUser && password == adminPass) {
                BUZZ->successSound();
                onAdminConnected();
                RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
                request->redirect("/admin.html");
                return;
            }
            if (username == userUser && password == userPass) {
                BUZZ->successSound();
                onUserConnected();
                RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
                request->redirect("/user.html");
                return;
            }

            BUZZ->bipFault();
            request->redirect("/login_failed.html");
        }
    );

    // ---- Session history (JSON) ----
    server.on("/session_history", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(POWERTRACKER_HISTORY_FILE)) {
                request->send(SPIFFS,
                              POWERTRACKER_HISTORY_FILE,
                              "application/json");
                return;
            }

            StaticJsonDocument<2048> doc;
            JsonArray arr = doc.createNestedArray("history");

            uint16_t count = POWER_TRACKER->getHistoryCount();
            for (uint16_t i = 0; i < count; ++i) {
                PowerTracker::HistoryEntry h;
                if (!POWER_TRACKER->getHistoryEntry(i, h) || !h.valid) continue;

                JsonObject row = arr.createNestedObject();
                row["start_ms"]      = h.startMs;
                row["duration_s"]    = h.stats.duration_s;
                row["energy_Wh"]     = h.stats.energy_Wh;
                row["peakPower_W"]   = h.stats.peakPower_W;
                row["peakCurrent_A"] = h.stats.peakCurrent_A;
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    server.on("/History.json", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(POWERTRACKER_HISTORY_FILE)) {
                request->send(SPIFFS,
                              POWERTRACKER_HISTORY_FILE,
                              "application/json");
            } else {
                request->send(200, "application/json", "{\"history\":[]}");
            }
        }
    );

    // ---- Disconnect ----
    server.on("/disconnect", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            static String body;
            if (index == 0) body = "";
            body += String((char*)data, len);
            if (index + len != total) return;

            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, body)) {
                body = "";
                request->send(400, "application/json",
                              "{\"error\":\"Invalid JSON\"}");
                return;
            }
            body = "";

            if ((String)(doc["action"] | "") != "disconnect") {
                request->send(400, "application/json",
                              "{\"error\":\"Invalid action\"}");
                return;
            }

            onDisconnected();
            if (lock()) {
                lastActivityMillis = millis();
                keepAlive = false;
                unlock();
            }
            RGB->postOverlay(OverlayEvent::WIFI_LOST);
            request->redirect("/login.html");
        }
    );

    // ---- Monitor (uses snapshot) ----
    server.on("/monitor", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            StatusSnapshot s;
            if (!getSnapshot(s)) {
                request->send(503, "application/json",
                              "{\"error\":\"snapshot_busy\"}");
                return;
            }

            StaticJsonDocument<768> doc;

            doc["capVoltage"] = s.capVoltage;
            doc["current"]    = s.current;

            JsonArray temps = doc.createNestedArray("temperatures");
            for (uint8_t i = 0; i < MAX_TEMP_SENSORS; ++i) {
                temps.add(s.temps[i]);
            }

            JsonArray wireTemps = doc.createNestedArray("wireTemps");
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                const float t = s.wireTemps[i];
                wireTemps.add(isfinite(t) ? (int)lroundf(t) : -127);
            }

            doc["ready"] = digitalRead(READY_LED_PIN);
            doc["off"]   = digitalRead(POWER_OFF_LED_PIN);
            doc["ac"]    = s.acPresent;
            doc["relay"] = s.relayOn;

            JsonObject outputs = doc.createNestedObject("outputs");
            for (int i = 0; i < HeaterManager::kWireCount; ++i) {
                outputs["output" + String(i + 1)] = s.outputs[i];
            }

            doc["fanSpeed"] = FAN->getSpeedPercent();

            // Totals + session snapshot
            {
                JsonObject totals = doc.createNestedObject("sessionTotals");
                totals["totalEnergy_Wh"]  = POWER_TRACKER->getTotalEnergy_Wh();
                totals["totalSessions"]   = POWER_TRACKER->getTotalSessions();
                totals["totalSessionsOk"] = POWER_TRACKER->getTotalSuccessful();
            }
            {
                JsonObject sess = doc.createNestedObject("session");
                PowerTracker::SessionStats cur =
                    POWER_TRACKER->getCurrentSessionSnapshot();
                const auto& last = POWER_TRACKER->getLastSession();

                if (cur.valid) {
                    sess["valid"]         = true;
                    sess["running"]       = true;
                    sess["energy_Wh"]     = cur.energy_Wh;
                    sess["duration_s"]    = cur.duration_s;
                    sess["peakPower_W"]   = cur.peakPower_W;
                    sess["peakCurrent_A"] = cur.peakCurrent_A;
                } else if (last.valid) {
                    sess["valid"]         = true;
                    sess["running"]       = false;
                    sess["energy_Wh"]     = last.energy_Wh;
                    sess["duration_s"]    = last.duration_s;
                    sess["peakPower_W"]   = last.peakPower_W;
                    sess["peakCurrent_A"] = last.peakCurrent_A;
                } else {
                    sess["valid"]   = false;
                    sess["running"] = false;
                }
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- CONTROL (queued) ----
    server.on("/control", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            static String body;
            if (index == 0) body = "";
            body += String((char*)data, len);
            if (index + len != total) return;
            if (!isAuthenticated(request)) {
                body = "";
                return;
            }

            StaticJsonDocument<1024> doc;
            if (deserializeJson(doc, body)) {
                body = "";
                request->send(400, "application/json",
                              "{\"error\":\"Invalid JSON\"}");
                return;
            }
            body = "";

            ControlCmd c{};
            const String action = doc["action"] | "";
            const String target = doc["target"] | "";
            JsonVariant value   = doc["value"];

            if (action == "set") {
                if (target == "reboot")                       c.type = CTRL_REBOOT;
                else if (target == "systemReset")             c.type = CTRL_SYS_RESET;
                else if (target == "ledFeedback")             { c.type = CTRL_LED_FEEDBACK_BOOL; c.b1 = value.as<bool>(); }
                else if (target == "onTime")                  { c.type = CTRL_ON_TIME_MS;        c.i1 = value.as<int>(); }
                else if (target == "offTime")                 { c.type = CTRL_OFF_TIME_MS;       c.i1 = value.as<int>(); }
                else if (target == "relay")                   { c.type = CTRL_RELAY_BOOL;        c.b1 = value.as<bool>(); }
                else if (target.startsWith("output"))         { c.type = CTRL_OUTPUT_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "desiredVoltage")          { c.type = CTRL_DESIRED_V;         c.f1 = value.as<float>(); }
                else if (target == "acFrequency")             { c.type = CTRL_AC_FREQ;           c.i1 = value.as<int>(); }
                else if (target == "chargeResistor")          { c.type = CTRL_CHARGE_RES;        c.f1 = value.as<float>(); }
                else if (target == "dcVoltage")               { c.type = CTRL_DC_VOLT;           c.f1 = value.as<float>(); }
                else if (target.startsWith("Access"))         { c.type = CTRL_ACCESS_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "mode")                    c.type = CTRL_MODE_IDLE;
                else if (target == "systemStart")             c.type = CTRL_SYSTEM_START;
                else if (target == "systemShutdown")          c.type = CTRL_SYSTEM_SHUTDOWN;
                else if (target == "bypass")                  { c.type = CTRL_BYPASS_BOOL;       c.b1 = value.as<bool>(); }
                else if (target == "fanSpeed")                { c.type = CTRL_FAN_SPEED;         c.i1 = constrain(value.as<int>(), 0, 100); }
                else if (target == "buzzerMute")              { c.type = CTRL_BUZZER_MUTE;       c.b1 = value.as<bool>(); }
                else if (target.startsWith("wireRes"))        { c.type = CTRL_WIRE_RES;          c.i1 = target.substring(7).toInt(); c.f1 = value.as<float>(); }
                else if (target == "targetRes")               { c.type = CTRL_TARGET_RES;        c.f1 = value.as<float>(); }
                else if (target == "wireOhmPerM")             { c.type = CTRL_WIRE_OHM_PER_M;    c.f1 = value.as<float>(); }
                else {
                    request->send(400, "application/json",
                                  "{\"error\":\"Unknown target\"}");
                    return;
                }

                sendCmd(c);
                request->send(202, "application/json",
                              "{\"status\":\"queued\"}");
            } else if (action == "get" && target == "status") {
                String statusStr;
                switch (DEVICE->currentState) {
                    case DeviceState::Idle:     statusStr = "Idle"; break;
                    case DeviceState::Running:  statusStr = "Running"; break;
                    case DeviceState::Error:    statusStr = "Error"; break;
                    case DeviceState::Shutdown: statusStr = "Shutdown"; break;
                    default:                    statusStr = "Unknown"; break;
                }
                request->send(200, "application/json",
                              "{\"state\":\"" + statusStr + "\"}");
            } else {
                request->send(400, "application/json",
                              "{\"error\":\"Invalid action or target\"}");
            }
        }
    );

    // ---- load_controls (uses snapshot + config) ----
    server.on("/load_controls", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }
            BUZZ->bip();

            if (isAdminConnected())
                RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
            else if (isUserConnected())
                RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);

            StatusSnapshot s;
            if (!getSnapshot(s)) {
                request->send(503, "application/json",
                              "{\"error\":\"snapshot_busy\"}");
                return;
            }

            StaticJsonDocument<1024> doc;

            // Preferences (config only)
            doc["ledFeedback"]    = CONF->GetBool(LED_FEEDBACK_KEY, false);
            doc["onTime"]         = CONF->GetInt(ON_TIME_KEY, 500);
            doc["offTime"]        = CONF->GetInt(OFF_TIME_KEY, 500);
            doc["desiredVoltage"] = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0);
            doc["acFrequency"]    = CONF->GetInt(AC_FREQUENCY_KEY, 50);
            doc["chargeResistor"] = CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f);
            doc["dcVoltage"]      = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
            doc["wireOhmPerM"]    = CONF->GetFloat(WIRE_OHM_PER_M_KEY,
                                                    DEFAULT_WIRE_OHM_PER_M);
            doc["buzzerMute"]     = CONF->GetBool(BUZMUT_KEY, BUZMUT_DEFAULT);

            // Fast bits via snapshot
            doc["relay"] = s.relayOn;
            doc["ready"] = digitalRead(READY_LED_PIN);
            doc["off"]   = digitalRead(POWER_OFF_LED_PIN);

            JsonObject outputs = doc.createNestedObject("outputs");
            for (int i = 0; i < HeaterManager::kWireCount; ++i) {
                outputs["output" + String(i + 1)] = s.outputs[i];
            }

            // Output access flags
            const char* accessKeys[10] = {
                OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                OUT10_ACCESS_KEY
            };
            JsonObject access = doc.createNestedObject("outputAccess");
            for (int i = 0; i < 10; ++i) {
                access["output" + String(i + 1)] =
                    CONF->GetBool(accessKeys[i], false);
            }

            // Wire resistances
            JsonObject wr = doc.createNestedObject("wireRes");
            const char* rkeys[10] = {
                R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
                R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
            };
            for (int i = 0; i < 10; ++i) {
                wr[String(i + 1)] =
                    CONF->GetFloat(rkeys[i], DEFAULT_WIRE_RES_OHMS);
            }

            doc["targetRes"] =
                CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Static & misc ----
    server.on("/favicon.ico", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (lock()) { keepAlive = true; unlock(); }
            request->send(204);
        }
    );

    server.serveStatic("/",       SPIFFS, "/");
    server.serveStatic("/icons/", SPIFFS, "/icons/")
          .setCacheControl("no-store, must-revalidate");
    server.serveStatic("/css/",   SPIFFS, "/css/")
          .setCacheControl("no-store, must-revalidate");
    server.serveStatic("/js/",    SPIFFS, "/js/")
          .setCacheControl("no-store, must-revalidate");
    server.serveStatic("/fonts/", SPIFFS, "/fonts/")
          .setCacheControl("no-store, must-revalidate");
}

// ====================== Common helpers / tasks ======================

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFi] Handling root request 🌐");
    if (lock()) { keepAlive = true; unlock(); }
    request->send(SPIFFS, "/login.html", "text/html");
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
    DEBUG_PRINTLN("[WiFi] WiFi disabled ❌");
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
            DEBUG_PRINTLN("[WiFi] Inactivity timeout ⏳");
            self->disableWiFiAP();
            vTaskDelete(nullptr);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void WiFiManager::startInactivityTimer() {
    resetTimer();
    if (inactivityTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            WiFiManager::inactivityTask,
            "WiFiInactivity",
            2048,
            this,
            1,
            &inactivityTaskHandle,
            APP_CPU_NUM
        );
        DEBUG_PRINTLN("[WiFi] Inactivity timer started ⏱️");
    }
}

// ===================== Auth & heartbeat =====================

void WiFiManager::onUserConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::UserConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] User connected 🌐");
    RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
}

void WiFiManager::onAdminConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::AdminConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] Admin connected 🔐");
    RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
}

void WiFiManager::onDisconnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::NotConnected;
        unlock();
    }
    DEBUG_PRINTLN("[WiFi] All clients disconnected ❌");
    RGB->postOverlay(OverlayEvent::WIFI_LOST);
}

bool WiFiManager::isUserConnected() const {
    return wifiStatus == WiFiStatus::UserConnected;
}

bool WiFiManager::isAdminConnected() const {
    return wifiStatus == WiFiStatus::AdminConnected;
}

bool WiFiManager::isAuthenticated(AsyncWebServerRequest* request) {
    if (wifiStatus == WiFiStatus::NotConnected) {
        request->send(403, "application/json",
                      "{\"error\":\"Not authenticated\"}");
        return false;
    }
    return true;
}

void WiFiManager::heartbeat() {
    if (heartbeatTaskHandle != nullptr) return;

    DEBUG_PRINTLN("[WiFi] Heartbeat Create 🟢");
    BUZZ->bip();

    xTaskCreatePinnedToCore(
        [](void* param) {
            WiFiManager* self = static_cast<WiFiManager*>(param);
            const TickType_t interval = pdMS_TO_TICKS(6000);

            for (;;) {
                vTaskDelay(interval);

                bool user  = self->isUserConnected();
                bool admin = self->isAdminConnected();
                bool ka    = false;

                if (self->lock()) {
                    ka = self->keepAlive;
                    self->unlock();
                } else {
                    ka = self->keepAlive;
                }

                if (!user && !admin) {
                    DEBUG_PRINTLN("[WiFi] Heartbeat deleted 🔴 (no clients)");
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }

                if (!ka) {
                    DEBUG_PRINTLN("[WiFi] ⚠️ Heartbeat timeout – disconnecting");
                    self->onDisconnected();
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    DEBUG_PRINTLN("[WiFi] Heartbeat deleted 🔴");
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
        &heartbeatTaskHandle,
        APP_CPU_NUM
    );
}

void WiFiManager::restartWiFiAP() {
    disableWiFiAP();
    vTaskDelay(pdMS_TO_TICKS(100));
    begin();
}

// ===================== Control queue worker =====================

void WiFiManager::controlTaskTrampoline(void* pv) {
    static_cast<WiFiManager*>(pv)->controlTaskLoop();
    vTaskDelete(nullptr);
}

void WiFiManager::controlTaskLoop() {
    ControlCmd c{};
    for (;;) {
        if (xQueueReceive(_ctrlQueue, &c, portMAX_DELAY) == pdTRUE) {
            handleControl(c);
        }
    }
}

void WiFiManager::sendCmd(const ControlCmd& c) {
    if (_ctrlQueue) {
        xQueueSendToBack(_ctrlQueue, &c, 0); // non-blocking; drop if full
    }
}

void WiFiManager::handleControl(const ControlCmd& c) {
    DEBUG_PRINTF("[WiFi] Handling control type: %d\n",
                 static_cast<int>(c.type));

    switch (c.type) {
        case CTRL_REBOOT:
            DEBUG_PRINTLN("[WiFi] CTRL_REBOOT → Restarting system...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_SYS_RESET:
            DEBUG_PRINTLN("[WiFi] CTRL_SYS_RESET → Full system reset...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->PutBool(RESET_FLAG, true);
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_LED_FEEDBACK_BOOL:
            BUZZ->bip();
            CONF->PutBool(LED_FEEDBACK_KEY, c.b1);
            break;

        case CTRL_BUZZER_MUTE:
            BUZZ->bip();
            BUZZ->setMuted(c.b1);
            break;

        case CTRL_ON_TIME_MS:
            BUZZ->bip();
            CONF->PutInt(ON_TIME_KEY, c.i1);
            break;

        case CTRL_OFF_TIME_MS:
            BUZZ->bip();
            CONF->PutInt(OFF_TIME_KEY, c.i1);
            break;

        case CTRL_RELAY_BOOL:
            BUZZ->bip();
            if (c.b1) {
                DEVICE->relayControl->turnOn();
                RGB->postOverlay(OverlayEvent::RELAY_ON);
            } else {
                DEVICE->relayControl->turnOff();
                RGB->postOverlay(OverlayEvent::RELAY_OFF);
            }
            break;

        case CTRL_OUTPUT_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                if (isAdminConnected()) {
                    WIRE->setOutput(c.i1, c.b1);
                    DEVICE->indicator->setLED(c.i1, c.b1);
                    RGB->postOutputEvent(c.i1, c.b1);
                } else if (isUserConnected()) {
                    const char* accessKeys[10] = {
                        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                        OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                        OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                        OUT10_ACCESS_KEY
                    };
                    bool allowed =
                        CONF->GetBool(accessKeys[c.i1 - 1], false);
                    if (allowed) {
                        WIRE->setOutput(c.i1, c.b1);
                        DEVICE->indicator->setLED(c.i1, c.b1);
                        RGB->postOutputEvent(c.i1, c.b1);
                    }
                }
            }
            break;

        case CTRL_DESIRED_V:
            BUZZ->bip();
            CONF->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, c.f1);
            break;

        case CTRL_AC_FREQ:
            BUZZ->bip();
            CONF->PutInt(AC_FREQUENCY_KEY, c.i1);
            break;

        case CTRL_CHARGE_RES:
            BUZZ->bip();
            CONF->PutFloat(CHARGE_RESISTOR_KEY, c.f1);
            break;

        case CTRL_DC_VOLT:
            BUZZ->bip();
            CONF->PutFloat(DC_VOLTAGE_KEY, c.f1);
            break;

        case CTRL_ACCESS_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                const char* accessKeys[10] = {
                    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                    OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                    OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                    OUT10_ACCESS_KEY
                };
                BUZZ->bip();
                CONF->PutBool(accessKeys[c.i1 - 1], c.b1);
            }
            break;

        case CTRL_MODE_IDLE:
            BUZZ->bip();
            DEVICE->currentState = DeviceState::Idle;
            DEVICE->indicator->clearAll();
            WIRE->disableAll();
            RGB->setIdle();
            break;

        case CTRL_SYSTEM_START:
            BUZZ->bip();
            DEVICE->startLoopTask();
            if (gEvt) {
                xEventGroupSetBits(gEvt,
                                   EVT_WAKE_REQ | EVT_RUN_REQ);
            }
            RGB->postOverlay(OverlayEvent::PWR_START);
            break;

        case CTRL_SYSTEM_SHUTDOWN:
            BUZZ->bip();
            if (gEvt) {
                xEventGroupSetBits(gEvt, EVT_STOP_REQ);
            }
            RGB->postOverlay(OverlayEvent::RELAY_OFF);
            break;

        case CTRL_BYPASS_BOOL:
            BUZZ->bip();
            if (c.b1) DEVICE->bypassFET->enable();
            else      DEVICE->bypassFET->disable();
            break;

        case CTRL_FAN_SPEED: {
            int pct = constrain(c.i1, 0, 100);
            FAN->setSpeedPercent(pct);
            if (pct <= 0) RGB->postOverlay(OverlayEvent::FAN_OFF);
            else          RGB->postOverlay(OverlayEvent::FAN_ON);
            break;
        }

        case CTRL_WIRE_RES: {
            int idx = constrain(c.i1, 1, 10);
            const char* rkeys[10] = {
                R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
                R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
            };
            BUZZ->bip();
            CONF->PutFloat(rkeys[idx - 1], c.f1);
            break;
        }

        case CTRL_TARGET_RES:
            BUZZ->bip();
            CONF->PutFloat(R0XTGT_KEY, c.f1);
            break;

        case CTRL_WIRE_OHM_PER_M: {
            float ohmPerM = c.f1;
            if (ohmPerM <= 0.0f) {
                ohmPerM = DEFAULT_WIRE_OHM_PER_M;
            }
            BUZZ->bip();
            CONF->PutFloat(WIRE_OHM_PER_M_KEY, ohmPerM);
            break;
        }

        default:
            DEBUG_PRINTF("[WiFi] Unknown control type: %d\n",
                         static_cast<int>(c.type));
            break;
    }
}

// ===================== Snapshot task =====================

void WiFiManager::startSnapshotTask(uint32_t periodMs) {
    if (_snapMtx == nullptr) {
        _snapMtx = xSemaphoreCreateMutex();
    }
    if (snapshotTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            WiFiManager::snapshotTask,
            "WiFiSnapshot",
            4096,
            reinterpret_cast<void*>(periodMs),
            1, // low priority
            &snapshotTaskHandle,
            APP_CPU_NUM
        );
    }
}

void WiFiManager::snapshotTask(void* param) {
    const uint32_t periodMs =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    const TickType_t periodTicks =
        pdMS_TO_TICKS(periodMs ? periodMs : 250);

    WiFiManager* self = WiFiManager::Get();
    if (!self) {
        vTaskDelete(nullptr);
    }

    StatusSnapshot local{};

    for (;;) {
        // Cap voltage & current (these should be cheap / cached)
        if (DEVICE && DEVICE->discharger) {
            local.capVoltage = DEVICE->discharger->readCapVoltage();
        } else {
            local.capVoltage = 0.0f;
        }

        if (DEVICE && DEVICE->currentSensor) {
            local.current = DEVICE->currentSensor->readCurrent();
        } else {
            local.current = 0.0f;
        }

        // DS18B20 temps (TempSensor already caches via its own task)
        uint8_t n = 0;
        if (DEVICE && DEVICE->tempSensor) {
            n = DEVICE->tempSensor->getSensorCount();
        }
        if (n > MAX_TEMP_SENSORS) n = MAX_TEMP_SENSORS;

        for (uint8_t i = 0; i < n; ++i) {
            local.temps[i] = DEVICE->tempSensor->getTemperature(i);
        }
        for (uint8_t i = n; i < MAX_TEMP_SENSORS; ++i) {
            local.temps[i] = -127.0f;
        }

        // Virtual wire temps + outputs
        for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
            const float wt = (WIRE
                              ? WIRE->getWireEstimatedTemp(i)
                              : NAN);
            local.wireTemps[i - 1] = isfinite(wt) ? wt : -127.0f;
            local.outputs[i - 1]   =
                (WIRE ? WIRE->getOutputState(i) : false);
        }

        // AC detect + relay state
        local.acPresent =
            (digitalRead(DETECT_12V_PIN) == HIGH);
        local.relayOn =
            (DEVICE && DEVICE->relayControl
             ? DEVICE->relayControl->isOn()
             : false);

        local.updatedMs = millis();

        // Commit snapshot under lock
        if (self->_snapMtx &&
            xSemaphoreTake(self->_snapMtx, portMAX_DELAY) == pdTRUE)
        {
            self->_snap = local;
            xSemaphoreGive(self->_snapMtx);
        }

        vTaskDelay(periodTicks);
    }
}

bool WiFiManager::getSnapshot(StatusSnapshot& out) {
    if (_snapMtx == nullptr) return false;
    if (xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(25)) != pdTRUE) {
        return false;
    }
    out = _snap;
    xSemaphoreGive(_snapMtx);
    return true;
}
