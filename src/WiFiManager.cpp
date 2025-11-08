#include "WiFiManager.h"
#include "Utils.h"

// ===== Singleton storage & accessors =====
WiFiManager* WiFiManager::instance = nullptr;

void WiFiManager::Init() {
    if (!instance) {
        instance = new WiFiManager();
    }
}

WiFiManager* WiFiManager::Get() {
    return instance; // nullptr until Init() has been called
}

// ===== Ctor kept lightweight; full setup happens in begin() =====
WiFiManager::WiFiManager()
: server(80) {}

void WiFiManager::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();
    // In case someone constructs manually, keep instance in sync.
    if (!instance) instance = this;

    // Create mutex first so all shared state is protected immediately
    _mutex = xSemaphoreCreateMutex();

    // Create control queue + worker task (serialize /control effects)
    _ctrlQueue = xQueueCreate(24, sizeof(ControlCmd));
    xTaskCreatePinnedToCore(controlTaskTrampoline, "WiFiCtrlTask", 4096, this, 1, &_ctrlTask, APP_CPU_NUM);

    // Set initial session state safely
    if (lock()) {
        wifiStatus = WiFiStatus::NotConnected; // (global) keep it consistent
        keepAlive = false;
        WifiState = false;
        prev_WifiState = false;
        unlock();
    }

#if WIFI_START_IN_STA
    if (!StartWifiSTA()) {
        DEBUG_PRINTLN("[WiFiManager] STA connect failed → falling back to AP 📡");
        StartWifiAP();
    }
#else
    StartWifiAP();
#endif

    BUZZ->bipWiFiConnected();
}

// ========================== AP / STA ==========================

void WiFiManager::StartWifiAP() {
    if (lock()) { keepAlive = false; WifiState = true; prev_WifiState = false; unlock(); }

    DEBUG_PRINTLN("[WiFiManager] Starting Access Point ✅");
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(2000));

    const String ap_ssid = CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME);
    const String ap_pass = CONF->GetString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

    if (!WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str())) {
        DEBUG_PRINTLN("[WiFiManager] Failed to start AP ❌");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }
    if (!WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("[WiFiManager] Failed to set AP config ❌");
        BUZZ->bipFault();
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return;
    }

    DEBUG_PRINTF("✅ AP Started: %s\n", ap_ssid.c_str());
    DEBUG_PRINT("[WiFiManager] AP IP Address: "); DEBUG_PRINTLN(WiFi.softAPIP().toString());

    registerRoutes_();
    server.begin();
    startInactivityTimer();

    // Visual cue for AP mode
    RGB->postOverlay(OverlayEvent::WIFI_AP_);
}

// Returns true on successful STA connection
bool WiFiManager::StartWifiSTA() {
    if (lock()) { keepAlive = false; WifiState = true; prev_WifiState = false; unlock(); }

    DEBUG_PRINTLN("[WiFiManager] Starting Station (STA) mode 🚏");

    // Prefer stored creds if available; otherwise use compile-time defaults
    //String ssid = dev->config->GetString(STA_SSID_KEY, WIFI_STA_SSID);
    //String pass = dev->config->GetString(STA_PASS_KEY, WIFI_STA_PASS);
    
    String ssid = WIFI_STA_SSID;
    String pass =  WIFI_STA_PASS;

    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(2000));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_STA_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINTLN("[WiFiManager] STA connect timeout ❌");
        RGB->postOverlay(OverlayEvent::WIFI_LOST);
        return false;
    }

    DEBUG_PRINTF("✅ STA Connected. SSID=%s, IP=%s\n", ssid.c_str(), WiFi.localIP().toString().c_str());

    registerRoutes_();
    server.begin();
    startInactivityTimer();

    // Visual cue for STA join
    RGB->postOverlay(OverlayEvent::WIFI_STATION);
    return true;
}

// Shared route registration (used by AP and STA)
void WiFiManager::registerRoutes_() {
    // ---------- Simple pages ----------
    server.on("/login", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        handleRoot(request);
    });

    // ---------- Heartbeat ----------
    server.on("/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) { BUZZ->bipFault(); request->redirect("/login"); return; }
        if (lock()) { lastActivityMillis = millis(); keepAlive = true; unlock(); }
        request->send(200, "text/plain", "alive");
    });

    // ---------- Connect ----------
    server.on("/connect", HTTP_POST, [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = ""; body += String((char*)data);
            if (index + len != total) return;

            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, body)) { request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); body = ""; return; }
            body = "";

            const String username = doc["username"] | "";
            const String password = doc["password"] | "";
            if (username == "" || password == "") { request->send(400, "application/json", "{\"error\":\"Missing fields\"}"); return; }

            // already connected?
            if (wifiStatus != WiFiStatus::NotConnected) { request->send(403, "application/json", "{\"error\":\"Already connected\"}"); return; }

            // Check stored credentials
            String adminUser = CONF->GetString(ADMIN_ID_KEY, "");
            String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
            String userUser  = CONF->GetString(USER_ID_KEY, "");
            String userPass  = CONF->GetString(USER_PASS_KEY, "");

            if (username == adminUser && password == adminPass) {
                BUZZ->successSound(); onAdminConnected(); RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
                request->redirect("/admin.html"); return;
            }
            if (username == userUser && password == userPass) {
                BUZZ->successSound(); onUserConnected();  RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
                request->redirect("/user.html");  return;
            }

            BUZZ->bipFault();
            request->redirect("/login_failed.html");
        }
    );

    // ---------- Disconnect ----------
    server.on("/disconnect", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = ""; body += String((char*)data);
            if (index + len != total) return;

            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, body)) { request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); body = ""; return; }
            body = "";
            if ((String)(doc["action"] | "") != "disconnect") { request->send(400, "application/json", "{\"error\":\"Invalid action\"}"); return; }

            onDisconnected();
            if (lock()) { lastActivityMillis = millis(); keepAlive = false; unlock(); }
            RGB->postOverlay(OverlayEvent::WIFI_LOST);
            request->redirect("/login.html");
        }
    );

    // ---------- Monitor ----------
    server.on("/monitor", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) return;
        if (lock()) { lastActivityMillis = millis(); unlock(); }

        StaticJsonDocument<768> doc;

        doc["capVoltage"] = DEVICE->discharger->readCapVoltage();
        doc["current"]    = DEVICE->currentSensor->readCurrent();

        JsonArray temps = doc.createNestedArray("temperatures");
        for (uint8_t i = 0; i < MAX_TEMP_SENSORS; ++i) {
            float t = (i < DEVICE->tempSensor->getSensorCount()) ? DEVICE->tempSensor->getTemperature(i) : -127;
            temps.add((t == -127) ? -127 : t);
        }
        JsonArray wireTemps = doc.createNestedArray("wireTemps");
        for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
            float t = WIRE->getWireEstimatedTemp(i);
            // You said “add int”: send rounded int °C, -127 if not valid
            int tInt = (isfinite(t)) ? (int) lroundf(t) : -127;
            wireTemps.add(tInt);
        }
        doc["ready"] = digitalRead(READY_LED_PIN);
        doc["off"]   = digitalRead(POWER_OFF_LED_PIN);
        doc["ac"]    = digitalRead(DETECT_12V_PIN) == HIGH;
        doc["relay"] = DEVICE->relayControl->isOn();
        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) outputs["output" + String(i)] = WIRE->getOutputState(i);

        doc["fanSpeed"] = FAN->getSpeedPercent();

        String json; serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // ---------- CONTROL (now queued) ----------
    server.on("/control", HTTP_POST, [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = ""; body += String((char*)data);
            if (index + len != total) return;
            if (!isAuthenticated(request)) { body = ""; return; }

            StaticJsonDocument<1024> doc;
            if (deserializeJson(doc, body)) { body = ""; request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
            body = "";

            ControlCmd c{};
            const String action = doc["action"] | "";
            const String target = doc["target"] | "";
            JsonVariant value   = doc["value"];

            if (action == "set") {
                if (target == "reboot")                          c.type = CTRL_REBOOT;
                else if (target == "systemReset")               c.type = CTRL_SYS_RESET;
                else if (target == "ledFeedback")               { c.type = CTRL_LED_FEEDBACK_BOOL; c.b1 = value.as<bool>(); }
                else if (target == "onTime")                    { c.type = CTRL_ON_TIME_MS;         c.i1 = value.as<int>(); }
                else if (target == "offTime")                   { c.type = CTRL_OFF_TIME_MS;        c.i1 = value.as<int>(); }
                else if (target == "relay")                     { c.type = CTRL_RELAY_BOOL;         c.b1 = value.as<bool>(); }
                else if (target.startsWith("output"))           { c.type = CTRL_OUTPUT_BOOL;        c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "desiredVoltage")            { c.type = CTRL_DESIRED_V;          c.f1 = value.as<float>(); }
                else if (target == "acFrequency")               { c.type = CTRL_AC_FREQ;            c.i1 = value.as<int>(); }
                else if (target == "chargeResistor")            { c.type = CTRL_CHARGE_RES;         c.f1 = value.as<float>(); }
                else if (target == "dcVoltage")                 { c.type = CTRL_DC_VOLT;            c.f1 = value.as<float>(); }
                else if (target.startsWith("Access"))           { c.type = CTRL_ACCESS_BOOL;        c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "mode")                      c.type = CTRL_MODE_IDLE;
                else if (target == "systemStart")               c.type = CTRL_SYSTEM_START;
                else if (target == "systemShutdown")            c.type = CTRL_SYSTEM_SHUTDOWN;
                else if (target == "bypass")                    { c.type = CTRL_BYPASS_BOOL;        c.b1 = value.as<bool>(); }
                else if (target == "fanSpeed")                  { c.type = CTRL_FAN_SPEED;          c.i1 = constrain(value.as<int>(), 0, 100); }
                else if (target == "buzzerMute")                { c.type = CTRL_BUZZER_MUTE; c.b1 = value.as<bool>(); }
                else if (target.startsWith("wireRes"))        { c.type = CTRL_WIRE_RES; c.i1 = target.substring(7).toInt(); c.f1 = value.as<float>(); }
                else if (target == "targetRes")               { c.type = CTRL_TARGET_RES; c.f1 = value.as<float>(); }
                else if (target == "wireOhmPerM")               { c.type = CTRL_WIRE_OHM_PER_M;     c.f1 = value.as<float>(); } 
                else { request->send(400, "application/json", "{\"error\":\"Unknown target\"}"); return; }

                sendCmd(c);
                // Respond quickly — worker will execute in order
                request->send(202, "application/json", "{\"status\":\"queued\"}");
            } else if (action == "get" && target == "status") {
                String statusStr;
                switch (DEVICE->currentState) {
                    case DeviceState::Idle:     statusStr = "Idle"; break;
                    case DeviceState::Running:  statusStr = "Running"; break;
                    case DeviceState::Error:    statusStr = "Error"; break;
                    case DeviceState::Shutdown: statusStr = "Shutdown"; break;
                    default:                    statusStr = "Unknown"; break;
                }
                request->send(200, "application/json", "{\"state\":\"" + statusStr + "\"}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid action or target\"}");
            }
        }
    );

    // ---------- load_controls ----------
    server.on("/load_controls", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) return;
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        BUZZ->bip();

        // show a soft overlay based on who’s active
        if (isAdminConnected())      RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
        else if (isUserConnected())  RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);

        StaticJsonDocument<1024> doc;
        doc["ledFeedback"]     = CONF->GetBool(LED_FEEDBACK_KEY, false);
        doc["onTime"]          = CONF->GetInt(ON_TIME_KEY, 500);
        doc["offTime"]         = CONF->GetInt(OFF_TIME_KEY, 500);
        doc["desiredVoltage"]  = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0);
        doc["acFrequency"]     = CONF->GetInt(AC_FREQUENCY_KEY, 50);
        doc["chargeResistor"]  = CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f);
        doc["dcVoltage"]       = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        doc["wireOhmPerM"]     = CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M);
        bool relayOn           = DEVICE->relayControl->isOn();
        doc["relay"]           = relayOn;
        doc["ready"]           = digitalRead(READY_LED_PIN);
        doc["off"]             = digitalRead(POWER_OFF_LED_PIN);
        doc["buzzerMute"]      = CONF->GetBool(BUZMUT_KEY, BUZMUT_DEFAULT);

        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) outputs["output" + String(i)] = WIRE->getOutputState(i);

        const char* accessKeys[10] = {
            OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
            OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
        };
        JsonObject access = doc.createNestedObject("outputAccess");
        for (int i = 0; i < 10; ++i) access["output" + String(i + 1)] = CONF->GetBool(accessKeys[i], false);
        // --- Nichrome wire resistances + target ---
        JsonObject wr = doc.createNestedObject("wireRes");
        const char* rkeys[10] = {
        R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
        R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
        };
        for (int i = 0; i < 10; ++i) {
        wr[String(i + 1)] = CONF->GetFloat(rkeys[i], DEFAULT_WIRE_RES_OHMS);
        }
        doc["targetRes"] = CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);

        String json; serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { keepAlive = true; unlock(); }
        request->send(204);
    });

    server.serveStatic("/",      SPIFFS, "/");
    server.serveStatic("/icons/",SPIFFS, "/icons/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/css/",  SPIFFS, "/css/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/js/",   SPIFFS, "/js/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/fonts/",SPIFFS, "/fonts/").setCacheControl("no-store, must-revalidate");
}

// ====================== Common helpers / tasks ======================

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFiManager] Handling root request 🌐");
    if (lock()) { keepAlive = true; unlock(); }
    request->send(SPIFFS, "/login.html", "text/html");
}

void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("[WiFiManager] Disabling WiFi ...");
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (lock()) {
        WifiState = false;
        prev_WifiState = true;
        inactivityTaskHandle = nullptr;
        unlock();
    }
    RGB->postOverlay(OverlayEvent::WIFI_LOST);
    DEBUG_PRINTLN("[WiFiManager] WiFi disabled ❌");
}

void WiFiManager::resetTimer() {
    if (lock()) { lastActivityMillis = millis(); unlock(); }
}

void WiFiManager::inactivityTask(void* param) {
    auto* self = static_cast<WiFiManager*>(param);
    for (;;) {
        bool wifiOn;
        unsigned long last;
        if (self->lock()) { wifiOn = self->WifiState; last = self->lastActivityMillis; self->unlock(); }
        else { wifiOn = self->WifiState; last = self->lastActivityMillis; }

        if (wifiOn) {
            if (millis() - last > INACTIVITY_TIMEOUT_MS) {
                DEBUG_PRINTLN("[WiFiManager] Inactivity timeout ⏳");
                self->disableWiFiAP();
                // disableWiFiAP() already posts WIFI_LOST overlay
                vTaskDelete(nullptr);
            }
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
        DEBUG_PRINTLN("[WiFiManager] Inactivity timer started ⏱️");
    }
}

// WiFiManager.cpp
void WiFiManager::onUserConnected() {
    if (lock()) { wifiStatus = WiFiStatus::UserConnected; unlock(); }
    heartbeat(); // <<< start / refresh the heartbeat task
    DEBUG_PRINTLN("[WiFiManager] User connected 🌐");
    RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
}

void WiFiManager::onAdminConnected() {
    if (lock()) { wifiStatus = WiFiStatus::AdminConnected; unlock(); }
    heartbeat(); // <<< start / refresh the heartbeat task
    DEBUG_PRINTLN("[WiFiManager] Admin connected 🔐");
    RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
}

void WiFiManager::onDisconnected() {
    if (lock()) { wifiStatus = WiFiStatus::NotConnected; unlock(); }
    DEBUG_PRINTLN("[WiFiManager] All clients disconnected ❌");
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
        request->send(403, "application/json", "{\"error\":\"Not authenticated\"}");
        return false;
    }
    return true;
}

void WiFiManager::heartbeat() {
    if (heartbeatTaskHandle != nullptr) return;

    DEBUG_PRINTLN("[WiFiManager] Heartbeat Create 🟢");
    BUZZ->bip();

    xTaskCreatePinnedToCore(
        [](void* param) {
            WiFiManager* self = static_cast<WiFiManager*>(param);
            const TickType_t interval = pdMS_TO_TICKS(6000);
            for (;;) {
                vTaskDelay(interval);

                bool user = self->isUserConnected();
                bool admin = self->isAdminConnected();
                bool ka = false;
                if (self->lock()) { ka = self->keepAlive; self->unlock(); } else { ka = self->keepAlive; }

                if (!user && !admin) {
                    DEBUG_PRINTLN("[WiFiManager] Heartbeat deleted 🔴");
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }
                if (!ka) {
                    DEBUG_PRINTLN("[WiFiManager] ⚠️  Heartbeat timeout – disconnecting");
                    self->onDisconnected();
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    DEBUG_PRINTLN("[WiFiManager] Heartbeat deleted 🔴");
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }
                // prepare for next round
                if (self->lock()) { self->keepAlive = false; self->unlock(); } else { self->keepAlive = false; }
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
    if (_ctrlQueue) xQueueSendToBack(_ctrlQueue, &c, 0); // drop if full
}

void WiFiManager::handleControl(const ControlCmd& c) {
    DEBUG_PRINTF("[WiFiManager] Handling control type: %d\n", static_cast<int>(c.type));

    switch (c.type) {

        case CTRL_REBOOT:
            DEBUG_PRINTLN("[WiFiManager] CTRL_REBOOT → Restarting system...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_SYS_RESET:
            DEBUG_PRINTLN("[WiFiManager] CTRL_SYS_RESET → Full system reset...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->PutBool(RESET_FLAG, true);
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_LED_FEEDBACK_BOOL:
            DEBUG_PRINTF("[WiFiManager] CTRL_LED_FEEDBACK_BOOL → %s\n", c.b1 ? "ON" : "OFF");
            BUZZ->bip();
            CONF->PutBool(LED_FEEDBACK_KEY, c.b1);
            break;

        case CTRL_BUZZER_MUTE:
            DEBUG_PRINTF("[WiFiManager] CTRL_BUZZER_MUTE → %s\n", c.b1 ? "MUTED" : "ACTIVE");
            BUZZ->bip();
            BUZZ->setMuted(c.b1);
            CONF->PutBool(BUZMUT_KEY, c.b1);     // <-- add this line
            break;

        case CTRL_ON_TIME_MS:
            DEBUG_PRINTF("[WiFiManager] CTRL_ON_TIME_MS → %d ms\n", c.i1);
            BUZZ->bip();
            CONF->PutInt(ON_TIME_KEY, c.i1);
            break;

        case CTRL_OFF_TIME_MS:
            DEBUG_PRINTF("[WiFiManager] CTRL_OFF_TIME_MS → %d ms\n", c.i1);
            BUZZ->bip();
            CONF->PutInt(OFF_TIME_KEY, c.i1);
            break;

        case CTRL_RELAY_BOOL:
            DEBUG_PRINTF("[WiFiManager] CTRL_RELAY_BOOL → %s\n", c.b1 ? "ON" : "OFF");
            BUZZ->bip();
            if (c.b1) {
                DEVICE->relayControl->turnOn();
                RGB->postOverlay(OverlayEvent::RELAY_ON);
            } else {
                DEVICE->relayControl->turnOff();
                RGB->postOverlay(OverlayEvent::RELAY_OFF);
            }
            break;
        // inside switch(c.type) in WiFiManager::handleControl(...)
        case CTRL_WIRE_RES: {
            // c.i1 = 1..10 index (set during parsing), c.f1 = ohms value
            int idx = constrain(c.i1, 1, 10);
            const char* rkeys[10] = {
                R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
                R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
            };
            float ohms = c.f1;
            BUZZ->bip();
            CONF->PutFloat(rkeys[idx - 1], ohms);
            DEBUG_PRINTF("[WiFiManager] CTRL_WIRE_RES → R%02d = %.3f Ω saved\n", idx, ohms);
            break;
        }

        case CTRL_TARGET_RES: {
            float tgt = c.f1;
            BUZZ->bip();
            CONF->PutFloat(R0XTGT_KEY, tgt);
            DEBUG_PRINTF("[WiFiManager] CTRL_TARGET_RES → Target = %.3f Ω saved\n", tgt);
            break;
        }

        case CTRL_OUTPUT_BOOL:
            DEBUG_PRINTF("[WiFiManager] CTRL_OUTPUT_BOOL → Output #%d = %s\n", c.i1, c.b1 ? "ON" : "OFF");
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                if (isAdminConnected()) {
                    WIRE->setOutput(c.i1, c.b1);
                    DEVICE->indicator->setLED(c.i1, c.b1);
                    RGB->postOutputEvent(c.i1, c.b1);
                } else if (isUserConnected()) {
                    const char* accessKeys[10] = {
                        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
                        OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
                    };
                    bool allowed = CONF->GetBool(accessKeys[c.i1 - 1], false);
                    DEBUG_PRINTF("[WiFiManager]   User access check: %s\n", allowed ? "ALLOWED" : "DENIED");
                    if (allowed) {
                        WIRE->setOutput(c.i1, c.b1);
                        DEVICE->indicator->setLED(c.i1, c.b1);
                        RGB->postOutputEvent(c.i1, c.b1);
                    }
                }
            }
            break;

        case CTRL_DESIRED_V:
            DEBUG_PRINTF("[WiFiManager] CTRL_DESIRED_V → %.2f V\n", c.f1);
            BUZZ->bip();
            CONF->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, c.f1);
            break;

        case CTRL_AC_FREQ:
            DEBUG_PRINTF("[WiFiManager] CTRL_AC_FREQ → %d Hz\n", c.i1);
            BUZZ->bip();
            CONF->PutInt(AC_FREQUENCY_KEY, c.i1);
            break;

        case CTRL_CHARGE_RES:
            DEBUG_PRINTF("[WiFiManager] CTRL_CHARGE_RES → %.2f Ω\n", c.f1);
            BUZZ->bip();
            CONF->PutFloat(CHARGE_RESISTOR_KEY, c.f1);
            break;

        case CTRL_DC_VOLT:
            DEBUG_PRINTF("[WiFiManager] CTRL_DC_VOLT → %.2f V\n", c.f1);
            BUZZ->bip();
            CONF->PutFloat(DC_VOLTAGE_KEY, c.f1);
            break;

        case CTRL_ACCESS_BOOL:
            DEBUG_PRINTF("[WiFiManager] CTRL_ACCESS_BOOL → Output #%d access = %s\n", c.i1, c.b1 ? "ENABLED" : "DISABLED");
            if (c.i1 >= 1 && c.i1 <= 10) {
                const char* accessKeys[10] = {
                    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
                    OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
                };
                BUZZ->bip();
                CONF->PutBool(accessKeys[c.i1 - 1], c.b1);
                DEBUG_PRINTLN("[WiFiManager] Access updated in config ✅");
            }
            break;

        case CTRL_MODE_IDLE:
            DEBUG_PRINTLN("[WiFiManager] CTRL_MODE_IDLE → Switching to IDLE mode");
            BUZZ->bip();
            DEVICE->currentState = DeviceState::Idle;
            DEVICE->indicator->clearAll();
            WIRE->disableAll();
            RGB->setIdle();
            break;

        case CTRL_SYSTEM_START:
            DEBUG_PRINTLN("[WiFiManager] CTRL_SYSTEM_START → Starting system");
            BUZZ->bip();
            DEVICE->startLoopTask();
            xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
            RGB->postOverlay(OverlayEvent::PWR_START);
            break;

        case CTRL_SYSTEM_SHUTDOWN:
            DEBUG_PRINTLN("[WiFiManager] CTRL_SYSTEM_SHUTDOWN → Initiating shutdown");
            BUZZ->bip();
            xEventGroupSetBits(gEvt, EVT_STOP_REQ);
            RGB->postOverlay(OverlayEvent::RELAY_OFF);
            break;

        case CTRL_BYPASS_BOOL:
            DEBUG_PRINTF("[WiFiManager] CTRL_BYPASS_BOOL → %s\n", c.b1 ? "ENABLED" : "DISABLED");
            BUZZ->bip();
            if (c.b1) DEVICE->bypassFET->enable();
            else      DEVICE->bypassFET->disable();
            break;

        case CTRL_FAN_SPEED: {
            int pct = constrain(c.i1, 0, 100);
            DEBUG_PRINTF("[WiFiManager] CTRL_FAN_SPEED → %d%%\n", pct);
            FAN->setSpeedPercent(pct);
            if (pct <= 0) RGB->postOverlay(OverlayEvent::FAN_OFF);
            else          RGB->postOverlay(OverlayEvent::FAN_ON);
            break;
        }
        case CTRL_WIRE_OHM_PER_M: {
            float ohmPerM = c.f1;
            if (ohmPerM <= 0.0f) {
                DEBUG_PRINTLN("[WiFiManager] CTRL_WIRE_OHM_PER_M → invalid value, using default");
                ohmPerM = DEFAULT_WIRE_OHM_PER_M;
            }
            BUZZ->bip();
            CONF->PutFloat(WIRE_OHM_PER_M_KEY, ohmPerM);
            DEBUG_PRINTF("[WiFiManager] CTRL_WIRE_OHM_PER_M → %.3f Ω/m saved\n", ohmPerM);
            break;
        }

        default:
            DEBUG_PRINTF("[WiFiManager] Unknown control type: %d\n", static_cast<int>(c.type));
            break;
    }
}
