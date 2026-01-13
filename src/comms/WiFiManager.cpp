#include <WiFiManager.hpp>
#include <Utils.hpp>
#include <DeviceTransport.hpp>
#include <CalibrationRecorder.hpp>
#include <BusSampler.hpp>
#include <NtcSensor.hpp>
#include <ThermalEstimator.hpp>
#include <RTCManager.hpp>
#include <SPIFFS.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h>
#include <string.h>
#include <math.h>

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

static uint8_t getNtcGateIndexFromConfig() {
    int idx = DEFAULT_NTC_GATE_INDEX;
    if (CONF) {
        idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    }
    if (idx < 1) idx = 1;
    if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
    return static_cast<uint8_t>(idx);
}
} // namespace

static const char* floorMaterialToString(int code) {
    switch (code) {
        case FLOOR_MAT_WOOD:     return FLOOR_MAT_WOOD_STR;
        case FLOOR_MAT_EPOXY:    return FLOOR_MAT_EPOXY_STR;
        case FLOOR_MAT_CONCRETE: return FLOOR_MAT_CONCRETE_STR;
        case FLOOR_MAT_SLATE:    return FLOOR_MAT_SLATE_STR;
        case FLOOR_MAT_MARBLE:   return FLOOR_MAT_MARBLE_STR;
        case FLOOR_MAT_GRANITE:  return FLOOR_MAT_GRANITE_STR;
        default:                 return FLOOR_MAT_WOOD_STR;
    }
}

static int parseFloorMaterialCode(const String& raw, int fallback) {
    if (raw.isEmpty()) return fallback;

    String s = raw;
    s.toLowerCase();
    s.trim();

    if (s == FLOOR_MAT_WOOD_STR) return FLOOR_MAT_WOOD;
    if (s == FLOOR_MAT_EPOXY_STR) return FLOOR_MAT_EPOXY;
    if (s == FLOOR_MAT_CONCRETE_STR) return FLOOR_MAT_CONCRETE;
    if (s == FLOOR_MAT_SLATE_STR) return FLOOR_MAT_SLATE;
    if (s == FLOOR_MAT_MARBLE_STR) return FLOOR_MAT_MARBLE;
    if (s == FLOOR_MAT_GRANITE_STR) return FLOOR_MAT_GRANITE;

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

namespace {
constexpr float kNtcCalTargetDefaultC = 100.0f;
constexpr uint32_t kNtcCalSampleMsDefault = 500;
constexpr uint32_t kNtcCalTimeoutMs = 20 * 60 * 1000;
constexpr uint32_t kNtcCalMinSamples = 6;
constexpr uint32_t kModelCalPollMs = 500;
constexpr uint32_t kModelCalTimeoutMs = 30 * 60 * 1000;
constexpr uint32_t kCalibWakeTimeoutMs = 15000;

struct NtcCalStatus {
    bool     running   = false;
    bool     done      = false;
    bool     error     = false;
    char     errorMsg[96] = {0};
    uint32_t startMs   = 0;
    uint32_t elapsedMs = 0;
    float    targetC   = NAN;
    float    heatsinkC = NAN;
    float    ntcOhm    = NAN;
    uint32_t sampleMs  = 0;
    uint32_t samples   = 0;
    float    shA       = NAN;
    float    shB       = NAN;
    float    shC       = NAN;
    uint8_t  wireIndex = 0;
};

struct NtcCalTaskArgs {
    float    targetC   = kNtcCalTargetDefaultC;
    uint8_t  wireIndex = 1;
    uint32_t sampleMs  = kNtcCalSampleMsDefault;
    uint32_t timeoutMs = kNtcCalTimeoutMs;
    uint32_t startMs   = 0;
};

static SemaphoreHandle_t s_ntcCalMtx = nullptr;
static TaskHandle_t s_ntcCalTask = nullptr;
static NtcCalStatus s_ntcCalStatus{};
static bool s_ntcCalAbort = false;
static TaskHandle_t s_modelCalTask = nullptr;
static bool s_modelCalAbort = false;

static bool waitForIdle(DeviceTransport* transport, uint32_t timeoutMs, DeviceState& lastState) {
    if (!transport) return false;

    Device::StateSnapshot snap = transport->getStateSnapshot();
    lastState = snap.state;
    if (snap.state == DeviceState::Idle) return true;
    if (snap.state != DeviceState::Shutdown) return false;

    if (!transport->requestWake()) return false;

    const uint32_t startMs = millis();
    while ((millis() - startMs) < timeoutMs) {
        Device::StateSnapshot evt{};
        if (transport->waitForStateEvent(evt, pdMS_TO_TICKS(250))) {
            lastState = evt.state;
            if (evt.state == DeviceState::Idle) return true;
        } else {
            snap = transport->getStateSnapshot();
            lastState = snap.state;
            if (snap.state == DeviceState::Idle) return true;
        }
    }

    return false;
}

struct ModelCalTaskArgs {
    float    targetC   = NAN;
    uint8_t  wireIndex = 1;
    uint32_t timeoutMs = kModelCalTimeoutMs;
    uint32_t startMs   = 0;
};

static void ntcCalEnsureMutex() {
    if (!s_ntcCalMtx) {
        s_ntcCalMtx = xSemaphoreCreateMutex();
    }
}

static bool ntcCalLock(TickType_t timeoutTicks = portMAX_DELAY) {
    ntcCalEnsureMutex();
    if (!s_ntcCalMtx) return true;
    return (xSemaphoreTake(s_ntcCalMtx, timeoutTicks) == pdTRUE);
}

static void ntcCalUnlock() {
    if (s_ntcCalMtx) xSemaphoreGive(s_ntcCalMtx);
}

static void ntcCalStartStatus(const NtcCalTaskArgs& args) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = true;
    s_ntcCalStatus.done = false;
    s_ntcCalStatus.error = false;
    s_ntcCalStatus.errorMsg[0] = '\0';
    s_ntcCalAbort = false;
    s_ntcCalStatus.startMs = args.startMs;
    s_ntcCalStatus.elapsedMs = 0;
    s_ntcCalStatus.targetC = args.targetC;
    s_ntcCalStatus.heatsinkC = NAN;
    s_ntcCalStatus.ntcOhm = NAN;
    s_ntcCalStatus.sampleMs = args.sampleMs;
    s_ntcCalStatus.samples = 0;
    s_ntcCalStatus.shA = NAN;
    s_ntcCalStatus.shB = NAN;
    s_ntcCalStatus.shC = NAN;
    s_ntcCalStatus.wireIndex = args.wireIndex;
    ntcCalUnlock();
}

static void ntcCalUpdateProgress(float heatsinkC, float ntcOhm, uint32_t samples, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(25))) return;
    s_ntcCalStatus.heatsinkC = heatsinkC;
    s_ntcCalStatus.ntcOhm = ntcOhm;
    s_ntcCalStatus.samples = samples;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    ntcCalUnlock();
}

static void ntcCalSetError(const char* msg, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = false;
    s_ntcCalStatus.done = false;
    s_ntcCalStatus.error = true;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    if (msg && msg[0]) {
        strlcpy(s_ntcCalStatus.errorMsg, msg, sizeof(s_ntcCalStatus.errorMsg));
    } else {
        s_ntcCalStatus.errorMsg[0] = '\0';
    }
    ntcCalUnlock();
}

static void ntcCalFinish(float a, float b, float c, uint32_t samples, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = false;
    s_ntcCalStatus.done = true;
    s_ntcCalStatus.error = false;
    s_ntcCalStatus.errorMsg[0] = '\0';
    s_ntcCalStatus.shA = a;
    s_ntcCalStatus.shB = b;
    s_ntcCalStatus.shC = c;
    s_ntcCalStatus.samples = samples;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    ntcCalUnlock();
}

static NtcCalStatus ntcCalGetStatus() {
    NtcCalStatus out{};
    if (ntcCalLock(pdMS_TO_TICKS(25))) {
        out = s_ntcCalStatus;
        ntcCalUnlock();
    } else {
        out = s_ntcCalStatus;
    }
    return out;
}

static void ntcCalRequestAbort() {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalAbort = true;
    ntcCalUnlock();
}

static bool ntcCalAbortRequested() {
    bool abort = false;
    if (ntcCalLock(pdMS_TO_TICKS(25))) {
        abort = s_ntcCalAbort;
        ntcCalUnlock();
    } else {
        abort = s_ntcCalAbort;
    }
    return abort;
}

static bool modelCalAbortRequested() {
    return s_modelCalAbort;
}

static void modelCalRequestAbort() {
    s_modelCalAbort = true;
}

static bool solve3x3(const double a[3][3], const double b[3], double out[3]) {
    double m[3][4] = {
        {a[0][0], a[0][1], a[0][2], b[0]},
        {a[1][0], a[1][1], a[1][2], b[1]},
        {a[2][0], a[2][1], a[2][2], b[2]}
    };

    for (int i = 0; i < 3; ++i) {
        int pivot = i;
        double maxAbs = fabs(m[i][i]);
        for (int r = i + 1; r < 3; ++r) {
            const double v = fabs(m[r][i]);
            if (v > maxAbs) {
                maxAbs = v;
                pivot = r;
            }
        }
        if (maxAbs < 1e-12) {
            return false;
        }
        if (pivot != i) {
            for (int c = i; c < 4; ++c) {
                const double tmp = m[i][c];
                m[i][c] = m[pivot][c];
                m[pivot][c] = tmp;
            }
        }
        const double div = m[i][i];
        for (int c = i; c < 4; ++c) {
            m[i][c] /= div;
        }
        for (int r = 0; r < 3; ++r) {
            if (r == i) continue;
            const double factor = m[r][i];
            if (factor == 0.0) continue;
            for (int c = i; c < 4; ++c) {
                m[r][c] -= factor * m[i][c];
            }
        }
    }

    out[0] = m[0][3];
    out[1] = m[1][3];
    out[2] = m[2][3];
    return true;
}

static void ntcCalTask(void* param) {
    NtcCalTaskArgs args{};
    if (param) {
        args = *static_cast<NtcCalTaskArgs*>(param);
        delete static_cast<NtcCalTaskArgs*>(param);
    }

    const uint32_t startMs = args.startMs ? args.startMs : millis();
    uint32_t lastUpdateMs = startMs;

    double s00 = 0.0;
    double s01 = 0.0;
    double s02 = 0.0;
    double s11 = 0.0;
    double s12 = 0.0;
    double s22 = 0.0;
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    uint32_t samples = 0;

    bool failed = false;
    const char* failReason = nullptr;
    bool heating = true;
    float baseTempC = NAN;

    while (true) {
        const uint32_t nowMs = millis();
        const uint32_t elapsedMs = (nowMs >= startMs) ? (nowMs - startMs) : 0;

        if (ntcCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }

        if (elapsedMs >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        if (!DEVICE || !DEVICE->tempSensor || !NTC) {
            failed = true;
            failReason = ERR_SENSOR_MISSING;
            break;
        }

        Device::WireTargetStatus run{};
        if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(run)) {
            failed = true;
            failReason = ERR_STATUS_UNAVAILABLE;
            break;
        }
        if (heating &&
            (!run.active || run.purpose != Device::EnergyRunPurpose::NtcCal))
        {
            failed = true;
            failReason = ERR_ENERGY_STOPPED;
            break;
        }

        const float hsC = DEVICE->tempSensor->getHeatsinkTemp();
        NTC->update();
        NtcSensor::Sample s = NTC->getLastSample();

        if (!isfinite(baseTempC) && isfinite(hsC)) {
            baseTempC = hsC;
        }

        bool sampleOk = false;
        if (isfinite(hsC) && isfinite(s.rNtcOhm) && s.rNtcOhm > 0.0f && !s.pressed) {
            const double tK = static_cast<double>(hsC) + 273.15;
            if (tK > 0.0) {
                const double lnR = log(static_cast<double>(s.rNtcOhm));
                if (isfinite(lnR)) {
                    const double lnR2 = lnR * lnR;
                    const double lnR3 = lnR2 * lnR;
                    const double lnR4 = lnR2 * lnR2;
                    const double lnR6 = lnR3 * lnR3;
                    const double invT = 1.0 / tK;

                    s00 += 1.0;
                    s01 += lnR;
                    s02 += lnR3;
                    s11 += lnR2;
                    s12 += lnR4;
                    s22 += lnR6;
                    b0 += invT;
                    b1 += invT * lnR;
                    b2 += invT * lnR3;
                    samples++;
                    sampleOk = true;
                }
            }
        }

        if (sampleOk || (nowMs - lastUpdateMs) >= args.sampleMs) {
            ntcCalUpdateProgress(hsC, s.rNtcOhm, samples, elapsedMs);
            lastUpdateMs = nowMs;
        }

        if (heating && isfinite(hsC) && hsC >= args.targetC) {
            heating = false;
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
        } else if (!heating && isfinite(hsC)) {
            float coolTargetC = isfinite(baseTempC) ? (baseTempC + 2.0f) : (args.targetC - 10.0f);
            if (hsC <= coolTargetC) {
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(args.sampleMs));
    }

    if (DEVTRAN) {
        DEVTRAN->stopWireTargetTest();
    }

    const uint32_t endMs = millis();
    const uint32_t elapsedMs = (endMs >= startMs) ? (endMs - startMs) : 0;

    if (!failed && samples < kNtcCalMinSamples) {
        failed = true;
        failReason = ERR_NOT_ENOUGH_SAMPLES;
    }

    if (failed) {
        ntcCalSetError(failReason ? failReason : ERR_FAILED, elapsedMs);
    } else {
        const double mat[3][3] = {
            {s00, s01, s02},
            {s01, s11, s12},
            {s02, s12, s22}
        };
        const double vec[3] = {b0, b1, b2};
        double out[3] = {0.0, 0.0, 0.0};
        bool ok = solve3x3(mat, vec, out);
        float a = static_cast<float>(out[0]);
        float b = static_cast<float>(out[1]);
        float c = static_cast<float>(out[2]);

        if (!ok || !isfinite(a) || !isfinite(b) || !isfinite(c)) {
            ntcCalSetError(ERR_FIT_FAILED, elapsedMs);
        } else if (!NTC || !NTC->setSteinhartCoefficients(a, b, c, true)) {
            ntcCalSetError(ERR_PERSIST_FAILED, elapsedMs);
        } else {
            NTC->setModel(NtcSensor::Model::Steinhart, true);
            ntcCalFinish(a, b, c, samples, elapsedMs);
        }
    }

    if (ntcCalLock(pdMS_TO_TICKS(50))) {
        s_ntcCalTask = nullptr;
        ntcCalUnlock();
    } else {
        s_ntcCalTask = nullptr;
    }

    vTaskDelete(nullptr);
}

static void modelCalTask(void* param) {
    ModelCalTaskArgs args{};
    if (param) {
        args = *static_cast<ModelCalTaskArgs*>(param);
        delete static_cast<ModelCalTaskArgs*>(param);
    }

    const uint32_t startMs = args.startMs ? args.startMs : millis();
    bool failed = false;
    const char* failReason = nullptr;
    bool heating = true;
    float baseTempC = NAN;

    while (true) {
        const uint32_t nowMs = millis();
        const uint32_t elapsedMs = (nowMs >= startMs) ? (nowMs - startMs) : 0;

        if (modelCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }

        if (elapsedMs >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        if (!DEVICE || !DEVTRAN || !NTC) {
            failed = true;
            failReason = ERR_DEVICE_MISSING;
            break;
        }

        Device::WireTargetStatus st{};
        const bool statusOk = DEVTRAN->getWireTargetStatus(st);
        const bool statusActive =
            statusOk && st.active && st.purpose == Device::EnergyRunPurpose::ModelCal;

        NTC->update();
        const float ntcTemp = NTC->getLastTempC();
        const float modelTemp = statusOk ? st.activeTempC : NAN;
        const float tempNow = isfinite(ntcTemp) ? ntcTemp : modelTemp;

        if (!isfinite(baseTempC) && isfinite(tempNow)) {
            baseTempC = tempNow;
        }

        if (heating && isfinite(tempNow) && isfinite(args.targetC) &&
            tempNow >= args.targetC) {
            heating = false;
            if (statusActive) {
                DEVTRAN->stopWireTargetTest();
            }
        }

        if (!statusActive) {
            if (heating) {
                failed = true;
                failReason = ERR_ENERGY_STOPPED;
                break;
            }
            if (isfinite(tempNow) && isfinite(baseTempC) &&
                tempNow <= (baseTempC + 2.0f)) {
                break;
            }
            if (!isfinite(tempNow)) {
                failed = true;
                failReason = ERR_SENSOR_MISSING;
                break;
            }
        } else if (!heating) {
            if (isfinite(tempNow) && isfinite(baseTempC) &&
                tempNow <= (baseTempC + 2.0f)) {
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kModelCalPollMs));
    }

    if (DEVTRAN) {
        DEVTRAN->stopWireTargetTest();
    }

    if (CALIB) {
        if (failed) {
            CALIB->stop();
        } else {
            CALIB->stopAndSave(5000);
        }
    }

    s_modelCalTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

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

String WiFiManager::issueSessionToken_(const IPAddress& ip) {
    char buf[25];
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const uint32_t r3 = esp_random();
    snprintf(buf, sizeof(buf), "%08lx%08lx%08lx",
             static_cast<unsigned long>(r1),
             static_cast<unsigned long>(r2),
             static_cast<unsigned long>(r3));

    if (lock()) {
        _sessionToken = buf;
        _sessionIp = ip;
        unlock();
    } else {
        _sessionToken = buf;
        _sessionIp = ip;
    }

    return _sessionToken;
}

bool WiFiManager::validateSession_(AsyncWebServerRequest* request) const {
    String sessionToken;
    IPAddress sessionIp;
    if (lock()) {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
        unlock();
    } else {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
    }

    if (sessionToken.isEmpty()) return false;

    String token;
    if (request->hasHeader("X-Session-Token")) {
        token = request->getHeader("X-Session-Token")->value();
    }
    if (token.isEmpty() && request->hasParam("token")) {
        token = request->getParam("token")->value();
    }
    if (token.isEmpty() || token != sessionToken) return false;

    if (sessionIp != IPAddress(0, 0, 0, 0)) {
        IPAddress ip =
            request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (ip != sessionIp) return false;
    }

    return true;
}

bool WiFiManager::sessionIpMatches_(const IPAddress& ip) const {
    String sessionToken;
    IPAddress sessionIp;
    if (lock()) {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
        unlock();
    } else {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
    }

    if (sessionToken.isEmpty()) return false;
    if (sessionIp == IPAddress(0, 0, 0, 0)) return true;
    return ip == sessionIp;
}

void WiFiManager::clearSession_() {
    if (lock()) {
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
        unlock();
    } else {
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
    }
}

const char* WiFiManager::stateName(DeviceState s) {
    switch (s) {
        case DeviceState::Idle:     return STATE_IDLE;
        case DeviceState::Running:  return STATE_RUNNING;
        case DeviceState::Error:    return STATE_ERROR;
        case DeviceState::Shutdown: return STATE_SHUTDOWN;
        default:                    return STATE_UNKNOWN;
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
    startEventStreamTask(); // SSE push for warnings/errors
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

// ======================= Route registration =======================

void WiFiManager::registerRoutes_() {
    static bool corsReady = false;
    if (!corsReady) {
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader(
            "Access-Control-Allow-Methods",
            "GET, POST, OPTIONS"
        );
        DefaultHeaders::Instance().addHeader(
            "Access-Control-Allow-Headers",
            "Content-Type, X-Session-Token"
        );
        DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "600");
        DefaultHeaders::Instance().addHeader(
            "Access-Control-Allow-Private-Network",
            "true"
        );
        server.onNotFound([](AsyncWebServerRequest* request) {
            if (request->method() == HTTP_OPTIONS) {
                request->send(204);
                return;
            }
            request->send(404, CT_APP_JSON, RESP_ERR_NOT_FOUND);
        });
        corsReady = true;
    }
    // ---- State stream (SSE) ----
    server.addHandler(&stateSse);
    // ---- Event stream (SSE) ----
    server.addHandler(&eventSse);
    // ---- Live monitor stream (SSE) ----
    server.addHandler(&liveSse);
    // ---- Live monitor sinceSeq (HTTP) ----
    server.on(EP_MONITOR_SINCE, HTTP_GET,
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
            request->send(200, CT_APP_JSON, json);
        }
    );
    // ---- Live monitor stream (SSE) ----
    server.addHandler(&liveSse);

    // ---- Login page ----
    server.on(EP_LOGIN, HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        handleRoot(request);
    });

    // ---- Device info for login ----
    server.on(EP_DEVICE_INFO, HTTP_GET,
        [](AsyncWebServerRequest* request) {
            StaticJsonDocument<256> doc;
            doc["deviceId"] = CONF->GetString(DEV_ID_KEY, "");
            doc["sw"]       = CONF->GetString(DEV_SW_KEY, DEVICE_SW_VERSION);
            doc["hw"]       = CONF->GetString(DEV_HW_KEY, DEVICE_HW_VERSION);
            String json;
            serializeJson(doc, json);
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Heartbeat ----
    server.on(EP_HEARTBEAT, HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isAuthenticated(request)) {
            BUZZ->bipFault();
            return;
        }
        if (lock()) {
            lastActivityMillis = millis();
            keepAlive = true;
            unlock();
        }
        request->send(200, CT_TEXT_PLAIN, RESP_ALIVE);
    });

    // ---- Login connect ----
    server.on(EP_CONNECT, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            const String username = doc["username"] | "";
            const String password = doc["password"] | "";
            if (username.isEmpty() || password.isEmpty()) {
                request->send(400, CT_APP_JSON,
                              RESP_ERR_MISSING_FIELDS);
                return;
            }

            if (wifiStatus != WiFiStatus::NotConnected) {
                request->send(403, CT_APP_JSON,
                              RESP_ERR_ALREADY_CONNECTED);
                return;
            }

            String adminUser = CONF->GetString(ADMIN_ID_KEY, "");
            String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
            String userUser  = CONF->GetString(USER_ID_KEY, "");
            String userPass  = CONF->GetString(USER_PASS_KEY, "");

            if (username == adminUser && password == adminPass) {
                BUZZ->successSound();
                const String token =
                    issueSessionToken_(request->client()->remoteIP());
                onAdminConnected();
                RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);

                StaticJsonDocument<128> resp;
                resp["ok"] = true;
                resp["role"] = "admin";
                resp["token"] = token;
                String json;
                serializeJson(resp, json);
                request->send(200, CT_APP_JSON, json);
                return;
            }
            if (username == userUser && password == userPass) {
                BUZZ->successSound();
                const String token =
                    issueSessionToken_(request->client()->remoteIP());
                onUserConnected();
                RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);

                StaticJsonDocument<128> resp;
                resp["ok"] = true;
                resp["role"] = "user";
                resp["token"] = token;
                String json;
                serializeJson(resp, json);
                request->send(200, CT_APP_JSON, json);
                return;
            }

            BUZZ->bipFault();
            request->send(401, CT_APP_JSON, RESP_ERR_BAD_PASSWORD);
        }
    );

    // ---- Session history (JSON) ----
    server.on(EP_SESSION_HISTORY, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(POWERTRACKER_HISTORY_FILE)) {
                request->send(SPIFFS,
                              POWERTRACKER_HISTORY_FILE,
                              CT_APP_JSON);
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
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Device log ----
    server.on(EP_DEVICE_LOG, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            AsyncResponseStream* response =
                request->beginResponseStream(CT_TEXT_PLAIN);
            Debug::writeMemoryLog(*response);
            request->send(response);
        }
    );

    server.on(EP_DEVICE_LOG_CLEAR, HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Debug::clearMemoryLog();
            request->send(200, CT_APP_JSON, RESP_OK_TRUE);
        }
    );

    // ---- Access Point settings ----
    server.on(EP_AP_CONFIG, HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (!isAdminConnected()) {
                request->send(403, CT_APP_JSON,
                              RESP_ERR_NOT_AUTHENTICATED);
                return;
            }
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            static String body;
            if (index == 0) body = "";
            body += String((char*)data, len);
            if (index + len != total) return;

            if (!isAuthenticated(request)) {
                body = "";
                return;
            }

            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, body)) {
                body = "";
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            const String newSsid = doc["apSSID"] | "";
            const String newPass = doc["apPassword"] | "";

            bool changed = false;
            if (newSsid.length()) {
                const String current =
                    CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY,
                                    DEVICE_WIFI_HOTSPOT_NAME);
                if (newSsid != current) {
                    CONF->PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY, newSsid);
                    changed = true;
                }
            }
            if (newPass.length()) {
                const String current =
                    CONF->GetString(DEVICE_AP_AUTH_PASS_KEY,
                                    DEVICE_AP_AUTH_PASS_DEFAULT);
                if (newPass != current) {
                    CONF->PutString(DEVICE_AP_AUTH_PASS_KEY, newPass);
                    changed = true;
                }
            }

            request->send(200, CT_APP_JSON,
                          RESP_STATUS_OK_APPLIED);

            if (changed) {
                CONF->RestartSysDelayDown(3000);
            }
        }
    );

    // ---- Calibration recorder status ----
    server.on(EP_CALIB_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const CalibrationRecorder::Meta meta = CALIB->getMeta();
            const char* modeStr =
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                MODE_NONE;

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
            String json;
            serializeJson(doc, json);
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Calibration recorder start ----
    server.on(EP_CALIB_START, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            auto sendCalibError = [&](int status,
                                      const char* error,
                                      const String& detail,
                                      const char* state) {
                StaticJsonDocument<192> err;
                err["error"] = error;
                if (detail.length() > 0) err["detail"] = detail;
                if (state) err[SSE_EVENT_STATE] = state;
                String json;
                serializeJson(err, json);
                request->send(status, CT_APP_JSON, json);
            };

            String modeStr = doc["mode"] | "";
            modeStr.toLowerCase();
            CalibrationRecorder::Mode mode = CalibrationRecorder::Mode::None;
            if (modeStr == MODE_NTC) mode = CalibrationRecorder::Mode::Ntc;
            else if (modeStr == MODE_MODEL) mode = CalibrationRecorder::Mode::Model;

            if (mode == CalibrationRecorder::Mode::None) {
                sendCalibError(400, ERR_INVALID_MODE, "", nullptr);
                return;
            }
            if (!BUS_SAMPLER) {
                sendCalibError(503, ERR_BUS_SAMPLER_MISSING, "", nullptr);
                return;
            }
            if (CALIB && CALIB->isRunning()) {
                sendCalibError(409, ERR_ALREADY_RUNNING, "", nullptr);
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
            const uint8_t ntcGate = getNtcGateIndexFromConfig();
            uint8_t wireIndex = doc["wire_index"] | ntcGate;
            if (mode == CalibrationRecorder::Mode::Model ||
                mode == CalibrationRecorder::Mode::Ntc) {
                wireIndex = ntcGate;
            }

            const bool ok = CALIB->start(mode, intervalMs, maxSamples, targetC, wireIndex);
            if (!ok) {
                sendCalibError(500, ERR_START_FAILED, "", nullptr);
                return;
            }

            if (mode == CalibrationRecorder::Mode::Model) {
                float runTargetC = targetC;
                if (!isfinite(runTargetC) || runTargetC <= 0.0f) {
                    float fallback = 150.0f;
                    if (CONF) {
                        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                                 DEFAULT_NICHROME_FINAL_TEMP_C);
                        if (isfinite(v) && v > 0.0f) fallback = v;
                    }
                    runTargetC = fallback;
                }

                if (!DEVTRAN) {
                    CALIB->stop();
                    sendCalibError(503, ERR_DEVICE_TRANSPORT_MISSING, "", nullptr);
                    return;
                }
                DeviceState lastState = DeviceState::Shutdown;
                if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                    CALIB->stop();
                    String detail = (lastState == DeviceState::Shutdown) ? "wake_timeout" : "";
                    sendCalibError(409, ERR_DEVICE_NOT_IDLE, detail, stateName(lastState));
                    return;
                }
                if (!WIRE) {
                    CALIB->stop();
                    sendCalibError(503, ERR_WIRE_SUBSYSTEM_MISSING, "", nullptr);
                    return;
                }
                if (CONF && DEVICE) {
                    if (!DEVICE->getWireConfigStore().getAccessFlag(wireIndex)) {
                        CALIB->stop();
                        sendCalibError(403, ERR_WIRE_ACCESS_BLOCKED,
                                       String("wire=") + String(wireIndex), nullptr);
                        return;
                    }
                }
                auto wi = WIRE->getWireInfo(wireIndex);
                if (!wi.connected) {
                    CALIB->stop();
                    sendCalibError(400, ERR_WIRE_NOT_CONNECTED,
                                   String("wire=") + String(wireIndex), nullptr);
                    return;
                }
                if (!DEVTRAN->startEnergyCalibration(runTargetC,
                                                     wireIndex,
                                                     Device::EnergyRunPurpose::ModelCal)) {
                    CALIB->stop();
                    sendCalibError(500, ERR_ENERGY_START_FAILED, "", nullptr);
                    return;
                }
                if (s_modelCalTask != nullptr) {
                    DEVTRAN->stopWireTargetTest();
                    CALIB->stop();
                    sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                    return;
                }
                s_modelCalAbort = false;
                ModelCalTaskArgs* args = new ModelCalTaskArgs();
                if (!args) {
                    DEVTRAN->stopWireTargetTest();
                    CALIB->stop();
                    sendCalibError(500, ERR_ALLOC_FAILED, "", nullptr);
                    return;
                }
                args->targetC = runTargetC;
                args->wireIndex = wireIndex;
                args->timeoutMs = kModelCalTimeoutMs;
                args->startMs = millis();

                BaseType_t okTask = xTaskCreate(
                    modelCalTask,
                    "ModelCal",
                    4096,
                    args,
                    2,
                    &s_modelCalTask
                );
                if (okTask != pdPASS) {
                    delete args;
                    s_modelCalTask = nullptr;
                    DEVTRAN->stopWireTargetTest();
                    CALIB->stop();
                    sendCalibError(500, ERR_TASK_FAILED, "", nullptr);
                    return;
                }
            }

            request->send(200, CT_APP_JSON,
                          RESP_STATUS_OK_RUNNING_TRUE);
        }
    );

    // ---- Calibration recorder stop ----
    server.on(EP_CALIB_STOP, HTTP_POST,
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
            modelCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            String json = String(RESP_STATUS_OK_RUNNING_FALSE_SAVED_PREFIX) +
                          (saved ? JSON_TRUE : JSON_FALSE) + RESP_STATE_JSON_SUFFIX;
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Calibration recorder clear ----
    server.on(EP_CALIB_CLEAR, HTTP_POST,
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
            modelCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
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

            String json = String(RESP_STATUS_OK_CLEARED_FILE_PREFIX) +
                          (removed ? JSON_TRUE : JSON_FALSE) +
                          RESP_HISTORY_REMOVED_PREFIX + String(removedCount) +
                          RESP_STATE_JSON_SUFFIX;
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Calibration recorder data (paged) ----
    server.on(EP_CALIB_DATA, HTTP_GET,
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
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                MODE_NONE;
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
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Calibration recorder file (json) ----
    server.on(EP_CALIB_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(CALIB_MODEL_JSON_FILE)) {
                request->send(SPIFFS, CALIB_MODEL_JSON_FILE, CT_APP_JSON);
            } else {
                request->send(404, CT_APP_JSON, RESP_ERR_NOT_FOUND);
            }
        }
    );

    // ---- Calibration history list (json) ----
    server.on(EP_CALIB_HISTORY_LIST, HTTP_GET,
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
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Calibration history file (json) ----
    server.on(EP_CALIB_HISTORY_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!request->hasParam("name")) {
                request->send(400, CT_APP_JSON, RESP_ERR_MISSING_NAME);
                return;
            }
            String name = request->getParam("name")->value();
            String fullName;
            String baseName;
            if (!normalizeHistoryPath(name, fullName, baseName, nullptr)) {
                request->send(400, CT_APP_JSON, RESP_ERR_INVALID_NAME);
                return;
            }
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(fullName)) {
                    request->send(SPIFFS, fullName, CT_APP_JSON);
                    return;
                }
                String legacyPath = "/" + baseName;
                if (legacyPath != fullName && SPIFFS.exists(legacyPath)) {
                    request->send(SPIFFS, legacyPath, CT_APP_JSON);
                    return;
                }
            }
            request->send(404, CT_APP_JSON, RESP_ERR_NOT_FOUND);
        }
    );

    // ---- Calibration model suggestions (compute) ----
    server.on(EP_CALIB_PI_SUGGEST, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            ThermalEstimator::Result r = THERMAL_EST->computeSuggestions(CALIB);
            StaticJsonDocument<512> doc;
            doc["wire_tau"]    = r.tauSec;
            doc["wire_k_loss"] = r.kLoss;
            doc["wire_c"]      = r.thermalC;
            doc["max_power_w"] = r.maxPowerW;

            String json;
            serializeJson(doc, json);
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Persist thermal model params ----
    server.on(EP_CALIB_PI_SAVE, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            ThermalEstimator::Result r{};
            if (doc.containsKey("wire_tau"))    r.tauSec   = doc["wire_tau"].as<float>();
            if (doc.containsKey("wire_k_loss")) r.kLoss    = doc["wire_k_loss"].as<float>();
            if (doc.containsKey("wire_c"))      r.thermalC = doc["wire_c"].as<float>();

            THERMAL_EST->persist(r);

            request->send(200, CT_APP_JSON,
                          RESP_STATUS_OK_APPLIED);
        }
    );

    // ---- Wire target test status ----
    server.on(EP_WIRE_TEST_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Device::WireTargetStatus st{};
            if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(st)) {
                request->send(503, CT_APP_JSON,
                              RESP_ERR_STATUS_UNAVAILABLE);
                return;
            }

            StaticJsonDocument<256> doc;
            doc["running"] = st.active;
            if (isfinite(st.targetC)) doc["target_c"] = st.targetC;
            if (st.activeWire > 0) doc["active_wire"] = st.activeWire;
            if (isfinite(st.ntcTempC)) doc["ntc_temp_c"] = st.ntcTempC;
            if (isfinite(st.activeTempC)) doc["active_temp_c"] = st.activeTempC;
            doc["packet_ms"] = st.packetMs;
            doc["frame_ms"] = st.frameMs;
            doc["updated_ms"] = st.updatedMs;
            doc["mode"] = MODE_ENERGY;
            const char* purpose = PURPOSE_NONE;
            switch (st.purpose) {
                case Device::EnergyRunPurpose::WireTest: purpose = PURPOSE_WIRE_TEST; break;
                case Device::EnergyRunPurpose::ModelCal: purpose = PURPOSE_MODEL_CAL; break;
                case Device::EnergyRunPurpose::NtcCal:   purpose = PURPOSE_NTC_CAL; break;
                default: break;
            }
            doc["purpose"] = purpose;

            String json;
            serializeJson(doc, json);
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Wire target test start ----
    server.on(EP_WIRE_TEST_START, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            float targetC = doc["target_c"].as<float>();
            if (!isfinite(targetC) || targetC <= 0.0f) {
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_TARGET);
                return;
            }

            if (!DEVTRAN) {
                request->send(503, CT_APP_JSON,
                              RESP_ERR_DEVICE_MISSING);
                return;
            }
            const uint8_t wireIndex = getNtcGateIndexFromConfig();
            DeviceState lastState = DeviceState::Shutdown;
            if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                request->send(409, CT_APP_JSON,
                              RESP_ERR_DEVICE_NOT_IDLE);
                return;
            }
            if (!DEVTRAN->startWireTargetTest(targetC, wireIndex)) {
                request->send(400, CT_APP_JSON,
                              RESP_ERR_START_FAILED);
                return;
            }

            request->send(200, CT_APP_JSON,
                          RESP_STATUS_OK_RUNNING_TRUE);
        }
    );

    // ---- Wire target test stop ----
    server.on(EP_WIRE_TEST_STOP, HTTP_POST,
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
            request->send(200, CT_APP_JSON,
                          RESP_STATUS_OK_RUNNING_FALSE);
        }
    );

    server.on(EP_HISTORY_JSON, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (SPIFFS.begin(false) && SPIFFS.exists(POWERTRACKER_HISTORY_FILE)) {
                request->send(SPIFFS,
                              POWERTRACKER_HISTORY_FILE,
                              CT_APP_JSON);
            } else {
                request->send(200, CT_APP_JSON, RESP_HISTORY_EMPTY);
            }
        }
    );

    // ---- Disconnect ----
    server.on(EP_DISCONNECT, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
                return;
            }
            body = "";

            if ((String)(doc["action"] | "") != "disconnect") {
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_ACTION);
                return;
            }

            onDisconnected();
            if (lock()) {
                lastActivityMillis = millis();
                keepAlive = false;
                unlock();
            }
            RGB->postOverlay(OverlayEvent::WIFI_LOST);
            request->send(200, CT_APP_JSON, RESP_OK_TRUE);
        }
    );

    // ---- Monitor (uses snapshot) ----
    server.on(EP_MONITOR, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); keepAlive = true; unlock(); }

            String json;
            if (!getMonitorJson(json)) {
                request->send(503, CT_APP_JSON,
                              RESP_ERR_SNAPSHOT_BUSY);
                return;
            }
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Last stop/error + recent events ----
    server.on(EP_LAST_EVENT, HTTP_GET,
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
            doc[SSE_EVENT_STATE] = stateName(snap.state);

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
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- CONTROL (queued) ----
    server.on(EP_CONTROL, HTTP_POST,
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
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_JSON);
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
                else if (target == "relay")                   { c.type = CTRL_RELAY_BOOL;        c.b1 = value.as<bool>(); }
                else if (target.startsWith("output"))         { c.type = CTRL_OUTPUT_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "acFrequency")             { c.type = CTRL_AC_FREQ;           c.i1 = value.as<int>(); }
                else if (target == "chargeResistor")          { c.type = CTRL_CHARGE_RES;        c.f1 = value.as<float>(); }
                else if (target.startsWith("Access"))         { c.type = CTRL_ACCESS_BOOL;       c.i1 = target.substring(6).toInt(); c.b1 = value.as<bool>(); }
                else if (target == "mode")                    { c.type = CTRL_SET_MODE;          c.b1 = value.as<bool>(); }
                else if (target == "systemStart")             c.type = CTRL_SYSTEM_START;
                else if (target == "systemWake")              c.type = CTRL_SYSTEM_WAKE;
                else if (target == "systemShutdown")          c.type = CTRL_SYSTEM_SHUTDOWN;
                else if (target == "fanSpeed")                { c.type = CTRL_FAN_SPEED;         c.i1 = constrain(value.as<int>(), 0, 100); }
                else if (target == "buzzerMute")              { c.type = CTRL_BUZZER_MUTE;       c.b1 = value.as<bool>(); }
                else if (target.startsWith("wireRes"))        { c.type = CTRL_WIRE_RES;          c.i1 = target.substring(7).toInt(); c.f1 = value.as<float>(); }
                else if (target == "wireOhmPerM")             { c.type = CTRL_WIRE_OHM_PER_M;    c.f1 = value.as<float>(); }
                else if (target == "wireGauge")               { c.type = CTRL_WIRE_GAUGE;        c.i1 = value.as<int>(); }
                else if (target == "currLimit")               { c.type = CTRL_CURR_LIMIT;        c.f1 = value.as<float>(); }
                else if (target == "adminCredentials") {
                    const String current = value["current"] | "";
                    const String newUser = value["username"] | "";
                    const String newPass = value["password"] | "";
                    const String newSsid = value["wifiSSID"] | "";
                    const String newWifiPass = value["wifiPassword"] | "";

                    const String storedUser = CONF->GetString(ADMIN_ID_KEY, DEFAULT_ADMIN_ID);
                    const String storedPass = CONF->GetString(ADMIN_PASS_KEY, DEFAULT_ADMIN_PASS);
                    const String storedSsid = CONF->GetString(STA_SSID_KEY, DEFAULT_STA_SSID);
                    const String storedWifiPass = CONF->GetString(STA_PASS_KEY, DEFAULT_STA_PASS);
                    if (current.length() && current != storedPass) {
                        request->send(403, CT_APP_JSON,
                                      RESP_ERR_BAD_PASSWORD);
                        return;
                    }

                    bool sessionChanged = false;
                    bool wifiChanged = false;

                    if (newUser.length() && newUser != storedUser) {
                        CONF->PutString(ADMIN_ID_KEY, newUser);
                        sessionChanged = true;
                    }
                    if (newPass.length() && newPass != storedPass) {
                        CONF->PutString(ADMIN_PASS_KEY, newPass);
                        sessionChanged = true;
                    }
                    if (newSsid.length() && newSsid != storedSsid) {
                        CONF->PutString(STA_SSID_KEY, newSsid);
                        wifiChanged = true;
                    }
                    if (newWifiPass.length() && newWifiPass != storedWifiPass) {
                        CONF->PutString(STA_PASS_KEY, newWifiPass);
                        wifiChanged = true;
                    }

                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    if (sessionChanged) {
                        onDisconnected();
                    }
                    if (wifiChanged) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "userCredentials") {
                    const String current = value["current"] | "";
                    const String newPass = value["newPass"] | "";
                    const String newId   = value["newId"]   | "";
                    const String storedPass = CONF->GetString(USER_PASS_KEY, DEFAULT_USER_PASS);
                    if (current.length() && current != storedPass) {
                        request->send(403, CT_APP_JSON,
                                      RESP_ERR_BAD_PASSWORD);
                        return;
                    }
                    bool sessionChanged = false;
                    const String storedId = CONF->GetString(USER_ID_KEY, DEFAULT_USER_ID);
                    if (newId.length() && newId != storedId) {
                        CONF->PutString(USER_ID_KEY, newId);
                        sessionChanged = true;
                    }
                    if (newPass.length() && newPass != storedPass) {
                        CONF->PutString(USER_PASS_KEY, newPass);
                        sessionChanged = true;
                    }
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    if (sessionChanged) {
                        onDisconnected();
                    }
                    return;
                }
                else if (target == "wifiSSID") {
                    const String ssid = value.as<String>();
                    bool changed = false;
                    if (ssid.length()) {
                        const String stored = CONF->GetString(STA_SSID_KEY, DEFAULT_STA_SSID);
                        if (ssid != stored) {
                            CONF->PutString(STA_SSID_KEY, ssid);
                            changed = true;
                        }
                    }
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "wifiPassword") {
                    const String pw = value.as<String>();
                    bool changed = false;
                    if (pw.length()) {
                        const String stored = CONF->GetString(STA_PASS_KEY, DEFAULT_STA_PASS);
                        if (pw != stored) {
                            CONF->PutString(STA_PASS_KEY, pw);
                            changed = true;
                        }
                    }
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "tempWarnC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = 0.0f;
                    CONF->PutFloat(TEMP_WARN_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "tempTripC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_TEMP_THRESHOLD;
                    CONF->PutFloat(TEMP_THRESHOLD_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "idleCurrentA") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = 0.0f;
                    CONF->PutFloat(IDLE_CURR_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "wireTauSec") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v < 0.05) v = DEFAULT_WIRE_TAU_SEC;
                    if (v > 600.0) v = 600.0;
                    CONF->PutDouble(WIRE_TAU_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "wireKLoss") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_K_LOSS;
                    CONF->PutDouble(WIRE_K_LOSS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "wireThermalC") {
                    double v = value.as<double>();
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_THERMAL_C;
                    CONF->PutDouble(WIRE_C_TH_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
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
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
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
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "floorMaxC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_FLOOR_MAX_C;
                    if (v > DEFAULT_FLOOR_MAX_C) v = DEFAULT_FLOOR_MAX_C;
                    CONF->PutFloat(FLOOR_MAX_C_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "nichromeFinalTempC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_NICHROME_FINAL_TEMP_C;
                    CONF->PutFloat(NICHROME_FINAL_TEMP_C_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "ntcGateIndex") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > HeaterManager::kWireCount) v = HeaterManager::kWireCount;
                    CONF->PutInt(NTC_GATE_INDEX_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "timingMode") {
                    String modeStr = value.as<String>();
                    modeStr.toLowerCase();
                    int mode = (modeStr == "manual" || value.as<int>() == 1) ? 1 : 0;
                    CONF->PutInt(TIMING_MODE_KEY, mode);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
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
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixFrameMs") {
                    int v = value.as<int>();
                    if (v < 10) v = 10;
                    if (v > 300) v = 300;
                    CONF->PutInt(MIX_FRAME_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixRefOnMs") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > 200) v = 200;
                    CONF->PutInt(MIX_REF_ON_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixRefResOhm") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_MIX_REF_RES_OHM;
                    CONF->PutFloat(MIX_REF_RES_OHM_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixBoostK") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_MIX_BOOST_K;
                    if (v > 5.0f) v = 5.0f;
                    CONF->PutFloat(MIX_BOOST_K_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixBoostMs") {
                    int v = value.as<int>();
                    if (v < 0) v = 0;
                    if (v > 600000) v = 600000;
                    CONF->PutInt(MIX_BOOST_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixPreDeltaC") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_MIX_PRE_DELTA_C;
                    if (v > 30.0f) v = 30.0f;
                    CONF->PutFloat(MIX_PRE_DELTA_C_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixHoldUpdateMs") {
                    int v = value.as<int>();
                    if (v < 200) v = 200;
                    if (v > 5000) v = 5000;
                    CONF->PutInt(MIX_HOLD_UPDATE_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixHoldGain") {
                    float v = value.as<float>();
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_MIX_HOLD_GAIN;
                    if (v > 5.0f) v = 5.0f;
                    CONF->PutFloat(MIX_HOLD_GAIN_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixMinOnMs") {
                    int v = value.as<int>();
                    if (v < 0) v = 0;
                    if (v > 200) v = 200;
                    CONF->PutInt(MIX_MIN_ON_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixMaxOnMs") {
                    int v = value.as<int>();
                    if (v < 1) v = 1;
                    if (v > 1000) v = 1000;
                    CONF->PutInt(MIX_MAX_ON_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "mixMaxAvgMs") {
                    int v = value.as<int>();
                    if (v < 0) v = 0;
                    if (v > 1000) v = 1000;
                    CONF->PutInt(MIX_MAX_AVG_MS_KEY, v);
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_APPLIED);
                    return;
                }
                else if (target == "calibrate")              { c.type = CTRL_CALIBRATE; }
                else {
                    request->send(400, CT_APP_JSON,
                                  RESP_ERR_UNKNOWN_TARGET);
                    return;
                }

                const bool ok = sendCmd(c);
                if (ok) {
                    request->send(200, CT_APP_JSON,
                                  RESP_STATUS_OK_QUEUED);
                } else {
                    request->send(503, CT_APP_JSON,
                                  RESP_ERR_CTRL_QUEUE_FULL);
                }
            } else if (action == "get" && target == "status") {
                const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
                const String statusStr = stateName(snap.state);
                request->send(200, CT_APP_JSON,
                              String(RESP_STATE_JSON_PREFIX) + statusStr +
                              RESP_STATE_JSON_SUFFIX);
            } else {
                request->send(400, CT_APP_JSON,
                              RESP_ERR_INVALID_ACTION_TARGET);
            }
        }
    );

    // ---- load_controls (uses snapshot + config) ----
    server.on(EP_LOAD_CONTROLS, HTTP_GET,
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
                request->send(503, CT_APP_JSON,
                              RESP_ERR_SNAPSHOT_BUSY);
                return;
            }

            StaticJsonDocument<2048> doc;
            const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();

            // Preferences (config only)
            doc["ledFeedback"]    = CONF->GetBool(LED_FEEDBACK_KEY, false);
            doc["acFrequency"]    = CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
            doc["chargeResistor"] = CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f);
            doc["deviceId"]       = CONF->GetString(DEV_ID_KEY, "");
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
            doc["floorThicknessMm"] = CONF->GetFloat(FLOOR_THICKNESS_MM_KEY, DEFAULT_FLOOR_THICKNESS_MM);
            const int floorMatCode = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
            doc["floorMaterial"]    = floorMaterialToString(floorMatCode);
            doc["floorMaterialCode"] = floorMatCode;
            doc["floorMaxC"]         = CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C);
            doc["nichromeFinalTempC"] = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY, DEFAULT_NICHROME_FINAL_TEMP_C);
            doc["ntcGateIndex"]     = getNtcGateIndexFromConfig();
            doc["mixFrameMs"]      = CONF->GetInt(MIX_FRAME_MS_KEY, DEFAULT_MIX_FRAME_MS);
            doc["mixRefOnMs"]      = CONF->GetInt(MIX_REF_ON_MS_KEY, DEFAULT_MIX_REF_ON_MS);
            doc["mixRefResOhm"]    = CONF->GetFloat(MIX_REF_RES_OHM_KEY, DEFAULT_MIX_REF_RES_OHM);
            doc["mixBoostK"]       = CONF->GetFloat(MIX_BOOST_K_KEY, DEFAULT_MIX_BOOST_K);
            doc["mixBoostMs"]      = CONF->GetInt(MIX_BOOST_MS_KEY, DEFAULT_MIX_BOOST_MS);
            doc["mixPreDeltaC"]    = CONF->GetFloat(MIX_PRE_DELTA_C_KEY, DEFAULT_MIX_PRE_DELTA_C);
            doc["mixHoldUpdateMs"] = CONF->GetInt(MIX_HOLD_UPDATE_MS_KEY, DEFAULT_MIX_HOLD_UPDATE_MS);
            doc["mixHoldGain"]     = CONF->GetFloat(MIX_HOLD_GAIN_KEY, DEFAULT_MIX_HOLD_GAIN);
            doc["mixMinOnMs"]      = CONF->GetInt(MIX_MIN_ON_MS_KEY, DEFAULT_MIX_MIN_ON_MS);
            doc["mixMaxOnMs"]      = CONF->GetInt(MIX_MAX_ON_MS_KEY, DEFAULT_MIX_MAX_ON_MS);
            doc["mixMaxAvgMs"]     = CONF->GetInt(MIX_MAX_AVG_MS_KEY, DEFAULT_MIX_MAX_AVG_MS);
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

            String json;
            serializeJson(doc, json);
            request->send(200, CT_APP_JSON, json);
        }
    );

    // ---- Static & misc ----
    server.on(EP_FAVICON, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (lock()) { keepAlive = true; unlock(); }
            request->send(204);
        }
    );

}

// ====================== Common helpers / tasks ======================

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFi] Handling root request");
    if (lock()) { keepAlive = true; unlock(); }
    request->send(404, CT_APP_JSON, RESP_ERR_NOT_FOUND);
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
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
        unlock();
    } else {
        wifiStatus = WiFiStatus::NotConnected;
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
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
        request->send(401, CT_APP_JSON,
                      RESP_ERR_NOT_AUTHENTICATED);
        return false;
    }
    if (!validateSession_(request)) {
        request->send(401, CT_APP_JSON,
                      RESP_ERR_NOT_AUTHENTICATED);
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

                if (self->lock()) {
                    ka = self->keepAlive;
                    last = self->lastActivityMillis;
                    self->unlock();
                } else {
                    ka = self->keepAlive;
                    last = self->lastActivityMillis;
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
                    if (!busy) {
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

        case CTRL_SYSTEM_WAKE:
            BUZZ->bip();
            ok = DEVTRAN->requestWake();
            if (ok) RGB->postOverlay(OverlayEvent::WAKE_FLASH);
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
        if (wifiStatus == WiFiStatus::NotConnected) {
            client->close();
            return;
        }

        IPAddress ip =
            client->client() ? client->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (!sessionIpMatches_(ip)) {
            client->close();
            return;
        }

        Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
        String json = RESP_STATE_JSON_PREFIX;
        json += stateName(snap.state);
        json += RESP_STATE_JSON_MID;
        json += snap.seq;
        json += RESP_STATE_JSON_TAIL;
        json += snap.sinceMs;
        json += RESP_STATE_JSON_SUFFIX;
        client->send(json.c_str(), SSE_EVENT_STATE, snap.seq);
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

        String json = RESP_STATE_JSON_PREFIX;
        json += stateName(snap.state);
        json += RESP_STATE_JSON_MID;
        json += snap.seq;
        json += RESP_STATE_JSON_TAIL;
        json += snap.sinceMs;
        json += RESP_STATE_JSON_SUFFIX;

        self->stateSse.send(json.c_str(), SSE_EVENT_STATE, snap.seq);
    }
}

// ===================== Event streaming (SSE) =====================

void WiFiManager::startEventStreamTask() {
    if (eventStreamTaskHandle) return;

    eventSse.onConnect([this](AsyncEventSourceClient* client) {
        if (wifiStatus == WiFiStatus::NotConnected) {
            client->close();
            return;
        }

        IPAddress ip =
            client->client() ? client->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (!sessionIpMatches_(ip)) {
            client->close();
            return;
        }

        if (!DEVICE) return;

        StaticJsonDocument<384> doc;
        uint8_t warnCount = 0;
        uint8_t errCount = 0;
        DEVICE->getUnreadEventCounts(warnCount, errCount);
        JsonObject unread = doc.createNestedObject("unread");
        unread["warn"] = warnCount;
        unread["error"] = errCount;
        doc["kind"] = "snapshot";

        Device::EventEntry warnEntries[1]{};
        Device::EventEntry errEntries[1]{};
        if (DEVICE->getWarningHistory(warnEntries, 1) > 0) {
            JsonObject warn = doc.createNestedObject("last_warning");
            warn["reason"] = warnEntries[0].reason;
            if (warnEntries[0].ms) warn["ms"] = warnEntries[0].ms;
            if (warnEntries[0].epoch) warn["epoch"] = warnEntries[0].epoch;
        }
        if (DEVICE->getErrorHistory(errEntries, 1) > 0) {
            JsonObject err = doc.createNestedObject("last_error");
            err["reason"] = errEntries[0].reason;
            if (errEntries[0].ms) err["ms"] = errEntries[0].ms;
            if (errEntries[0].epoch) err["epoch"] = errEntries[0].epoch;
        }

        String json;
        serializeJson(doc, json);
        client->send(json.c_str(), SSE_EVENT_EVENT, ++eventSeq);
    });

    BaseType_t ok = xTaskCreate(
        WiFiManager::eventStreamTask,
        "EventStreamTask",
        3072,
        this,
        1,
        &eventStreamTaskHandle
    );
    if (ok != pdPASS) {
        eventStreamTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFi] Failed to start EventStreamTask");
    }
}

void WiFiManager::eventStreamTask(void* pv) {
    WiFiManager* self = static_cast<WiFiManager*>(pv);

    for (;;) {
        if (!DEVICE) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        Device::EventNotice note{};
        if (!DEVICE->waitForEventNotice(note, portMAX_DELAY)) continue;

        StaticJsonDocument<256> doc;
        doc["kind"] = (note.kind == Device::EventKind::Warning) ? "warning" : "error";
        doc["reason"] = note.reason;
        if (note.ms) doc["ms"] = note.ms;
        if (note.epoch) doc["epoch"] = note.epoch;
        JsonObject unread = doc.createNestedObject("unread");
        unread["warn"] = note.unreadWarn;
        unread["error"] = note.unreadErr;

        String json;
        serializeJson(doc, json);
        self->eventSse.send(json.c_str(), SSE_EVENT_EVENT, ++self->eventSeq);
    }
}

// ===================== Snapshot task =====================

void WiFiManager::startSnapshotTask(uint32_t periodMs) {
    if (_snapMtx == nullptr) {
        _snapMtx = xSemaphoreCreateMutex();
    }
    _monitorJson.reserve(1536);
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
    StaticJsonDocument<1536> doc;
    String monitorJson;
    monitorJson.reserve(1536);
    constexpr float kWireTargetMaxC = 150.0f;

    for (;;) {
        // Cap voltage & current (these should be cheap / cached)
        if (DEVICE && DEVICE->discharger) {
            local.capVoltage = DEVICE->discharger->readCapVoltage();
            local.capAdcScaled = DEVICE->discharger->readCapAdcScaled();
        } else {
            local.capVoltage = 0.0f;
            local.capAdcScaled = 0.0f;
        }
        float estCurrent = 0.0f;
        if (WIRE && isfinite(local.capVoltage)) {
            estCurrent = WIRE->estimateCurrentFromVoltage(local.capVoltage, WIRE->getOutputMask());
            if (!isfinite(estCurrent)) estCurrent = 0.0f;
        }
        local.current = estCurrent;

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
        if (NTC) {
            const uint8_t ntcIdx = getNtcGateIndexFromConfig();
            const float ntcTemp = NTC->getLastTempC();
            if (isfinite(ntcTemp)) {
                local.wireTemps[ntcIdx - 1] = ntcTemp;
            }
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

        float targetC = NAN;
        if (DEVICE) {
            const Device::WireTargetStatus wt = DEVICE->getWireTargetStatus();
            if (wt.active && isfinite(wt.targetC)) {
                targetC = wt.targetC;
            } else {
                const Device::FloorControlStatus fc = DEVICE->getFloorControlStatus();
                if (fc.active && isfinite(fc.wireTargetC)) {
                    targetC = fc.wireTargetC;
                } else {
                    float v = DEFAULT_NICHROME_FINAL_TEMP_C;
                    if (CONF) {
                        v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                           DEFAULT_NICHROME_FINAL_TEMP_C);
                    }
                    if (isfinite(v) && v > 0.0f) targetC = v;
                }
            }
        }
        if (isfinite(targetC)) {
            if (targetC > kWireTargetMaxC) targetC = kWireTargetMaxC;
            if (targetC < 0.0f) targetC = 0.0f;
            doc["wireTargetC"] = targetC;
        }

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

            Device::AmbientWaitStatus wait = DEVICE->getAmbientWaitStatus();
            JsonObject waitObj = doc.createNestedObject("ambientWait");
            waitObj["active"] = wait.active;
            if (wait.active) {
                if (wait.sinceMs) waitObj["since_ms"] = wait.sinceMs;
                if (isfinite(wait.tolC)) waitObj["tol_c"] = wait.tolC;
                if (wait.reason[0]) waitObj["reason"] = wait.reason;
            }
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
