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
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
    DEBUG_PRINTLN("###########################################################");

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
    DEBUG_PRINT("[WiFiManager] AP IP Address: "); DEBUG_PRINTLN(WiFi.softAPIP());

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

        doc["ready"] = digitalRead(READY_LED_PIN);
        doc["off"]   = digitalRead(POWER_OFF_LED_PIN);

        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) outputs["output" + String(i)] = DEVICE->heaterManager->getOutputState(i);

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
        bool relayOn           = DEVICE->relayControl->isOn();
        doc["relay"]           = relayOn;
        doc["ready"]           = digitalRead(READY_LED_PIN);
        doc["off"]             = digitalRead(POWER_OFF_LED_PIN);

        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) outputs["output" + String(i)] = DEVICE->heaterManager->getOutputState(i);

        const char* accessKeys[10] = {
            OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
            OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
        };
        JsonObject access = doc.createNestedObject("outputAccess");
        for (int i = 0; i < 10; ++i) access["output" + String(i + 1)] = CONF->GetBool(accessKeys[i], false);

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

void WiFiManager::onUserConnected() {
    if (lock()) { wifiStatus = WiFiStatus::UserConnected; unlock(); }
    DEBUG_PRINTLN("[WiFiManager] User connected 🌐");
    RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
}

void WiFiManager::onAdminConnected() {
    if (lock()) { wifiStatus = WiFiStatus::AdminConnected; unlock(); }
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
    switch (c.type) {
        case CTRL_REBOOT:
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_SYS_RESET:
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->PutBool(RESET_FLAG, true);
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_LED_FEEDBACK_BOOL:
            BUZZ->bip(); CONF->PutBool(LED_FEEDBACK_KEY, c.b1); break;
        
        case CTRL_BUZZER_MUTE:
            BUZZ->bip();                          // optional UI nudge
            BUZZ->setMuted(c.b1);                 // or your existing API to mute
            break;
        case CTRL_ON_TIME_MS:
            BUZZ->bip(); CONF->PutInt(ON_TIME_KEY, c.i1); break;

        case CTRL_OFF_TIME_MS:
            BUZZ->bip(); CONF->PutInt(OFF_TIME_KEY, c.i1); break;

        case CTRL_RELAY_BOOL:
            BUZZ->bip();
            if (c.b1) { DEVICE->relayControl->turnOn();  RGB->postOverlay(OverlayEvent::RELAY_ON);  }
            else      { DEVICE->relayControl->turnOff(); RGB->postOverlay(OverlayEvent::RELAY_OFF); }
            break;

        case CTRL_OUTPUT_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                if (isAdminConnected()) {
                    DEVICE->heaterManager->setOutput(c.i1, c.b1);
                    DEVICE->indicator->setLED(c.i1, c.b1);
                    RGB->postOutputEvent(c.i1, c.b1);
                } else if (isUserConnected()) {
                    const char* accessKeys[10] = {
                        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
                        OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
                    };
                    bool allowed = CONF->GetBool(accessKeys[c.i1 - 1], false);
                    if (allowed) {
                        DEVICE->heaterManager->setOutput(c.i1, c.b1);
                        DEVICE->indicator->setLED(c.i1, c.b1);
                        RGB->postOutputEvent(c.i1, c.b1);
                    }
                }
            }
            break;

        case CTRL_DESIRED_V:
            BUZZ->bip(); CONF->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, c.f1); break;

        case CTRL_AC_FREQ:
            BUZZ->bip(); CONF->PutInt(AC_FREQUENCY_KEY, c.i1); break;

        case CTRL_CHARGE_RES:
            BUZZ->bip(); CONF->PutFloat(CHARGE_RESISTOR_KEY, c.f1); break;

        case CTRL_DC_VOLT:
            BUZZ->bip(); CONF->PutFloat(DC_VOLTAGE_KEY, c.f1); break;

        case CTRL_ACCESS_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                const char* accessKeys[10] = {
                    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
                    OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
                };
                BUZZ->bip(); CONF->PutBool(accessKeys[c.i1 - 1], c.b1);
            }
            break;

        case CTRL_MODE_IDLE:
            DEVICE->currentState = DeviceState::Idle;
            BUZZ->bip();
            DEVICE->indicator->clearAll();
            DEVICE->heaterManager->disableAll();
            RGB->setIdle();  // background hint
            break;

        case CTRL_SYSTEM_START:
            BUZZ->bip();
            DEVICE->startLoopTask();                               // make sure gate task exists
            xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);  // OFF → Power-Up → RUN
            RGB->postOverlay(OverlayEvent::PWR_START);
            break;

        case CTRL_SYSTEM_SHUTDOWN:
            BUZZ->bip();
            xEventGroupSetBits(gEvt, EVT_STOP_REQ);                // RUN/IDLE → OFF
            RGB->postOverlay(OverlayEvent::RELAY_OFF);
            break;
        case CTRL_BYPASS_BOOL:
            BUZZ->bip();
            (c.b1 ? DEVICE->bypassFET->enable() : DEVICE->bypassFET->disable());
            // (no dedicated overlay in RGB enum; leave as-is)
            break;

        case CTRL_FAN_SPEED: {
            int pct = constrain(c.i1, 0, 100);
            FAN->setSpeedPercent(pct);
            // Visual nudge only when crossing 0% boundary
            if (pct <= 0) RGB->postOverlay(OverlayEvent::FAN_OFF);
            else          RGB->postOverlay(OverlayEvent::FAN_ON);
            break;
        }
    }
}
