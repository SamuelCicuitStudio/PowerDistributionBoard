#include "comms/WiFiManager.h"
#include "system/Utils.h"
#include "system/DeviceTransport.h"
#include "services/CalibrationRecorder.h"
#include "sensing/NtcSensor.h"
#include "services/ThermalPiControllers.h"
#include "services/ThermalEstimator.h"
#include "services/RTCManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h>
#include <string.h>

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

static const char* floorMaterialToString(int code) {
    switch (code) {
        case FLOOR_MAT_WOOD:     return "wood";
        case FLOOR_MAT_EPOXY:    return "epoxy";
        case FLOOR_MAT_CONCRETE: return "concrete";
        case FLOOR_MAT_SLATE:    return "slate";
        case FLOOR_MAT_MARBLE:   return "marble";
        case FLOOR_MAT_GRANITE:  return "granite";
        default:                 return "wood";
    }
}

static int parseFloorMaterialCode(const String& raw, int fallback) {
    if (raw.isEmpty()) return fallback;

    String s = raw;
    s.toLowerCase();
    s.trim();

    if (s == "wood") return FLOOR_MAT_WOOD;
    if (s == "epoxy") return FLOOR_MAT_EPOXY;
    if (s == "concrete") return FLOOR_MAT_CONCRETE;
    if (s == "slate") return FLOOR_MAT_SLATE;
    if (s == "marble") return FLOOR_MAT_MARBLE;
    if (s == "granite") return FLOOR_MAT_GRANITE;

    bool numeric = true;
    for (size_t i = 0; i < s.length(); ++i) {
        if (!isDigit(s[i])) {
            numeric = false;
            break;
        }
    }
    if (numeric) {
        int v = s.toInt();
        if (v >= FLOOR_MAT_WOOD && v <= FLOOR_MAT_GRANITE) return v;
    }

    return fallback;
}

static bool normalizeHistoryPath(const String& rawName,
                                 String& fullName,
                                 String& baseName,
                                 uint32_t* epochOut) {
    String name = rawName;
    name.trim();
    if (name.isEmpty() || name.indexOf("..") >= 0) return false;

    const int slash = name.lastIndexOf('/');
    baseName = (slash >= 0) ? name.substring(slash + 1) : name;

    const size_t extLen = strlen(CALIB_HISTORY_EXT);

    if (baseName.length() <= extLen || !baseName.endsWith(CALIB_HISTORY_EXT)) return false;
    String epochStr = baseName.substring(0, baseName.length() - extLen);
    if (epochStr.isEmpty()) return false;
    for (size_t i = 0; i < epochStr.length(); ++i) {
        if (!isDigit(epochStr[i])) return false;
    }

    String dir = (slash >= 0) ? name.substring(0, slash) : "";
    if (!dir.isEmpty()) {
        String dirTrimmed = dir;
        dirTrimmed.trim();
        if (dirTrimmed != CALIB_HISTORY_DIR &&
            dirTrimmed != String(CALIB_HISTORY_DIR).substring(1)) {
            return false;
        }
    }

    if (epochOut) {
        *epochOut = static_cast<uint32_t>(epochStr.toInt());
    }

    if (name.startsWith("/")) {
        fullName = name;
    } else if (slash >= 0) {
        fullName = "/" + name;
    } else {
        fullName = String(CALIB_HISTORY_DIR) + "/" + baseName;
    }

    return true;
}

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

const char* WiFiManager::stateName(DeviceState s) {
    switch (s) {
        case DeviceState::Idle:     return "Idle";
        case DeviceState::Running:  return "Running";
        case DeviceState::Error:    return "Error";
        case DeviceState::Shutdown: return "Shutdown";
        default:                    return "Unknown";
    }
}

// ===== Constructor: keep lightweight; real setup in begin() =====

WiFiManager::WiFiManager()
    : server(80) {}

// ========================== begin() ==========================

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
    startLiveStreamTask();  // batched live stream for UI playback

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

    // Start AP
    if (!WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str())) {
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
    DEBUG_PRINTF("âœ… AP Started: %s\n", ap_ssid.c_str());
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

// ======================= Route registration =======================

void WiFiManager::registerRoutes_() {
    // ---- State stream (SSE) ----
    server.addHandler(&stateSse);
    // ---- Live monitor stream (SSE) ----
    server.addHandler(&liveSse);
    // ---- Live monitor sinceSeq (HTTP) ----
    server.on("/monitor_since", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            uint32_t since = 0;
            if (request->hasParam("seq")) {
                since = request->getParam("seq")->value().toInt();
            }

            StaticJsonDocument<3072> doc;
            JsonArray items = doc.createNestedArray("items");
            uint32_t seqStart = 0;
            uint32_t seqEnd = 0;

            if (_snapMtx &&
                xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(20)) == pdTRUE) {
                buildLiveBatch(items, since, seqStart, seqEnd);
                xSemaphoreGive(_snapMtx);
            }

            if (seqStart != 0) {
                doc["seqStart"] = seqStart;
                doc["seqEnd"]   = seqEnd;
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );
    // ---- Live monitor stream (SSE) ----
    server.addHandler(&liveSse);

    // ---- Login page ----
    server.on("/login", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        handleRoot(request);
    });

    // ---- Device info for login ----
    server.on("/device_info", HTTP_GET,
        [](AsyncWebServerRequest* request) {
            StaticJsonDocument<256> doc;
            doc["deviceId"] = CONF->GetString(DEV_ID_KEY, "");
            doc["sw"]       = CONF->GetString(DEV_SW_KEY, DEVICE_SW_VERSION);
            doc["hw"]       = CONF->GetString(DEV_HW_KEY, DEVICE_HW_VERSION);
            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Heartbeat ----
    server.on("/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) {
            BUZZ->bipFault();
            request->redirect("http://powerboard.local/login");
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

    // ---- Device log ----
    server.on("/device_log", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            AsyncResponseStream* response =
                request->beginResponseStream("text/plain");
            Debug::writeMemoryLog(*response);
            request->send(response);
        }
    );

    server.on("/device_log_clear", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Debug::clearMemoryLog();
            request->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ---- Calibration recorder status ----
    server.on("/calib_status", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const CalibrationRecorder::Meta meta = CALIB->getMeta();
            const char* modeStr =
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? "ntc" :
                (meta.mode == CalibrationRecorder::Mode::Model) ? "model" :
                "none";

            StaticJsonDocument<256> doc;
            doc["running"]     = meta.running;
            doc["mode"]        = modeStr;
            doc["count"]       = meta.count;
            doc["capacity"]    = meta.capacity;
            doc["interval_ms"] = meta.intervalMs;
            doc["start_ms"]    = meta.startMs;
            if (meta.startEpoch > 0) {
                doc["start_epoch"] = meta.startEpoch;
            }
            doc["saved"]       = meta.saved;
            doc["saved_ms"]    = meta.savedMs;
            if (meta.savedEpoch > 0) {
                doc["saved_epoch"] = meta.savedEpoch;
            }
            if (isfinite(meta.targetTempC)) {
                doc["target_c"] = meta.targetTempC;
            }
            if (meta.wireIndex > 0) {
                doc["wire_index"] = meta.wireIndex;
            }
            Device::CalibPwmStatus pwm{};
            if (DEVTRAN && DEVTRAN->getCalibrationPwmStatus(pwm)) {
                doc["pwm_running"] = pwm.active;
                if (pwm.wireIndex > 0) doc["pwm_wire_index"] = pwm.wireIndex;
                if (pwm.onMs > 0) doc["pwm_on_ms"] = pwm.onMs;
                if (pwm.offMs > 0) doc["pwm_off_ms"] = pwm.offMs;
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Calibration recorder start ----
    server.on("/calib_start", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

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

            String modeStr = doc["mode"] | "";
            modeStr.toLowerCase();
            CalibrationRecorder::Mode mode = CalibrationRecorder::Mode::None;
            if (modeStr == "ntc") mode = CalibrationRecorder::Mode::Ntc;
            else if (modeStr == "model") mode = CalibrationRecorder::Mode::Model;

            if (mode == CalibrationRecorder::Mode::None) {
                request->send(400, "application/json",
                              "{\"error\":\"Invalid mode\"}");
                return;
            }

            uint32_t intervalMs = doc["interval_ms"] | CalibrationRecorder::kDefaultIntervalMs;
            uint16_t maxSamples = doc["max_samples"] | CalibrationRecorder::kDefaultMaxSamples;
            float targetC = NAN;
            if (!doc["target_c"].isNull()) {
                targetC = doc["target_c"].as<float>();
            }
            uint32_t epoch = 0;
            if (!doc["epoch"].isNull()) {
                epoch = doc["epoch"].as<uint32_t>();
            }
            if (epoch > 0 && RTC) {
                RTC->setUnixTime(epoch);
            }
            int defaultWire = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
            if (defaultWire < 1) defaultWire = 1;
            if (defaultWire > HeaterManager::kWireCount) defaultWire = HeaterManager::kWireCount;
            uint8_t wireIndex = doc["wire_index"] | defaultWire;

            const bool ok = CALIB->start(mode, intervalMs, maxSamples, targetC, wireIndex);
            if (!ok) {
                request->send(400, "application/json",
                              "{\"error\":\"start_failed\"}");
                return;
            }

            if (mode == CalibrationRecorder::Mode::Model) {
                uint32_t pwmOnMs = doc["pwm_on_ms"] | CONF->GetInt(ON_TIME_KEY, 500);
                uint32_t pwmOffMs = doc["pwm_off_ms"] | CONF->GetInt(OFF_TIME_KEY, 500);
                uint8_t pwmWire = doc["pwm_wire_index"] | wireIndex;
                if (!DEVTRAN || !DEVTRAN->startCalibrationPwm(pwmWire, pwmOnMs, pwmOffMs)) {
                    CALIB->stop();
                    request->send(400, "application/json",
                                  "{\"error\":\"pwm_start_failed\"}");
                    return;
                }
            }

            request->send(200, "application/json",
                          "{\"status\":\"ok\",\"running\":true}");
        }
    );

    // ---- Calibration recorder stop ----
    server.on("/calib_stop", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            static String body;
            if (index == 0) body = "";
            body += String((char*)data, len);
            if (index + len != total) return;

            DynamicJsonDocument doc(256);
            if (!body.isEmpty() && !deserializeJson(doc, body)) {
                uint32_t epoch = doc["epoch"] | 0;
                if (epoch > 0 && RTC) {
                    RTC->setUnixTime(epoch);
                }
            }
            body = "";

            const bool saved = CALIB->stopAndSave();
            if (DEVTRAN) {
                DEVTRAN->stopCalibrationPwm();
            }
            String json = String("{\"status\":\"ok\",\"running\":false,\"saved\":") +
                          (saved ? "true" : "false") + "}";
            request->send(200, "application/json", json);
        }
    );

    // ---- Calibration recorder clear ----
    server.on("/calib_clear", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            (void)data; (void)len; (void)index; (void)total;
            CALIB->clear();
            if (DEVTRAN) {
                DEVTRAN->stopCalibrationPwm();
            }

            bool removed = false;
            size_t removedCount = 0;
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(CALIB_MODEL_JSON_FILE)) {
                    removed = SPIFFS.remove(CALIB_MODEL_JSON_FILE);
                }
                auto removeFromDir = [&](File dir) {
                    File file = dir.openNextFile();
                    while (file) {
                        const bool isDir = file.isDirectory();
                        String rawName = file.name();
                        file.close();
                        if (!isDir) {
                            String fullName;
                            String baseName;
                            if (normalizeHistoryPath(rawName, fullName, baseName, nullptr)) {
                                if (SPIFFS.remove(fullName)) {
                                    ++removedCount;
                                }
                            }
                        }
                        file = dir.openNextFile();
                    }
                };

                File historyDir = SPIFFS.open(CALIB_HISTORY_DIR);
                if (historyDir && historyDir.isDirectory()) {
                    removeFromDir(historyDir);
                }

                File root = SPIFFS.open("/");
                if (root && root.isDirectory()) {
                    removeFromDir(root);
                }
            }

            String json = String("{\"status\":\"ok\",\"cleared\":true,\"file_removed\":") +
                          (removed ? "true" : "false") +
                          ",\"history_removed\":" + String(removedCount) + "}";
            request->send(200, "application/json", json);
        }
    );

    // ---- Calibration recorder data (paged) ----
    server.on("/calib_data", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            uint16_t offset = 0;
            uint16_t count = 0;
            if (request->hasParam("offset")) {
                offset = request->getParam("offset")->value().toInt();
            }
            if (request->hasParam("count")) {
                count = request->getParam("count")->value().toInt();
            }
            if (count == 0) count = 200;
            if (count > 200) count = 200;

            const CalibrationRecorder::Meta meta = CALIB->getMeta();
            const uint16_t total = meta.count;

            DynamicJsonDocument doc(4096 + count * 128);
            JsonObject m = doc.createNestedObject("meta");
            const char* modeStr =
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? "ntc" :
                (meta.mode == CalibrationRecorder::Mode::Model) ? "model" :
                "none";
            m["mode"]        = modeStr;
            m["running"]     = meta.running;
            m["count"]       = total;
            m["capacity"]    = meta.capacity;
            m["interval_ms"] = meta.intervalMs;
            m["start_ms"]    = meta.startMs;
            if (meta.startEpoch > 0) m["start_epoch"] = meta.startEpoch;
            m["saved"]       = meta.saved;
            m["saved_ms"]    = meta.savedMs;
            if (meta.savedEpoch > 0) m["saved_epoch"] = meta.savedEpoch;
            if (isfinite(meta.targetTempC)) m["target_c"] = meta.targetTempC;
            if (meta.wireIndex > 0) m["wire_index"] = meta.wireIndex;
            m["offset"] = offset;
            m["limit"]  = count;

            JsonArray arr = doc.createNestedArray("samples");
            CalibrationRecorder::Sample buf[32];
            uint16_t copied = 0;
            while (copied < count) {
                const size_t chunk = (count - copied) < 32 ? (count - copied) : 32;
                const size_t got = CALIB->copySamples(offset + copied, buf, chunk);
                if (got == 0) break;
                for (size_t i = 0; i < got; ++i) {
                    JsonObject row = arr.createNestedObject();
                    row["t_ms"]     = buf[i].tMs;
                    row["v"]        = buf[i].voltageV;
                    row["i"]        = buf[i].currentA;
                    row["temp_c"]   = buf[i].tempC;
                    row["ntc_v"]    = buf[i].ntcVolts;
                    row["ntc_ohm"]  = buf[i].ntcOhm;
                    row["ntc_adc"]  = buf[i].ntcAdc;
                    row["ntc_ok"]   = buf[i].ntcValid;
                    row["pressed"]  = buf[i].pressed;
                }
                copied += got;
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Calibration recorder file (json) ----
    server.on("/calib_file", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(CALIB_MODEL_JSON_FILE)) {
                request->send(SPIFFS, CALIB_MODEL_JSON_FILE, "application/json");
            } else {
                request->send(404, "application/json", "{\"error\":\"not_found\"}");
            }
        }
    );

    // ---- Calibration history list (json) ----
    server.on("/calib_history_list", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            DynamicJsonDocument doc(2048);
            JsonArray items = doc.createNestedArray("items");

            if (SPIFFS.begin(false)) {
                auto addItem = [&](const String& rawName) {
                    String fullName;
                    String baseName;
                    uint32_t epoch = 0;
                    if (!normalizeHistoryPath(rawName, fullName, baseName, &epoch)) return;
                    for (JsonObject obj : items) {
                        const char* existing = obj["name"];
                        if (existing && fullName == existing) return;
                    }
                    JsonObject row = items.createNestedObject();
                    row["name"] = fullName;
                    if (epoch > 0) row["start_epoch"] = epoch;
                };

                File historyDir = SPIFFS.open(CALIB_HISTORY_DIR);
                if (historyDir && historyDir.isDirectory()) {
                    File file = historyDir.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            addItem(file.name());
                        }
                        file.close();
                        file = historyDir.openNextFile();
                    }
                }

                File root = SPIFFS.open("/");
                if (root && root.isDirectory()) {
                    File file = root.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            addItem(file.name());
                        }
                        file.close();
                        file = root.openNextFile();
                    }
                }
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Calibration history file (json) ----
    server.on("/calib_history_file", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!request->hasParam("name")) {
                request->send(400, "application/json", "{\"error\":\"missing_name\"}");
                return;
            }
            String name = request->getParam("name")->value();
            String fullName;
            String baseName;
            if (!normalizeHistoryPath(name, fullName, baseName, nullptr)) {
                request->send(400, "application/json", "{\"error\":\"invalid_name\"}");
                return;
            }
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(fullName)) {
                    request->send(SPIFFS, fullName, "application/json");
                    return;
                }
                String legacyPath = "/" + baseName;
                if (legacyPath != fullName && SPIFFS.exists(legacyPath)) {
                    request->send(SPIFFS, legacyPath, "application/json");
                    return;
                }
            }
            request->send(404, "application/json", "{\"error\":\"not_found\"}");
        }
    );

    // ---- Calibration PI suggestions (compute) ----
    server.on("/calib_pi_suggest", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            ThermalEstimator::Result r = THERMAL_EST->computeSuggestions(CALIB);
            StaticJsonDocument<512> doc;
            doc["wire_tau"]    = r.tauSec;
            doc["wire_k_loss"] = r.kLoss;
            doc["wire_c"]      = r.thermalC;
            doc["max_power_w"] = r.maxPowerW;
            doc["wire_kp_suggest"] = r.wireKpSuggest;
            doc["wire_ki_suggest"] = r.wireKiSuggest;
            doc["floor_kp_suggest"] = r.floorKpSuggest;
            doc["floor_ki_suggest"] = r.floorKiSuggest;
            doc["wire_kp_current"]  = r.wireKpCurrent;
            doc["wire_ki_current"]  = r.wireKiCurrent;
            doc["floor_kp_current"] = r.floorKpCurrent;
            doc["floor_ki_current"] = r.floorKiCurrent;

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Persist thermal model & PI gains ----
    server.on("/calib_pi_save", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

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

            ThermalEstimator::Result r{};
            if (doc.containsKey("wire_tau"))    r.tauSec   = doc["wire_tau"].as<float>();
            if (doc.containsKey("wire_k_loss")) r.kLoss    = doc["wire_k_loss"].as<float>();
            if (doc.containsKey("wire_c"))      r.thermalC = doc["wire_c"].as<float>();
            if (doc.containsKey("wire_kp"))     r.wireKpSuggest = doc["wire_kp"].as<float>();
            if (doc.containsKey("wire_ki"))     r.wireKiSuggest = doc["wire_ki"].as<float>();
            if (doc.containsKey("floor_kp"))    r.floorKpSuggest = doc["floor_kp"].as<float>();
            if (doc.containsKey("floor_ki"))    r.floorKiSuggest = doc["floor_ki"].as<float>();

            THERMAL_EST->persist(r);

            request->send(200, "application/json",
                          "{\"status\":\"ok\",\"applied\":true}");
        }
    );

    // ---- Wire target test status ----
    server.on("/wire_test_status", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Device::WireTargetStatus st{};
            if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(st)) {
                request->send(503, "application/json",
                              "{\"error\":\"status_unavailable\"}");
                return;
            }

            StaticJsonDocument<256> doc;
            doc["running"] = st.active;
            if (st.wireIndex > 0) doc["wire_index"] = st.wireIndex;
            if (isfinite(st.targetC)) doc["target_c"] = st.targetC;
            if (isfinite(st.tempC)) doc["temp_c"] = st.tempC;
            doc["duty"] = st.duty;
            doc["on_ms"] = st.onMs;
            doc["off_ms"] = st.offMs;
            doc["updated_ms"] = st.updatedMs;

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    );

    // ---- Wire target test start ----
    server.on("/wire_test_start", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

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

            float targetC = doc["target_c"].as<float>();
            uint8_t wireIndex = doc["wire_index"] | 0;

            if (!DEVTRAN || !DEVTRAN->startWireTargetTest(targetC, wireIndex)) {
                request->send(400, "application/json",
                              "{\"error\":\"start_failed\"}");
                return;
            }

            request->send(200, "application/json",
                          "{\"status\":\"ok\",\"running\":true}");
        }
    );

    // ---- Wire target test stop ----
    server.on("/wire_test_stop", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }
            (void)data; (void)len; (void)index; (void)total;

            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            request->send(200, "application/json",
                          "{\"status\":\"ok\",\"running\":false}");
        }
    );

    // ---- NTC calibrate (uses heatsink temp if no ref provided) ----
    server.on("/ntc_calibrate", HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

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

            float refTempC = NAN;
            if (!doc["ref_temp_c"].isNull()) {
                refTempC = doc["ref_temp_c"].as<float>();
            } else if (DEVICE && DEVICE->tempSensor) {
                refTempC = DEVICE->tempSensor->getHeatsinkTemp();
            }

            if (!isfinite(refTempC) || !NTC) {
                request->send(400, "application/json",
                              "{\"error\":\"no_reference\"}");
                return;
            }

            const bool ok = NTC->calibrateAtTempC(refTempC);
            if (!ok) {
                request->send(400, "application/json",
                              "{\"error\":\"calibration_failed\"}");
                return;
            }

            StaticJsonDocument<256> out;
            out["status"] = "ok";
            out["ref_c"] = refTempC;
            out["r0_ohm"] = NTC->getR0();
            String json;
            serializeJson(out, json);
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
            request->redirect("http://powerboard.local/login");
        }
    );

    // ---- Monitor (uses snapshot) ----
    server.on("/monitor", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); keepAlive = true; unlock(); }

            String json;
            if (!getMonitorJson(json)) {
                request->send(503, "application/json",
                              "{\"error\":\"snapshot_busy\"}");
                return;
            }
            request->send(200, "application/json", json);
        }
    );

    // ---- Last stop/error + recent events ----
    server.on("/last_event", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            bool markRead = false;
            if (request->hasParam("mark_read")) {
                const String v = request->getParam("mark_read")->value();
                markRead = (v.length() == 0) ? true : (v.toInt() != 0);
            }

            StaticJsonDocument<3072> doc;
            const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
            doc["state"] = stateName(snap.state);

            if (DEVICE) {
                if (markRead) {
                    DEVICE->markEventHistoryRead();
                }

                Device::LastEventInfo info = DEVICE->getLastEventInfo();
                JsonObject err = doc.createNestedObject("last_error");
                if (info.hasError) {
                    err["reason"] = info.errorReason;
                    if (info.errorMs) err["ms"] = info.errorMs;
                    if (info.errorEpoch) err["epoch"] = info.errorEpoch;
                }
                JsonObject stop = doc.createNestedObject("last_stop");
                if (info.hasStop) {
                    stop["reason"] = info.stopReason;
                    if (info.stopMs) stop["ms"] = info.stopMs;
                    if (info.stopEpoch) stop["epoch"] = info.stopEpoch;
                }

                uint8_t warnCount = 0;
                uint8_t errCount = 0;
                DEVICE->getUnreadEventCounts(warnCount, errCount);
                JsonObject unread = doc.createNestedObject("unread");
                unread["warn"] = warnCount;
                unread["error"] = errCount;

                Device::EventEntry warnEntries[10]{};
                Device::EventEntry errEntries[10]{};
                const size_t warnHistory = DEVICE->getWarningHistory(warnEntries, 10);
                const size_t errHistory = DEVICE->getErrorHistory(errEntries, 10);

                JsonArray warnings = doc.createNestedArray("warnings");
                for (size_t i = 0; i < warnHistory; ++i) {
                    const Device::EventEntry& e = warnEntries[i];
                    JsonObject item = warnings.createNestedObject();
                    item["reason"] = e.reason;
                    if (e.ms) item["ms"] = e.ms;
                    if (e.epoch) item["epoch"] = e.epoch;
                }

                JsonArray errors = doc.createNestedArray("errors");
                for (size_t i = 0; i < errHistory; ++i) {
                    const Device::EventEntry& e = errEntries[i];
                    JsonObject item = errors.createNestedObject();
                    item["reason"] = e.reason;
                    if (e.ms) item["ms"] = e.ms;
                    if (e.epoch) item["epoch"] = e.epoch;
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
            uint32_t epoch = doc["epoch"] | 0;
            if (epoch > 0 && RTC) {
                RTC->setUnixTime(epoch);
            }

            if (action == "set") {
                String valStr = value.isNull() ? String("null") : value.as<String>();
                DEBUG_PRINTF("[WiFi] /control set target=%s value=%s\n",
                             target.c_str(),
                             valStr.c_str());

                if (target == "reboot")                       c.type = CTRL_REBOOT;
                else if (target == "systemReset")             c.type = CTRL_SYS_RESET;
                else if (target == "ledFeedback")             { c.type = CTRL_LED_FEEDBACK_BOOL; c.b1 = value.as<bool>(); }
                else if (target == "onTime")                  { c.type = CTRL_ON_TIME_MS;        c.i1 = value.as<int>(); }
                else if (target == "offTime")                 { c.type = CTRL_OFF_TIME_MS;       c.i1 = value.as<int>(); }
                else if (target == "relay")                   { c.type = CTRL_RELAY_BOOL;        c.b1 = value.as<bool>(); }
                else if (target.startsWith("output"))         { c.type = CTRL_OUTPUT_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "acFrequency")             { c.type = CTRL_AC_FREQ;           c.i1 = value.as<int>(); }
                else if (target == "chargeResistor")          { c.type = CTRL_CHARGE_RES;        c.f1 = value.as<float>(); }
                else if (target.startsWith("Access"))         { c.type = CTRL_ACCESS_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "mode")                    { c.type = CTRL_SET_MODE;          c.b1 = value.as<bool>(); }
                else if (target == "systemStart")             c.type = CTRL_SYSTEM_START;
                else if (target == "systemShutdown")          c.type = CTRL_SYSTEM_SHUTDOWN;
                else if (target == "fanSpeed")                { c.type = CTRL_FAN_SPEED;         c.i1 = constrain(value.as<int>(), 0, 100); }
                else if (target == "buzzerMute")              { c.type = CTRL_BUZZER_MUTE;       c.b1 = value.as<bool>(); }
                else if (target.startsWith("wireRes"))        { c.type = CTRL_WIRE_RES;          c.i1 = target.substring(7).toInt(); c.f1 = value.as<float>(); }
                else if (target == "targetRes")               { c.type = CTRL_TARGET_RES;        c.f1 = value.as<float>(); }
                else if (target == "wireOhmPerM")             { c.type = CTRL_WIRE_OHM_PER_M;    c.f1 = value.as<float>(); }
                else if (target == "wireGauge")               { c.type = CTRL_WIRE_GAUGE;        c.i1 = value.as<int>(); }
                else if (target == "currLimit")               { c.type = CTRL_CURR_LIMIT;        c.f1 = value.as<float>(); }
                else if (target == "adminCredentials") {
                    const String current = value["current"] | "";
                    const String newUser = value["username"] | "";
                    const String newPass = value["password"] | "";
                    const String newSsid = value["wifiSSID"] | "";
                    const String newWifiPass = value["wifiPassword"] | "";

                    const String storedPass = CONF->GetString(ADMIN_PASS_KEY, DEFAULT_ADMIN_PASS);
                    if (current.length() && current != storedPass) {
                        request->send(403, "application/json",
                                      "{\"error\":\"bad_password\"}");
                        return;
                    }

                    if (newUser.length()) CONF->PutString(ADMIN_ID_KEY, newUser);
                    if (newPass.length()) CONF->PutString(ADMIN_PASS_KEY, newPass);
                    if (newSsid.length()) CONF->PutString(STA_SSID_KEY, newSsid);
                    if (newWifiPass.length()) CONF->PutString(STA_PASS_KEY, newWifiPass);

                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "userCredentials") {
                    const String current = value["current"] | "";
                    const String newPass = value["newPass"] | "";
                    const String newId   = value["newId"]   | "";
                    const String storedPass = CONF->GetString(USER_PASS_KEY, DEFAULT_USER_PASS);
                    if (current.length() && current != storedPass) {
                        request->send(403, "application/json",
                                      "{\"error\":\"bad_password\"}");
                        return;
                    }
                    if (newId.length())   CONF->PutString(USER_ID_KEY, newId);
                    if (newPass.length()) CONF->PutString(USER_PASS_KEY, newPass);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wifiSSID") {
                    const String ssid = value.as<String>();
                    if (ssid.length()) CONF->PutString(STA_SSID_KEY, ssid);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wifiPassword") {
                    const String pw = value.as<String>();
                    if (pw.length()) CONF->PutString(STA_PASS_KEY, pw);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "tempWarnC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = 0.0f;
                    CONF->PutFloat(TEMP_WARN_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "tempTripC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_TEMP_THRESHOLD;
                    CONF->PutFloat(TEMP_THRESHOLD_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "idleCurrentA") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = 0.0f;
                    CONF->PutFloat(IDLE_CURR_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wireTauSec") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.05) v = DEFAULT_WIRE_TAU_SEC;
                    if (v > 600.0) v = 600.0;
                    CONF->PutDouble(WIRE_TAU_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wireKLoss") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_K_LOSS;
                    CONF->PutDouble(WIRE_K_LOSS_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wireThermalC") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_THERMAL_C;
                    CONF->PutDouble(WIRE_C_TH_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wireKp") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.0) v = DEFAULT_WIRE_KP;
                    if (THERMAL_PI) THERMAL_PI->setWireKp(v, true);
                    else CONF->PutDouble(WIRE_KP_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "wireKi") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.0) v = DEFAULT_WIRE_KI;
                    if (THERMAL_PI) THERMAL_PI->setWireKi(v, true);
                    else CONF->PutDouble(WIRE_KI_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "floorKp") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.0) v = DEFAULT_FLOOR_KP;
                    if (THERMAL_PI) THERMAL_PI->setFloorKp(v, true);
                    else CONF->PutDouble(FLOOR_KP_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "floorKi") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.0) v = DEFAULT_FLOOR_KI;
                    if (THERMAL_PI) THERMAL_PI->setFloorKi(v, true);
                    else CONF->PutDouble(FLOOR_KI_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcBeta") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_BETA;
                    if (NTC) NTC->setBeta(v, true);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcR0") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_R0_OHMS;
                    if (NTC) NTC->setR0(v, true);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcFixedRes") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_FIXED_RES_OHMS;
                    if (NTC) NTC->setFixedRes(v, true);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcPressMv") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_NTC_PRESS_MV;
                    const float release = CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV);
                    const uint32_t db = static_cast<uint32_t>(
                        CONF->GetInt(NTC_DEBOUNCE_MS_KEY, DEFAULT_NTC_DEBOUNCE_MS));
                    if (NTC) NTC->setButtonThresholdsMv(v, release, db, true);
                    else CONF->PutFloat(NTC_PRESS_MV_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcReleaseMv") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_NTC_RELEASE_MV;
                    const float press = CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV);
                    const uint32_t db = static_cast<uint32_t>(
                        CONF->GetInt(NTC_DEBOUNCE_MS_KEY, DEFAULT_NTC_DEBOUNCE_MS));
                    if (NTC) NTC->setButtonThresholdsMv(press, v, db, true);
                    else CONF->PutFloat(NTC_RELEASE_MV_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcDebounceMs") {
                    int v = value.as<int>();
                    if (v < 0) v = DEFAULT_NTC_DEBOUNCE_MS;
                    const float press = CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV);
                    const float release = CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV);
                    if (NTC) NTC->setButtonThresholdsMv(press, release, static_cast<uint32_t>(v), true);
                    else CONF->PutInt(NTC_DEBOUNCE_MS_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcMinC") {
                    float v = value.as<float>();
                    if (!isfinite(v)) v = DEFAULT_NTC_MIN_C;
                    const float maxC = CONF->GetFloat(NTC_MAX_C_KEY, DEFAULT_NTC_MAX_C);
                    if (NTC) NTC->setTempLimits(v, maxC, true);
                    else CONF->PutFloat(NTC_MIN_C_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcMaxC") {
                    float v = value.as<float>();
                    if (!isfinite(v)) v = DEFAULT_NTC_MAX_C;
                    const float minC = CONF->GetFloat(NTC_MIN_C_KEY, DEFAULT_NTC_MIN_C);
                    if (NTC) NTC->setTempLimits(minC, v, true);
                    else CONF->PutFloat(NTC_MAX_C_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcSamples") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > 64) v = 64;
                    if (NTC) NTC->setSampleCount(static_cast<uint8_t>(v), true);
                    else CONF->PutInt(NTC_SAMPLES_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "ntcGateIndex") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > HeaterManager::kWireCount) v = HeaterManager::kWireCount;
                    CONF->PutInt(NTC_GATE_INDEX_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "floorThicknessMm") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) {
                        v = DEFAULT_FLOOR_THICKNESS_MM;
                    } else if (v > 0.0f) {
                        if (v < FLOOR_THICKNESS_MIN_MM) v = FLOOR_THICKNESS_MIN_MM;
                        if (v > FLOOR_THICKNESS_MAX_MM) v = FLOOR_THICKNESS_MAX_MM;
                    }
                    CONF->PutFloat(FLOOR_THICKNESS_MM_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "floorMaterial") {
                    const int fallback = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
                    int code = fallback;
                    if (value.is<const char*>()) {
                        code = parseFloorMaterialCode(String(value.as<const char*>()), fallback);
                    } else if (value.is<String>()) {
                        code = parseFloorMaterialCode(value.as<String>(), fallback);
                    } else {
                        int v = value.as<int>();
                        if (v >= FLOOR_MAT_WOOD && v <= FLOOR_MAT_GRANITE) {
                            code = v;
                        }
                    }
                    CONF->PutInt(FLOOR_MATERIAL_KEY, code);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "floorMaxC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_FLOOR_MAX_C;
                    if (v > DEFAULT_FLOOR_MAX_C) v = DEFAULT_FLOOR_MAX_C;
                    CONF->PutFloat(FLOOR_MAX_C_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "nichromeFinalTempC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_NICHROME_FINAL_TEMP_C;
                    CONF->PutFloat(NICHROME_FINAL_TEMP_C_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "timingMode") {
                    String modeStr = value.as<String>();
                    modeStr.toLowerCase();
                    int mode = (modeStr == "manual" || value.as<int>() == 1) ? 1 : 0;
                    CONF->PutInt(TIMING_MODE_KEY, mode);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "timingProfile") {
                    String profStr = value.as<String>();
                    profStr.toLowerCase();
                    int prof = 1; // medium default
                    if (profStr.startsWith("hot")) prof = 0;
                    else if (profStr.startsWith("gent")) prof = 2;
                    else {
                        int v = value.as<int>();
                        if (v == 0 || v == 1 || v == 2) prof = v;
                    }
                    CONF->PutInt(TIMING_PROFILE_KEY, prof);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "loopMode") {
                    String modeStr = value.as<String>();
                    modeStr.toLowerCase();
                    int mode = 0;
                    if (modeStr == "sequential" || value.as<int>() == 1) mode = 1;
                    else if (modeStr == "mixed" || value.as<int>() == 2) mode = 2;
                    c.type = CTRL_LOOP_MODE;
                    c.i1   = mode;
                }
                else if (target == "mixPreheatMs") {
                    int v = value.as<int>();
                    if (v < 0) v = 0;
                    if (v > 600000) v = 600000; // cap at 10 minutes
                    CONF->PutInt(MIX_PREHEAT_MS_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "mixPreheatOnMs") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > 1000) v = 1000;
                    CONF->PutInt(MIX_PREHEAT_ON_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "mixKeepMs") {
                    int v = value.as<int>();
                    if (v < 0) v = 0;
                    if (v > 1000) v = 1000;
                    CONF->PutInt(MIX_KEEP_MS_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "mixFrameMs") {
                    int v = value.as<int>();
                    if (v < 10) v = 10;
                    if (v > 2000) v = 2000;
                    CONF->PutInt(MIX_FRAME_MS_KEY, v);
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"applied\":true}");
                    return;
                }
                else if (target == "calibrate")              { c.type = CTRL_CALIBRATE; }
                else {
                    request->send(400, "application/json",
                                  "{\"error\":\"Unknown target\"}");
                    return;
                }

                const bool ok = sendCmd(c);
                if (ok) {
                    request->send(200, "application/json",
                                  "{\"status\":\"ok\",\"queued\":true}");
                } else {
                    request->send(503, "application/json",
                                  "{\"error\":\"ctrl_queue_full\"}");
                }
            } else if (action == "get" && target == "status") {
                const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
                const String statusStr = stateName(snap.state);
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

            StaticJsonDocument<2048> doc;
            const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();

            // Preferences (config only)
            doc["ledFeedback"]    = CONF->GetBool(LED_FEEDBACK_KEY, false);
            doc["onTime"]         = CONF->GetInt(ON_TIME_KEY, 500);
            doc["offTime"]        = CONF->GetInt(OFF_TIME_KEY, 500);
            doc["acFrequency"]    = CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
            doc["chargeResistor"] = CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f);
            doc["deviceId"]       = CONF->GetString(DEV_ID_KEY, "");
            doc["wifiSSID"]       = CONF->GetString(STA_SSID_KEY, DEFAULT_STA_SSID);
            doc["wireOhmPerM"]    = CONF->GetFloat(WIRE_OHM_PER_M_KEY,
                                                    DEFAULT_WIRE_OHM_PER_M);
            doc["wireGauge"]      = CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE);
            doc["buzzerMute"]     = CONF->GetBool(BUZMUT_KEY, BUZMUT_DEFAULT);
            doc["tempTripC"]       = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
            doc["tempWarnC"]       = CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C);
            doc["idleCurrentA"]    = CONF->GetFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);
            doc["wireTauSec"]      = CONF->GetDouble(WIRE_TAU_KEY, DEFAULT_WIRE_TAU_SEC);
            doc["wireKLoss"]       = CONF->GetDouble(WIRE_K_LOSS_KEY, DEFAULT_WIRE_K_LOSS);
            doc["wireThermalC"]    = CONF->GetDouble(WIRE_C_TH_KEY, DEFAULT_WIRE_THERMAL_C);
            doc["wireKp"]          = CONF->GetDouble(WIRE_KP_KEY, DEFAULT_WIRE_KP);
            doc["wireKi"]          = CONF->GetDouble(WIRE_KI_KEY, DEFAULT_WIRE_KI);
            doc["floorKp"]         = CONF->GetDouble(FLOOR_KP_KEY, DEFAULT_FLOOR_KP);
            doc["floorKi"]         = CONF->GetDouble(FLOOR_KI_KEY, DEFAULT_FLOOR_KI);
            doc["ntcBeta"]         = CONF->GetFloat(NTC_BETA_KEY, DEFAULT_NTC_BETA);
            doc["ntcR0"]           = CONF->GetFloat(NTC_R0_KEY, DEFAULT_NTC_R0_OHMS);
            doc["ntcFixedRes"]     = CONF->GetFloat(NTC_FIXED_RES_KEY, DEFAULT_NTC_FIXED_RES_OHMS);
            doc["ntcPressMv"]      = CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV);
            doc["ntcReleaseMv"]    = CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV);
            doc["ntcDebounceMs"]   = CONF->GetInt(NTC_DEBOUNCE_MS_KEY, DEFAULT_NTC_DEBOUNCE_MS);
            doc["ntcMinC"]         = CONF->GetFloat(NTC_MIN_C_KEY, DEFAULT_NTC_MIN_C);
            doc["ntcMaxC"]         = CONF->GetFloat(NTC_MAX_C_KEY, DEFAULT_NTC_MAX_C);
            doc["ntcSamples"]      = CONF->GetInt(NTC_SAMPLES_KEY, DEFAULT_NTC_SAMPLES);
            doc["ntcGateIndex"]    = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
            doc["floorThicknessMm"] = CONF->GetFloat(FLOOR_THICKNESS_MM_KEY, DEFAULT_FLOOR_THICKNESS_MM);
            const int floorMatCode = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
            doc["floorMaterial"]    = floorMaterialToString(floorMatCode);
            doc["floorMaterialCode"] = floorMatCode;
            doc["floorMaxC"]         = CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C);
            doc["nichromeFinalTempC"] = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY, DEFAULT_NICHROME_FINAL_TEMP_C);
            const int loopModeCfg = CONF->GetInt(LOOP_MODE_KEY, DEFAULT_LOOP_MODE);
            const char* loopModeStr =
                (loopModeCfg == 1) ? "sequential" :
                (loopModeCfg == 2) ? "mixed" : "advanced";
            doc["loopMode"]        = loopModeStr;
            doc["mixPreheatMs"]    = CONF->GetInt(MIX_PREHEAT_MS_KEY, DEFAULT_MIX_PREHEAT_MS);
            doc["mixPreheatOnMs"]  = CONF->GetInt(MIX_PREHEAT_ON_KEY, DEFAULT_MIX_PREHEAT_ON_MS);
            doc["mixKeepMs"]       = CONF->GetInt(MIX_KEEP_MS_KEY, DEFAULT_MIX_KEEP_MS);
            doc["mixFrameMs"]      = CONF->GetInt(MIX_FRAME_MS_KEY, DEFAULT_MIX_FRAME_MS);
            const int timingModeCfg = CONF->GetInt(TIMING_MODE_KEY, DEFAULT_TIMING_MODE);
            doc["timingMode"]     = (timingModeCfg == 1) ? "manual" : "preset";
            const int timingProfileCfg = CONF->GetInt(TIMING_PROFILE_KEY, DEFAULT_TIMING_PROFILE);
            const char* profStr =
                (timingProfileCfg == 0) ? "hot" :
                (timingProfileCfg == 2) ? "gentle" : "medium";
            doc["timingProfile"]  = profStr;
            doc["currLimit"]      = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
            doc["capacitanceF"]   = DEVICE ? DEVICE->getCapBankCapF() : 0.0f;
            doc["manualMode"]     = DEVTRAN->isManualMode();
            doc["fanSpeed"]       = FAN->getSpeedPercent();

            // Fast bits via snapshot
            doc["relay"] = s.relayOn;
            doc["ready"] = (snap.state == DeviceState::Idle);
            doc["off"]   = (snap.state == DeviceState::Shutdown);

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
    DEBUG_PRINTLN("[WiFi] Handling root request");
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

// ===================== Auth & heartbeat =====================

void WiFiManager::onUserConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::UserConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] User connected");
    RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
}

void WiFiManager::onAdminConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::AdminConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] Admin connected ");
    RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
}

void WiFiManager::onDisconnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::NotConnected;
        unlock();
    }
    DEBUG_PRINTLN("[WiFi] All clients disconnected");
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

bool WiFiManager::isWifiOn() const {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool on = WifiState;
        xSemaphoreGive(_mutex);
        return on;
    }
    return WifiState;
}

void WiFiManager::heartbeat() {
    if (heartbeatTaskHandle != nullptr) return;

    DEBUG_PRINTLN("[WiFi] Heartbeat Create ");
    BUZZ->bip();

    xTaskCreate(
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
                    DEBUG_PRINTLN("[WiFi] Heartbeat deleted  (no clients)");
                    BUZZ->bipWiFiOff();
                    RGB->postOverlay(OverlayEvent::WIFI_LOST);
                    self->heartbeatTaskHandle = nullptr;
                    vTaskDelete(nullptr);
                }

                if (!ka) {
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

bool WiFiManager::sendCmd(const ControlCmd& c) {
    if (_ctrlQueue) {
        return xQueueSendToBack(_ctrlQueue, &c, 0) == pdTRUE; // non-blocking; drop if full
    }
    return false;
}

bool WiFiManager::handleControl(const ControlCmd& c) {
    DEBUG_PRINTF("[WiFi] Handling control type: %d\n",
                 static_cast<int>(c.type));

    bool ok = true;

    switch (c.type) {
        case CTRL_REBOOT:
            DEBUG_PRINTLN("[WiFi] CTRL_REBOOT Restarting system...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_SYS_RESET:
            DEBUG_PRINTLN("[WiFi] CTRL_SYS_RESET â†’ Full system reset...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            ok = DEVTRAN->requestResetFlagAndRestart();
            break;

        case CTRL_LED_FEEDBACK_BOOL:
            BUZZ->bip();
            ok = DEVTRAN->setLedFeedback(c.b1);
            break;

        case CTRL_BUZZER_MUTE:
            BUZZ->bip();
            ok = DEVTRAN->setBuzzerMute(c.b1);
            break;

        case CTRL_ON_TIME_MS:
            BUZZ->bip();
            ok = DEVTRAN->setOnTimeMs(c.i1);
            break;

        case CTRL_OFF_TIME_MS:
            BUZZ->bip();
            ok = DEVTRAN->setOffTimeMs(c.i1);
            break;

        case CTRL_RELAY_BOOL:
            BUZZ->bip();
            ok = DEVTRAN->setRelay(c.b1, false);
            RGB->postOverlay(c.b1 ? OverlayEvent::RELAY_ON : OverlayEvent::RELAY_OFF);
            break;

        case CTRL_OUTPUT_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                if (isAdminConnected()) {
                    ok = DEVTRAN->setOutput(c.i1, c.b1, true, false);
                    if (ok) RGB->postOutputEvent(c.i1, c.b1);
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
                        ok = DEVTRAN->setOutput(c.i1, c.b1, true, false);
                        if (ok) RGB->postOutputEvent(c.i1, c.b1);
                    } else {
                        ok = false;
                    }
                } else {
                    ok = false;
                }
            } else {
                ok = false;
            }
            break;

        case CTRL_AC_FREQ:
            BUZZ->bip();
            ok = DEVTRAN->setAcFrequency(c.i1);
            break;

        case CTRL_CHARGE_RES:
            BUZZ->bip();
            ok = DEVTRAN->setChargeResistor(c.f1);
            break;

        case CTRL_ACCESS_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                ok = DEVTRAN->setAccessFlag(c.i1, c.b1);
            } else {
                ok = false;
            }
            break;

        case CTRL_SET_MODE:
            BUZZ->bip();
            ok = DEVTRAN->setManualMode(c.b1);
            if (c.b1) {
                ok = ok && DEVTRAN->requestIdle();
            }
            break;

        case CTRL_SYSTEM_START:
            BUZZ->bip();
            ok = DEVTRAN->requestRun();
            if (ok) RGB->postOverlay(OverlayEvent::PWR_START);
            break;

        case CTRL_SYSTEM_SHUTDOWN:
            BUZZ->bip();
            ok = DEVTRAN->requestStop();
            if (ok) RGB->postOverlay(OverlayEvent::RELAY_OFF);
            break;

        case CTRL_FAN_SPEED: {
            int pct = constrain(c.i1, 0, 100);
            ok = DEVTRAN->setFanSpeedPercent(pct, false);
            if (ok) {
                if (pct <= 0) RGB->postOverlay(OverlayEvent::FAN_OFF);
                else          RGB->postOverlay(OverlayEvent::FAN_ON);
            }
            break;
        }

        case CTRL_WIRE_RES: {
            int idx = constrain(c.i1, 1, 10);
            BUZZ->bip();
            ok = DEVTRAN->setWireRes(idx, c.f1);
            break;
        }

        case CTRL_TARGET_RES:
            BUZZ->bip();
            ok = DEVTRAN->setTargetRes(c.f1);
            break;

        case CTRL_WIRE_OHM_PER_M: {
            float ohmPerM = c.f1;
            if (ohmPerM <= 0.0f) {
                ohmPerM = DEFAULT_WIRE_OHM_PER_M;
            }
            BUZZ->bip();
            ok = DEVTRAN->setWireOhmPerM(ohmPerM);
            break;
        }
        case CTRL_WIRE_GAUGE: {
            int awg = constrain(c.i1, 1, 60);
            BUZZ->bip();
            ok = DEVTRAN->setWireGaugeAwg(awg);
            break;
        }
        case CTRL_LOOP_MODE: {
            BUZZ->bip();
            int mode = (c.i1 == 1) ? 1 : 0;
            ok = DEVTRAN->setLoopMode(static_cast<uint8_t>(mode));
            break;
        }

        case CTRL_CURR_LIMIT: {
            BUZZ->bip();
            float limitA = c.f1;
            if (!isfinite(limitA) || limitA < 0.0f) {
                limitA = 0.0f;
            }
            ok = DEVTRAN->setCurrentLimitA(limitA);
            break;
        }

        case CTRL_CALIBRATE:
            BUZZ->bip();
            ok = DEVTRAN->startCalibrationTask();
            break;

        default:
            DEBUG_PRINTF("[WiFi] Unknown control type: %d\n",
                         static_cast<int>(c.type));
            ok = false;
            break;
    }

    DEBUG_PRINTF("[WiFi] Control result type=%d ok=%d\n",
                 static_cast<int>(c.type), ok ? 1 : 0);
    return ok;
}

// ===================== State streaming (SSE) =====================

void WiFiManager::startStateStreamTask() {
    if (stateStreamTaskHandle) return;

    // Send current snapshot on connect
    stateSse.onConnect([this](AsyncEventSourceClient* client) {
        Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
        String json = "{\"state\":\"";
        json += stateName(snap.state);
        json += "\",\"seq\":";
        json += snap.seq;
        json += ",\"sinceMs\":";
        json += snap.sinceMs;
        json += "}";
        client->send(json.c_str(), "state", snap.seq);
    });

    BaseType_t ok = xTaskCreate(
        WiFiManager::stateStreamTask,
        "StateStreamTask",
        3072,
        this,
        1,
        &stateStreamTaskHandle
    );
    if (ok != pdPASS) {
        stateStreamTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFi] Failed to start StateStreamTask");
    }
}

void WiFiManager::stateStreamTask(void* pv) {
    WiFiManager* self = static_cast<WiFiManager*>(pv);
    DeviceTransport* dt = DEVTRAN;

    for (;;) {
        Device::StateSnapshot snap{};
        if (!dt->waitForStateEvent(snap, portMAX_DELAY)) continue;

        String json = "{\"state\":\"";
        json += stateName(snap.state);
        json += "\",\"seq\":";
        json += snap.seq;
        json += ",\"sinceMs\":";
        json += snap.sinceMs;
        json += "}";

        self->stateSse.send(json.c_str(), "state", snap.seq);
    }
}

// ===================== Snapshot task =====================

void WiFiManager::startSnapshotTask(uint32_t periodMs) {
    if (_snapMtx == nullptr) {
        _snapMtx = xSemaphoreCreateMutex();
    }
    _monitorJson.reserve(1024);
    if (snapshotTaskHandle == nullptr) {
        xTaskCreate(
            WiFiManager::snapshotTask,
            "WiFiSnapshot",
            4096,
            reinterpret_cast<void*>(periodMs),
            1, // low priority
            &snapshotTaskHandle
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
    StaticJsonDocument<1024> doc;
    String monitorJson;
    monitorJson.reserve(1024);

    for (;;) {
        // Cap voltage & current (these should be cheap / cached)
        if (DEVICE && DEVICE->discharger) {
            local.capVoltage = DEVICE->discharger->readCapVoltage();
            local.capAdcScaled = DEVICE->discharger->readCapAdcScaled();
        } else {
            local.capVoltage = 0.0f;
            local.capAdcScaled = 0.0f;
        }

        if (DEVICE && DEVICE->currentSensor) {
            if (DEVICE->currentSensor->isContinuousRunning()) {
                local.current = DEVICE->currentSensor->getLastCurrent();
            } else {
                local.current = DEVICE->currentSensor->readCurrent();
            }
        } else {
            local.current = 0.0f;
        }

        // Physical sensor temperatures → dashboard gauges.
        uint8_t n = 0;
        if (DEVICE && DEVICE->tempSensor) {
            n = DEVICE->tempSensor->getSensorCount();
            if (n > MAX_TEMP_SENSORS) n = MAX_TEMP_SENSORS;
            for (uint8_t i = 0; i < n; ++i) {
                const float t = DEVICE->tempSensor->getTemperature(i);
                local.temps[i] = isfinite(t) ? t : -127.0f;
            }
        }
        for (uint8_t i = n; i < MAX_TEMP_SENSORS; ++i) {
            local.temps[i] = -127.0f; // show as "off" when absent
        }

        float board0 = NAN;
        float board1 = NAN;
        float heatsink = NAN;
        if (DEVICE && DEVICE->tempSensor) {
            board0 = DEVICE->tempSensor->getBoardTemp(0);
            board1 = DEVICE->tempSensor->getBoardTemp(1);
            heatsink = DEVICE->tempSensor->getHeatsinkTemp();
        }
        float boardTemp = NAN;
        if (isfinite(board0) && isfinite(board1)) boardTemp = (board0 > board1) ? board0 : board1;
        else if (isfinite(board0)) boardTemp = board0;
        else if (isfinite(board1)) boardTemp = board1;

        // Virtual wire temps + outputs
        for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
            const double wt = (WIRE
                               ? WIRE->getWireEstimatedTemp(i)
                               : NAN);
            local.wireTemps[i - 1] = isfinite(wt) ? wt : -127.0;
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

        // Prebuild the /monitor JSON once per snapshot.
        doc.clear();
        doc["capVoltage"]   = local.capVoltage;
        doc["capAdcRaw"]    = local.capAdcScaled;
        doc["current"]      = local.current;
        doc["capacitanceF"] = DEVICE ? DEVICE->getCapBankCapF() : 0.0f;

        JsonArray temps = doc.createNestedArray("temperatures");
        for (uint8_t i = 0; i < MAX_TEMP_SENSORS; ++i) {
            temps.add(local.temps[i]);
        }
        doc["boardTemp"] = isfinite(boardTemp) ? boardTemp : -127.0f;
        doc["heatsinkTemp"] = isfinite(heatsink) ? heatsink : -127.0f;

        JsonArray wireTemps = doc.createNestedArray("wireTemps");
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            const double t = local.wireTemps[i];
            wireTemps.add(isfinite(t) ? (int)lround(t) : -127);
        }

        const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
        doc["ready"] = (snap.state == DeviceState::Idle);
        doc["off"]   = (snap.state == DeviceState::Shutdown);
        doc["ac"]    = local.acPresent;
        doc["relay"] = local.relayOn;
        if (DEVICE) {
            uint8_t warnCount = 0;
            uint8_t errCount = 0;
            DEVICE->getUnreadEventCounts(warnCount, errCount);
            JsonObject unread = doc.createNestedObject("eventUnread");
            unread["warn"] = warnCount;
            unread["error"] = errCount;
        }

        JsonObject outputs = doc.createNestedObject("outputs");
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            char key[12];
            snprintf(key, sizeof(key), "output%u", (unsigned)(i + 1));
            outputs[key] = local.outputs[i];
        }

        doc["fanSpeed"] = FAN->getSpeedPercent();
        const wifi_mode_t mode = WiFi.getMode();
        const bool staMode = (mode == WIFI_STA || mode == WIFI_AP_STA);
        const bool staConnected = (WiFi.status() == WL_CONNECTED);
        doc["wifiSta"] = staMode;
        doc["wifiConnected"] = staConnected;
        if (staMode && staConnected) {
            doc["wifiRssi"] = WiFi.RSSI();
        }

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

        monitorJson.remove(0);
        serializeJson(doc, monitorJson);

        // Commit snapshot under lock
        if (self->_snapMtx &&
            xSemaphoreTake(self->_snapMtx, portMAX_DELAY) == pdTRUE)
        {
            self->_snap = local;
            self->_monitorJson = monitorJson;
            self->pushLiveSample(local);
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

bool WiFiManager::getMonitorJson(String& out) {
    if (_snapMtx == nullptr) return false;
    if (xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(25)) != pdTRUE) {
        return false;
    }
    if (_monitorJson.length() == 0) {
        xSemaphoreGive(_snapMtx);
        return false;
    }
    out = _monitorJson;
    xSemaphoreGive(_snapMtx);
    return true;
}

// ===================== Live monitor stream (batched SSE) =====================
void WiFiManager::pushLiveSample(const StatusSnapshot& s) {
    // Live push disabled; snapshots are pulled by clients.
    (void)s;
}



bool WiFiManager::buildLiveBatch(JsonArray& items, uint32_t sinceSeq, uint32_t& seqStart, uint32_t& seqEnd) {
    seqStart = 0;
    seqEnd = 0;

    size_t count = _liveCount;
    if (count == 0) return false;

    size_t tail = (_liveHead + kLiveBufSize - count) % kLiveBufSize;

    for (size_t i = 0; i < count; ++i) {
        const size_t idx = (tail + i) % kLiveBufSize;
        const LiveSample& sm = _liveBuf[idx];
        if (sm.seq <= sinceSeq) continue;

        if (seqStart == 0) seqStart = sm.seq;
        seqEnd = sm.seq;

        JsonObject o = items.createNestedObject();
        o["seq"] = sm.seq;
        o["ts"]  = sm.tsMs;
        o["capV"] = sm.capV;
        o["i"]    = sm.currentA;
        o["mask"] = sm.outputsMask;
        o["relay"] = sm.relay;
        o["ac"]    = sm.ac;
        o["fan"]   = sm.fanPct;

        JsonArray wt = o.createNestedArray("wireTemps");
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            wt.add(sm.wireTemps[w]);
        }
    }

    return items.size() > 0;
}

void WiFiManager::startLiveStreamTask(uint32_t /*emitPeriodMs*/) {
    // Live streaming disabled; clients poll snapshots instead.
}



void WiFiManager::liveStreamTask(void* /*pv*/) {
    // Live streaming task disabled.
    vTaskDelete(nullptr);
}
