#include "WiFiManager.h"
#include "Utils.h"


// ─────────────────────────────────────────────────────────────
// Begin WiFi Manager
// ─────────────────────────────────────────────────────────────
void WiFiManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
    DEBUG_PRINTLN("###########################################################");

    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("❌ SPIFFS initialization failed!");
        return;
    }
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

    String ssid     = dev->config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME);
    String password = dev->config->GetString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

    if (!WiFi.softAP(ssid.c_str(), password.c_str())) {
        DEBUG_PRINTLN("[WiFiManager] Failed to start AP ❌");
        return;
    }

    DEBUG_PRINTF("✅ AP Started: %s\n", ssid.c_str());

    if (!WFi->softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("[WiFiManager] Failed to set AP config ❌");
        return;
    }


    DEBUG_PRINT("[WiFiManager] AP IP Address: ");
    DEBUG_PRINTLN(WFi->softAPIP());


    // ── Web routes ───────────────────────────────────────────
    
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        resetTimer();// reset activity timer
        handleRoot(request);
    });
    // ─────────────────────────────────────────────────────────────
    // REST API Callbacks – WiFiManager
    // ─────────────────────────────────────────────────────────────

    // 1. Heartbeat – Update keep-alive
    server.on("/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
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
    server.on("/disconnect", HTTP_GET, [this](AsyncWebServerRequest* request) {
        onDisconnected();
        resetTimer();// reset activity timer
        keepAlive = false;
        request->send(200, "application/json", "{\"status\":\"disconnected\"}");
    });

    // 4. Monitor – Return live readings
    server.on("/monitor", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) return;
        StaticJsonDocument<512> doc;
        resetTimer();// reset activity timer
        doc["capVoltage"] = readCapVoltage();
        doc["current"] = dev->currentSensor->readCurrent();

        JsonArray temps = doc.createNestedArray("temperatures");
        for (uint8_t i = 0; i < dev->tempSensor->getSensorCount(); ++i) {
            temps.add(dev->tempSensor->getTemperature(i));
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // 5. Control – Unified command handler with JSON body
    server.on("/control", HTTP_POST,[this](AsyncWebServerRequest* request) {
            // Nothing here; handled in body lambda
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body = "";
            body += String((char*)data);

            // Wait until full body is received
            if (index + len == total) {
                if (!isAuthenticated(request)) return;
                StaticJsonDocument<1024> doc;
                DeserializationError error = deserializeJson(doc, body);
                resetTimer();// reset activity timer
                body = "";  // Clear buffer

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                String action = doc["action"] | "";
                String target = doc["target"] | "";
                auto value    = doc["value"];

                if (action == "set") {
                    if (target == "reboot") {
                        dev->config->RestartSysDelayDown(3000);
                    } else if (target == "reset") {
                        blink(POWER_OFF_LED_PIN, 100);
                        DEBUG_PRINTLN("Long press detected 🕒");
                        DEBUG_PRINTLN("###########################################################");
                        DEBUG_PRINTLN("#                   Resetting device 🔄                   #");
                        DEBUG_PRINTLN("###########################################################");
                        dev->config->PutBool(RESET_FLAG, true);
                        dev->config->RestartSysDelayDown(3000);
                    } else if (target == "ledFeedback") {
                        dev->config->PutBool(LED_FEEDBACK_KEY, value.as<bool>());
                    } else if (target == "onTime") {
                        dev->config->PutInt(ON_TIME_KEY, value.as<int>());
                    } else if (target == "offTime") {
                        dev->config->PutInt(OFF_TIME_KEY, value.as<int>());
                    } else if (target == "relay") {
                        value.as<bool>() ? dev->relayControl->turnOn() : dev->relayControl->turnOff();
                    } else if (target.startsWith("output")) {
                        int index = target.substring(6).toInt();
                        if (index >= 1 && index <= 10) {
                            dev->heaterManager->setOutput(index, value.as<bool>());
                        }
                    } else if (target == "desiredVoltage") {
                        dev->config->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, value.as<float>());
                    } else if (target == "acFrequency") {
                        dev->config->PutInt(AC_FREQUENCY_KEY, value.as<int>());
                    } else if (target == "chargeResistor") {
                        dev->config->PutFloat(CHARGE_RESISTOR_KEY, value.as<float>());
                    } else if (target == "dcVoltage") {
                        dev->config->PutFloat(DC_VOLTAGE_KEY, value.as<float>());
                    } else if (target == "outputAccess") {
                        for (int i = 1; i <= 10; ++i) {
                            String key = "OUT0" + String(i) + "F";
                            dev->config->PutBool(key.c_str(), value[key].as<bool>());
                        }
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
        }
    );

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
        doc["relay"] = dev->relayControl->isOn();

        // Output states 1–10
        JsonObject outputs = doc.createNestedObject("outputs");
        for (int i = 1; i <= 10; ++i) {
            outputs["output" + String(i)] = dev->heaterManager->getOutputState(i);
        }

        // Output access flags 1–10
        JsonObject access = doc.createNestedObject("outputAccess");
        for (int i = 1; i <= 10; ++i) {
            String key = "OUT0" + String(i) + "F";
            access["output" + String(i)] = dev->config->GetBool(key.c_str(), false);
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

                if (!isAdminConnected()) {
                    request->send(403, "application/json", "{\"error\":\"Not allowed\"}");
                    body = "";
                    return;
                }

                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, body);
                body = "";

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                String username = doc["username"] | "";
                String password = doc["password"] | "";

                if (username == "" || password == "") {
                    request->send(400, "application/json", "{\"error\":\"Missing username or password\"}");
                    return;
                }

                dev->config->PutString(ADMIN_ID_KEY, username);
                dev->config->PutString(ADMIN_PASS_KEY, password);

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

                if (!isUserConnected()) {
                    request->send(403, "application/json", "{\"error\":\"Not allowed\"}");
                    body = "";
                    return;
                }

                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, body);
                body = "";

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                String username = doc["username"] | "";
                String password = doc["password"] | "";

                if (username == "" || password == "") {
                    request->send(400, "application/json", "{\"error\":\"Missing username or password\"}");
                    return;
                }

                dev->config->PutString(USER_ID_KEY, username);
                dev->config->PutString(USER_PASS_KEY, password);

                request->send(200, "application/json", "{\"status\":\"user credentials updated\"}");
            }
        }
    );

    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
        keepAlive = true;
        request->send(204);  // No Content
    });

    server.serveStatic("/", SPIFFS, "/");
    server.serveStatic("/icons/", SPIFFS, "/icons/").setCacheControl("max-age=86400");
    server.serveStatic("/css/", SPIFFS, "/css/").setCacheControl("max-age=86400");
    server.serveStatic("/js/", SPIFFS, "/js/").setCacheControl("max-age=86400");
    server.serveStatic("/fonts/", SPIFFS, "/fonts/").setCacheControl("max-age=86400");

    server.begin();

    // Start auto-disable timer
    startInactivityTimer();
}


// ─────────────────────────────────────────────────────────────
// Handle Root Request
// ─────────────────────────────────────────────────────────────
void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFiManager] Handling root request 🌐");
    keepAlive = true;
    request->send(SPIFFS, "/index.html", "text/html");
}


// ─────────────────────────────────────────────────────────────
// Disable Access Point
// ─────────────────────────────────────────────────────────────
void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("[WiFiManager] Disabling WiFi Access Point...");

    WifiState = false;
    prev_WifiState = true;

    WFi->softAPdisconnect(true);
    WFi->mode(WIFI_OFF);

    if (inactivityTaskHandle != nullptr) {
        vTaskDelete(inactivityTaskHandle);
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
                self->disableWiFiAP();
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
    static TaskHandle_t heartbeatTaskHandle = nullptr;

    // Only start if not already running
    if (heartbeatTaskHandle) return;

    xTaskCreatePinnedToCore(
        [](void* param) {
            WiFiManager* self = static_cast<WiFiManager*>(param);
            const TickType_t interval = pdMS_TO_TICKS(6000); // 6s interval

            while (true) {
                vTaskDelay(interval);

                if (!self->keepAlive) {
                    Serial.println("[WiFiManager] ⚠️  Heartbeat timeout – disconnecting");
                    self->onDisconnected();

                    // Delete this task after use
                    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
                    vTaskDelete(currentTask);
                    return;
                }
                // Reset for next heartbeat
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
