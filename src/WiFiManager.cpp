#include "WiFiManager.h"
#include "Utils.h"

// Constructor
WiFiManager:: WiFiManager( Device* dev)
        : dev(dev), server(80) {}
// ─────────────────────────────────────────────────────────────
// Begin WiFi Manager
// ─────────────────────────────────────────────────────────────
void WiFiManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
    DEBUG_PRINTLN("###########################################################");


    wifiStatus= WiFiStatus::NotConnected;; // global variable from utils 
    StartWifiAP();
}

// ─────────────────────────────────────────────────────────────
// Start Access Point
// ─────────────────────────────────────────────────────────────
void WiFiManager::StartWifiAP() {
    keepAlive = false;
    WifiState = true;
    prev_WifiState = false;

    DEBUG_PRINTLN("[WiFiManager] Starting Access Point ✅");

    if (!WiFi.softAP(dev->config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME).c_str(),dev->config->GetString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT).c_str())) {
        DEBUG_PRINTLN("[WiFiManager] Failed to start AP ❌");
        return;
    }

    DEBUG_PRINTF("✅ AP Started: %s\n", dev->config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME).c_str());

    if (!WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("[WiFiManager] Failed to set AP config ❌");
        return;
    }


    DEBUG_PRINT("[WiFiManager] AP IP Address: ");
    DEBUG_PRINTLN(WiFi.softAPIP());


    // ── Web routes ───────────────────────────────────────────
    
    server.on("/login", HTTP_GET, [this](AsyncWebServerRequest* request) {
        resetTimer();// reset activity timer
        handleRoot(request);
    });
    // ─────────────────────────────────────────────────────────────
    // REST API Callbacks – WiFiManager
    // ─────────────────────────────────────────────────────────────

    // 1. Heartbeat – Update keep-alive and verify auth
    server.on("/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) {
            DEBUG_PRINTLN("[Heartbeat] Not authenticated ❌ → Redirecting to root");
            request->redirect("/login");
            return;
        }
        DEBUG_PRINTLN("[Heartbeat❤️ ]");
        resetTimer();
        heartbeat();// start heartbeat
        keepAlive = true;
        request->send(200, "text/plain", "alive");
    });

    // 2. Connect – Authenticate user or admin via JSON body
    server.on("/connect", HTTP_POST,[this](AsyncWebServerRequest* request) {
            // Not used for POST body in AsyncWebServer
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = "";

            body += String((char*)data);

            // Wait until full body is received
            if (index + len == total) {
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, body);
                resetTimer();// reset activity timer

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    body = "";
                    return;
                }

                // Extract credentials
                String username = doc["username"] | "";
                String password = doc["password"] | "";

                // Validate presence
                if (username == "" || password == "") {
                    request->send(400, "application/json", "{\"error\":\"Missing username or password\"}");
                    body = "";
                    return;
                }

                // Check stored credentials
                String adminUser = dev->config->GetString(ADMIN_ID_KEY, "");
                String adminPass = dev->config->GetString(ADMIN_PASS_KEY, "");
                String userUser  = dev->config->GetString(USER_ID_KEY, "");
                String userPass  = dev->config->GetString(USER_PASS_KEY, "");

                if (wifiStatus != WiFiStatus::NotConnected) {
                    request->send(403, "application/json", "{\"error\":\"Already connected\"}");
                    body = "";
                    return;
                }

                if (username == adminUser && password == adminPass) {
                    onAdminConnected();
                    request->redirect("/admin.html");
                    body = "";
                    return;
                }

                if (username == userUser && password == userPass) {
                    onUserConnected();
                    request->redirect("/user.html");
                    body = "";
                    return;
                }

                request->redirect("/login_failed.html");  // ❌ Invalid credentials
                body = "";
            }
        }
    );

    // 3. Disconnect
    server.on("/disconnect", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Unused for POST body
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = "";
            body += String((char*)data);

            if (index + len == total) {
                body.trim();  // Clean up formatting
                DynamicJsonDocument doc(256);
                DeserializationError error = deserializeJson(doc, body);
                body = ""; // clear after use

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
                    return;
                }

                String action = doc["action"] | "";
                if (action != "disconnect") {
                    request->send(400, "application/json", "{\"error\":\"Invalid action\"}");
                    return;
                }

                DEBUG_PRINTLN("[Device]  Valid disconnect request received 🔌");

                onDisconnected();  // Clear any session state
                resetTimer();      // Reset watchdog
                keepAlive = false;
                request->redirect("/login.html");  // Trigger redirect
            }
        }
    );

    // 4. Monitor – Return live readings
    server.on("/monitor", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) return;

        resetTimer();  // Reset activity timer

        StaticJsonDocument<768> doc;

        // Measurements
        dev->discharger->startCapVoltageTask();
        doc["capVoltage"] = dev->discharger->readCapVoltage();
        doc["current"] = dev->currentSensor->readCurrent();

        // Temperatures
        JsonArray temps = doc.createNestedArray("temperatures");
        for (uint8_t i = 0; i < 4; ++i) {
            float temp = (i < dev->tempSensor->getSensorCount()) ? dev->tempSensor->getTemperature(i) : -127;
            temps.add((temp == -127) ? -127 : temp);
        }

        // LED status
        doc["ready"] = digitalRead(READY_LED_PIN);
        doc["off"]   = digitalRead(POWER_OFF_LED_PIN);

        // Output states 1–10
        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) {
            outputs["output" + String(i)] = dev->heaterManager->getOutputState(i);
        }

        // Fan speed
        doc["fanSpeed"] = dev->fanManager->getSpeedPercent();

        String json;
        serializeJson(doc, json);
        DEBUG_PRINTLN("[Update] Monitor data sent 🚀");
        request->send(200, "application/json", json);
    });

    // 5. Control – Unified command handler with JSON body
    server.on("/control", HTTP_POST, [this](AsyncWebServerRequest* request) {
        // Empty first-stage handler; actual logic below
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body = "";
        body += String((char*)data);

        if (index + len == total) {
            if (!isAuthenticated(request)) return;

            //resetTimer();
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, body);
            body = ""; // Clear buffer

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            String action = doc["action"] | "";
            String target = doc["target"] | "";
            JsonVariant value = doc["value"];

            if (action == "set") {

                if (target == "reboot") {
                    DEBUG_PRINTLN("⚙️ Reboot requested");
                    blink(POWER_OFF_LED_PIN, 100);
                    dev->config->RestartSysDelayDown(3000);

                } else if (target == "systemReset") {
                    blink(POWER_OFF_LED_PIN, 100);
                    DEBUG_PRINTLN("🔁 Resetting device...");
                    dev->config->PutBool(RESET_FLAG, true);
                    dev->config->RestartSysDelayDown(3000);

                } else if (target == "ledFeedback") {
                    DEBUG_PRINT("🔘 LED Feedback: ");
                    DEBUG_PRINTLN(value.as<bool>());
                    dev->config->PutBool(LED_FEEDBACK_KEY, value.as<bool>());

                } else if (target == "onTime") {
                    dev->config->PutInt(ON_TIME_KEY, value.as<int>());

                } else if (target == "offTime") {
                    dev->config->PutInt(OFF_TIME_KEY, value.as<int>());

                } else if (target == "relay") {
                    DEBUG_PRINT("🔌 Relay: ");
                    DEBUG_PRINTLN(value.as<bool>() ? "ON" : "OFF");
                    value.as<bool>() ? dev->relayControl->turnOn() : dev->relayControl->turnOff();

                } else if (target.startsWith("output")) {
                    int index = target.substring(6).toInt();
                    if (index >= 1 && index <= 10) {
                        bool state = value.as<bool>();
                        DEBUG_PRINTF("🔥 Output %d → %s\n", index, state ? "ON" : "OFF");
                        dev->heaterManager->setOutput(index, state);
                        dev->indicator->setLED(index, state);
                    }

                } else if (target == "desiredVoltage") {
                    dev->config->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, value.as<float>());

                } else if (target == "acFrequency") {
                    dev->config->PutInt(AC_FREQUENCY_KEY, value.as<int>());

                } else if (target == "chargeResistor") {
                    dev->config->PutFloat(CHARGE_RESISTOR_KEY, value.as<float>());

                } else if (target == "dcVoltage") {
                    dev->config->PutFloat(DC_VOLTAGE_KEY, value.as<float>());

                } else if (target.startsWith("Access")) {
                    int index = target.substring(6).toInt();
                    if (index >= 1 && index <= 10) {
                        const char* accessKeys[10] = {
                            OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
                            OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
                        };

                        bool flag = value.as<bool>();
                        DEBUG_PRINTF("🔐 Access %d → %s\n", index, flag ? "true" : "false");
                        dev->config->PutBool(accessKeys[index - 1], flag);
                    }

                } else if (target == "mode") {
                    DEBUG_PRINTLN("🧭 Mode switched → IDLE");
                    dev->currentState = DeviceState::Idle;
                    dev->indicator->clearAll();
                    dev->heaterManager->disableAll();

                } else if (target == "systemStart") {
                    DEBUG_PRINTLN("▶️ System Start requested");
                    if (dev->loopTaskHandle != nullptr && dev->currentState == DeviceState::Idle) {
                        dev->startLoopTask();
                    }
                    if (dev->discharger->readCapVoltage() < 0.78f * dev->config->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE)) {
                        StartFromremote = true;
                    }

                } else if (target == "systemShutdown") {
                    DEBUG_PRINTLN("⏹️ System Shutdown requested");
                    if (dev->currentState == DeviceState::Running) {
                        dev->currentState = DeviceState::Idle;
                    }

                } else if (target == "bypass") {
                    bool state = value.as<bool>();
                    DEBUG_PRINT("🛠️ Bypass: ");
                    DEBUG_PRINTLN(state ? "ENABLED" : "DISABLED");
                    state ? dev->bypassFET->enable() : dev->bypassFET->disable();

                } else if (target == "fanSpeed") {
                    int speed = constrain(value.as<int>(), 0, 100);
                    DEBUG_PRINTF("🌀 Fan speed set to: %d%%\n", speed);
                    dev->fanManager->setSpeedPercent(speed);

                } else {
                    request->send(400, "application/json", "{\"error\":\"Unknown target\"}");
                    return;
                }

                request->send(200, "application/json", "{\"status\":\"ok\"}");

            } else if (action == "get" && target == "status") {
                String statusStr;
                switch (dev->currentState) {
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
    });
    // 6. Load all controllable states for UI initialization
    server.on("/load_controls", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) return;
        resetTimer();

        StaticJsonDocument<1024> doc;

        // Basic control states
        doc["ledFeedback"]     = dev->config->GetBool(LED_FEEDBACK_KEY, false);
        doc["onTime"]          = dev->config->GetInt(ON_TIME_KEY, 500);
        doc["offTime"]         = dev->config->GetInt(OFF_TIME_KEY, 500);
        doc["desiredVoltage"]  = dev->config->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0);
        doc["acFrequency"]     = dev->config->GetInt(AC_FREQUENCY_KEY, 50);
        doc["chargeResistor"]  = dev->config->GetFloat(CHARGE_RESISTOR_KEY, 0.0f);
        doc["dcVoltage"]       = dev->config->GetFloat(DC_VOLTAGE_KEY, 0.0f);

        // Relay state
        bool relayOn = dev->relayControl->isOn();
        doc["relay"] = relayOn;

        // Ready and OFF LED indicators
        doc["ready"] = digitalRead(READY_LED_PIN);     // true if system is in ready state
        doc["off"]   =  digitalRead(POWER_OFF_LED_PIN); ;          // true if system is off

        // Output states 1–10
        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) {
            outputs["output" + String(i)] = dev->heaterManager->getOutputState(i);
        }

        // Output access flags (non-padded keys like "OUT1F", "OUT2F", ...)
        const char* accessKeys[10] = {
            OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
            OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
        };

        JsonObject access = doc.createNestedObject("outputAccess");
        for (int i = 0; i < 10; ++i) {
            access["output" + String(i + 1)] = dev->config->GetBool(accessKeys[i], false);
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    // 7. Set Admin Credentials
    server.on("/SetAdminCred", HTTP_POST, [this](AsyncWebServerRequest* request) { },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = "";
            body += String((char*)data);

            if (index + len == total) {
                resetTimer();

                DEBUG_PRINTLN("[AdminCred] Received request ✅");

                if (!isAdminConnected()) {
                    DEBUG_PRINTLN("[AdminCred] Admin not connected ❌");
                    request->send(403, "application/json", "{\"error\":\"Not allowed\"}");
                    body = "";
                    return;
                }

                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, body);
                body = "";

                if (error) {
                    DEBUG_PRINTLN("[AdminCred] Invalid JSON ❌");
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                String username     = doc["username"]     | "";
                String password     = doc["password"]     | "";
                String current      = doc["current"]      | "";
                String ssid         = doc["ssid"]         | "";
                String wifiPassword = doc["wifiPassword"] | "";

                if (username == "" || password == "") {
                    DEBUG_PRINTLN("[AdminCred] Missing username or password ❌");
                    request->send(400, "application/json", "{\"error\":\"Missing username or password\"}");
                    return;
                }

                // Optionally verify current password
                String stored = dev->config->GetString(ADMIN_PASS_KEY, "");
                if (current != "" && stored != current) {
                    DEBUG_PRINTLN("[AdminCred] Incorrect current password ❌");
                    request->send(403, "application/json", "{\"error\":\"Incorrect current password\"}");
                    return;
                }

                DEBUG_PRINTLN("[AdminCred] Saving admin credentials ✅");
                DEBUG_PRINT("[AdminCred] Username: "); DEBUG_PRINTLN(username);
                dev->config->PutString(ADMIN_ID_KEY, username);
                dev->config->PutString(ADMIN_PASS_KEY, password);

                // Optional Wi-Fi update
                if (ssid != "" && wifiPassword != "") {
                    DEBUG_PRINTLN("[AdminCred] Saving Wi-Fi settings ✅");
                    dev->config->PutString(STA_SSID_KEY, ssid);
                    dev->config->PutString(STA_PASS_KEY, wifiPassword);
                }

                DEBUG_PRINTLN("[AdminCred] Admin credentials updated ✅");
                request->send(200, "application/json", "{\"status\":\"admin credentials updated\"}");
            }
        }
    );

    // 8. Set User Credentials
    server.on("/SetUserCred", HTTP_POST, [this](AsyncWebServerRequest* request) { },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = "";
            body += String((char*)data);

            if (index + len == total) {
                resetTimer();

                DEBUG_PRINTLN("[UserCred] Received request ✅");

                if (!isUserConnected() && !isAdminConnected()) {
                    DEBUG_PRINTLN("[UserCred] User not connected ❌");
                    request->send(403, "application/json", "{\"error\":\"Not allowed\"}");
                    body = "";
                    return;
                }

                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, body);
                body = "";

                if (error) {
                    DEBUG_PRINTLN("[UserCred] Invalid JSON ❌");
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                String current = doc["current"] | "";
                String newUser = doc["username"] | "";
                String newPass = doc["password"] | "";

                if (current == "" || newUser == "" || newPass == "") {
                    DEBUG_PRINTLN("[UserCred] Missing required fields ❌");
                    request->send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                    return;
                }

                String stored = dev->config->GetString(USER_PASS_KEY, "");
                if (stored != current) {
                    DEBUG_PRINTLN("[UserCred] Incorrect current password ❌");
                    request->send(403, "application/json", "{\"error\":\"Incorrect current password\"}");
                    return;
                }

                DEBUG_PRINTLN("[UserCred] Updating user credentials ✅");
                DEBUG_PRINT("[UserCred] New User ID: "); DEBUG_PRINTLN(newUser);

                dev->config->PutString(USER_ID_KEY, newUser);
                dev->config->PutString(USER_PASS_KEY, newPass);

                DEBUG_PRINTLN("[UserCred] User credentials updated ✅");
                request->send(200, "application/json", "{\"status\":\"Credentials updated\"}");
            }
        }
    );
    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
        keepAlive = true;
        request->send(204);  // No Content
    });
    server.serveStatic("/", SPIFFS, "/");
    server.serveStatic("/icons/", SPIFFS, "/icons/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/css/", SPIFFS, "/css/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/js/", SPIFFS, "/js/").setCacheControl("no-store, must-revalidate");
    server.serveStatic("/fonts/", SPIFFS, "/fonts/").setCacheControl("no-store, must-revalidate");
    server.begin();
    //Start auto-disable timer
    startInactivityTimer();
}


// ─────────────────────────────────────────────────────────────
// Handle Root Request
// ─────────────────────────────────────────────────────────────
void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFiManager] Handling root request 🌐");
    keepAlive = true;
    request->send(SPIFFS, "/login.html", "text/html");
}


// ─────────────────────────────────────────────────────────────
// Disable Access Point
// ─────────────────────────────────────────────────────────────
void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("[WiFiManager] Disabling WiFi Access Point...");

    WifiState = false;
    prev_WifiState = true;

    WiFi.softAPdisconnect(true);     // Disconnect AP
    WiFi.disconnect(true);           // Disconnect STA (if connected)
    delay(1000);                      // Let stack settle
    //WiFi.mode(WIFI_OFF);             // Turn off WiFi
    //esp_wifi_deinit();               // 💥 Fully deinitialize WiFi

    if (inactivityTaskHandle != nullptr) {
        inactivityTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFiManager] Inactivity timer stopped 🛑");
    }
    DEBUG_PRINTLN("[WiFiManager] WiFi Access Point disabled ❌");
}

// ─────────────────────────────────────────────────────────────
// Reset Inactivity Timer
// ─────────────────────────────────────────────────────────────
void WiFiManager::resetTimer() {
    lastActivityMillis = millis();
}


// ─────────────────────────────────────────────────────────────
// RTOS Task: WiFi Inactivity Monitor
// ─────────────────────────────────────────────────────────────
void WiFiManager::inactivityTask(void* param) {
    WiFiManager* self = static_cast<WiFiManager*>(param);
    while (true) {
        if (self->WifiState) {
            unsigned long now = millis();
            if ((now - self->lastActivityMillis) > INACTIVITY_TIMEOUT_MS) {
                DEBUG_PRINTLN("[WiFiManager] Inactivity timeout reached ⏳");
                self->disableWiFiAP();     // Disables Wi-Fi and clears task handle
                vTaskDelete(nullptr);      // ✅ Kill this task
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ─────────────────────────────────────────────────────────────
// Start Inactivity Timer Task
// ─────────────────────────────────────────────────────────────
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
    wifiStatus = WiFiStatus::UserConnected;
    DEBUG_PRINTLN("[WiFiManager] User connected 🌐");
}

void WiFiManager::onAdminConnected() {
    wifiStatus = WiFiStatus::AdminConnected;
    DEBUG_PRINTLN("[WiFiManager] Admin connected 🔐");

}

void WiFiManager::onDisconnected() {
    wifiStatus = WiFiStatus::NotConnected;
    DEBUG_PRINTLN("[WiFiManager] All clients disconnected ❌");

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
    // Only start if not already running
    if (heartbeatTaskHandle != nullptr) return;

    DEBUG_PRINTLN("[WiFiManager] Heartbeat Create 🟢");

    xTaskCreatePinnedToCore(
        [](void* param) {
            WiFiManager* self = static_cast<WiFiManager*>(param);
            const TickType_t interval = pdMS_TO_TICKS(6000); // 6s interval

            while (true) {
                vTaskDelay(interval);

                // Delete if only admin remains
                if (!self->isUserConnected() && !self->isAdminConnected()) {
                    DEBUG_PRINTLN("[WiFiManager] Heartbeat deleted 🔴");
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                    return;
                }
                // Delete on timeout
                if (!self->keepAlive) {
                    DEBUG_PRINTLN("[WiFiManager] ⚠️  Heartbeat timeout – disconnecting");
                    self->onDisconnected();
                    DEBUG_PRINTLN("[WiFiManager] Heartbeat deleted 🔴");
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                    return;
                }

                // Reset for next round
                self->keepAlive = false;
            }
        },
        "HeartbeatTask",        // Task name
        2048,                   // Stack size
        this,                   // Parameter
        1,                      // Priority
        &heartbeatTaskHandle,   // Save handle for later reference
        APP_CPU_NUM             // Pin to core
    );
}

void WiFiManager::restartWiFiAP() {
    disableWiFiAP();  // Turn off WiFi & cleanup
    delay(100);       // Small delay to let WiFi stack clean up
    begin();          // Restart AP and all callbacks
}
