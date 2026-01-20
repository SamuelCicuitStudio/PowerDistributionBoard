#include <WiFiManager.hpp>
#include <WiFiCbor.hpp>
#include <Utils.hpp>
#include <DeviceTransport.hpp>
#include <CalibrationRecorder.hpp>
#include <BusSampler.hpp>
#include <NtcSensor.hpp>
#include <RTCManager.hpp>
#include <SPIFFS.h>
#include <esp_system.h>
#include <string.h>
#include <math.h>
#include <vector>

namespace {
static uint8_t getNtcGateIndexFromConfig() {
    int idx = DEFAULT_NTC_GATE_INDEX;
    if (CONF) {
        idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    }
    if (idx < 1) idx = 1;
    if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
    return static_cast<uint8_t>(idx);
}

static uint32_t getNtcCalSampleMsFromConfig() {
    int v = DEFAULT_NTC_CAL_SAMPLE_MS;
    if (CONF) {
        v = CONF->GetInt(NTC_CAL_SAMPLE_MS_KEY, DEFAULT_NTC_CAL_SAMPLE_MS);
    }
    if (v < 50) v = 50;
    if (v > 5000) v = 5000;
    return static_cast<uint32_t>(v);
}

static uint32_t getNtcCalTimeoutMsFromConfig() {
    int v = DEFAULT_NTC_CAL_TIMEOUT_MS;
    if (CONF) {
        v = CONF->GetInt(NTC_CAL_TIMEOUT_MS_KEY, DEFAULT_NTC_CAL_TIMEOUT_MS);
    }
    if (v < 1000) v = 1000;
    if (v > 3600000) v = 3600000;
    return static_cast<uint32_t>(v);
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

static const char* kWireModelTauKeys[HeaterManager::kWireCount] = {
    W1TAU_KEY, W2TAU_KEY, W3TAU_KEY, W4TAU_KEY, W5TAU_KEY,
    W6TAU_KEY, W7TAU_KEY, W8TAU_KEY, W9TAU_KEY, W10TAU_KEY
};
static const char* kWireModelKKeys[HeaterManager::kWireCount] = {
    W1KLS_KEY, W2KLS_KEY, W3KLS_KEY, W4KLS_KEY, W5KLS_KEY,
    W6KLS_KEY, W7KLS_KEY, W8KLS_KEY, W9KLS_KEY, W10KLS_KEY
};
static const char* kWireModelCKeys[HeaterManager::kWireCount] = {
    W1CAP_KEY, W2CAP_KEY, W3CAP_KEY, W4CAP_KEY, W5CAP_KEY,
    W6CAP_KEY, W7CAP_KEY, W8CAP_KEY, W9CAP_KEY, W10CAP_KEY
};
static const char* kWireCalibDoneKeys[HeaterManager::kWireCount] = {
    CALIB_W1_DONE_KEY, CALIB_W2_DONE_KEY, CALIB_W3_DONE_KEY, CALIB_W4_DONE_KEY, CALIB_W5_DONE_KEY,
    CALIB_W6_DONE_KEY, CALIB_W7_DONE_KEY, CALIB_W8_DONE_KEY, CALIB_W9_DONE_KEY, CALIB_W10_DONE_KEY
};
static const char* kWireCalibStageKeys[HeaterManager::kWireCount] = {
    CALIB_W1_STAGE_KEY, CALIB_W2_STAGE_KEY, CALIB_W3_STAGE_KEY, CALIB_W4_STAGE_KEY, CALIB_W5_STAGE_KEY,
    CALIB_W6_STAGE_KEY, CALIB_W7_STAGE_KEY, CALIB_W8_STAGE_KEY, CALIB_W9_STAGE_KEY, CALIB_W10_STAGE_KEY
};
static const char* kWireCalibRunKeys[HeaterManager::kWireCount] = {
    CALIB_W1_RUNNING_KEY, CALIB_W2_RUNNING_KEY, CALIB_W3_RUNNING_KEY, CALIB_W4_RUNNING_KEY, CALIB_W5_RUNNING_KEY,
    CALIB_W6_RUNNING_KEY, CALIB_W7_RUNNING_KEY, CALIB_W8_RUNNING_KEY, CALIB_W9_RUNNING_KEY, CALIB_W10_RUNNING_KEY
};
static const char* kWireCalibTsKeys[HeaterManager::kWireCount] = {
    CALIB_W1_TS_KEY, CALIB_W2_TS_KEY, CALIB_W3_TS_KEY, CALIB_W4_TS_KEY, CALIB_W5_TS_KEY,
    CALIB_W6_TS_KEY, CALIB_W7_TS_KEY, CALIB_W8_TS_KEY, CALIB_W9_TS_KEY, CALIB_W10_TS_KEY
};
static const char* kWireAccessKeys[HeaterManager::kWireCount] = {
    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
    OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
};
static const char* kWireResKeys[HeaterManager::kWireCount] = {
    R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
    R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
};

static bool isNonEmptyString(const String& s) {
    return s.length() > 0;
}

namespace {
constexpr size_t kCborKeyMax = 48;
constexpr size_t kCborTextMax = 128;

bool readCborText_(CborValue* it, String& out) {
    if (!cbor_value_is_text_string(it)) return false;
    char buf[kCborTextMax];
    size_t len = sizeof(buf) - 1;
    if (cbor_value_copy_text_string(it, buf, &len, it) != CborNoError) {
        return false;
    }
    buf[len] = '\0';
    out = buf;
    return true;
}

bool readCborText_(CborValue* it, char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    if (!cbor_value_is_text_string(it)) return false;
    size_t len = outLen - 1;
    if (cbor_value_copy_text_string(it, out, &len, it) != CborNoError) {
        return false;
    }
    out[len] = '\0';
    return true;
}

bool readCborBool_(CborValue* it, bool& value) {
    if (!cbor_value_is_boolean(it)) return false;
    if (cbor_value_get_boolean(it, &value) != CborNoError) return false;
    return cbor_value_advance(it) == CborNoError;
}

bool readCborInt64_(CborValue* it, int64_t& value) {
    if (!cbor_value_is_integer(it)) return false;
    if (cbor_value_get_int64(it, &value) != CborNoError) return false;
    return cbor_value_advance(it) == CborNoError;
}

bool readCborUInt64_(CborValue* it, uint64_t& value) {
    if (!cbor_value_is_integer(it)) return false;
    if (cbor_value_get_uint64(it, &value) != CborNoError) return false;
    return cbor_value_advance(it) == CborNoError;
}

bool readCborDouble_(CborValue* it, double& value) {
    if (cbor_value_is_double(it)) {
        if (cbor_value_get_double(it, &value) != CborNoError) return false;
        return cbor_value_advance(it) == CborNoError;
    }
    if (cbor_value_is_float(it)) {
        float tmp = 0.0f;
        if (cbor_value_get_float(it, &tmp) != CborNoError) return false;
        value = tmp;
        return cbor_value_advance(it) == CborNoError;
    }
    if (cbor_value_is_integer(it)) {
        int64_t iv = 0;
        if (cbor_value_get_int64(it, &iv) != CborNoError) return false;
        value = static_cast<double>(iv);
        return cbor_value_advance(it) == CborNoError;
    }
    return false;
}

bool skipCborValue_(CborValue* it) {
    return cbor_value_advance(it) == CborNoError;
}

template <typename Handler>
bool parseCborMap_(const std::vector<uint8_t>& body, Handler&& handler) {
    if (body.empty()) return false;
    CborParser parser;
    CborValue it;
    if (cbor_parser_init(body.data(), body.size(), 0, &parser, &it) != CborNoError) {
        return false;
    }
    if (!cbor_value_is_map(&it)) {
        return false;
    }
    CborValue mapIt;
    if (cbor_value_enter_container(&it, &mapIt) != CborNoError) {
        return false;
    }
    while (!cbor_value_at_end(&mapIt)) {
        if (!cbor_value_is_text_string(&mapIt)) {
            return false;
        }
        char key[kCborKeyMax];
        size_t keyLen = sizeof(key) - 1;
        if (cbor_value_copy_text_string(&mapIt, key, &keyLen, &mapIt) != CborNoError) {
            return false;
        }
        key[keyLen] = '\0';
        if (!handler(key, &mapIt)) {
            return false;
        }
    }
    return true;
}

template <typename Handler>
bool parseCborValueMap_(CborValue* value, Handler&& handler) {
    if (!value) return false;
    if (!cbor_value_is_map(value)) {
        return false;
    }
    CborValue mapIt;
    if (cbor_value_enter_container(value, &mapIt) != CborNoError) {
        return false;
    }
    while (!cbor_value_at_end(&mapIt)) {
        if (!cbor_value_is_text_string(&mapIt)) {
            return false;
        }
        char key[kCborKeyMax];
        size_t keyLen = sizeof(key) - 1;
        if (cbor_value_copy_text_string(&mapIt, key, &keyLen, &mapIt) != CborNoError) {
            return false;
        }
        key[keyLen] = '\0';
        if (!handler(key, &mapIt)) {
            return false;
        }
    }
    return true;
}

template <typename Handler>
void collectCborBody_(AsyncWebServerRequest* request,
                      uint8_t* data,
                      size_t len,
                      size_t index,
                      size_t total,
                      Handler&& handler) {
    if (!request) return;
    auto* body = static_cast<std::vector<uint8_t>*>(request->_tempObject);
    if (index == 0) {
        body = new std::vector<uint8_t>();
        if (total > 0) {
            body->reserve(total);
        }
        request->_tempObject = body;
    }
    if (!body) {
        return;
    }
    body->insert(body->end(), data, data + len);
    if (index + len != total) {
        return;
    }
    std::vector<uint8_t> payload;
    payload.swap(*body);
    delete body;
    request->_tempObject = nullptr;
    handler(request, payload);
}

void sendOk_(AsyncWebServerRequest* request, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 64, [&](CborEncoder* map) {
            return WiFiCbor::encodeKvBool(map, "ok", true);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendStatusApplied_(AsyncWebServerRequest* request, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 96, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
            return WiFiCbor::encodeKvBool(map, "applied", true);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendStatusQueued_(AsyncWebServerRequest* request, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 96, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
            return WiFiCbor::encodeKvBool(map, "queued", true);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendStatusRunning_(AsyncWebServerRequest* request, bool running, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 96, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
            return WiFiCbor::encodeKvBool(map, "running", running);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendStatusRunningSaved_(AsyncWebServerRequest* request, bool saved, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 128, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
            if (!WiFiCbor::encodeKvBool(map, "running", false)) return false;
            return WiFiCbor::encodeKvBool(map, "saved", saved);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendStatusClearedFile_(AsyncWebServerRequest* request,
                            bool removed,
                            size_t removedCount,
                            int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 160, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
            if (!WiFiCbor::encodeKvBool(map, "cleared", true)) return false;
            if (!WiFiCbor::encodeKvBool(map, "file_removed", removed)) return false;
            return WiFiCbor::encodeKvUInt(map, "history_removed",
                                          static_cast<uint64_t>(removedCount));
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendState_(AsyncWebServerRequest* request, const char* state, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 96, [&](CborEncoder* map) {
            return WiFiCbor::encodeKvText(map, "state", state ? state : STATE_UNKNOWN);
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendHistoryEmpty_(AsyncWebServerRequest* request, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 64, [&](CborEncoder* map) {
            if (!WiFiCbor::encodeText(map, "history")) return false;
            CborEncoder arr;
            if (cbor_encoder_create_array(map, &arr, 0) != CborNoError) return false;
            return cbor_encoder_close_container(map, &arr) == CborNoError;
        })) {
        request->send(500, CT_TEXT_PLAIN, "error");
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}
} // namespace

static void appendMissing(std::vector<const char*>* arr, const char* key) {
    if (arr && key) {
        arr->push_back(key);
    }
}

static bool checkSetupConfig(std::vector<const char*>* missing) {
    if (!CONF) return false;

    bool ok = true;

    const String devId = CONF->GetString(DEV_ID_KEY, "");
    if (!isNonEmptyString(devId)) {
        appendMissing(missing, DEV_ID_KEY);
        ok = false;
    }
    const String adminId = CONF->GetString(ADMIN_ID_KEY, "");
    const String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
    if (!isNonEmptyString(adminId)) {
        appendMissing(missing, ADMIN_ID_KEY);
        ok = false;
    }
    if (!isNonEmptyString(adminPass)) {
        appendMissing(missing, ADMIN_PASS_KEY);
        ok = false;
    }

    const String staSsid = CONF->GetString(STA_SSID_KEY, "");
    const String staPass = CONF->GetString(STA_PASS_KEY, "");
    if (!isNonEmptyString(staSsid)) {
        appendMissing(missing, STA_SSID_KEY);
        ok = false;
    }
    if (!isNonEmptyString(staPass)) {
        appendMissing(missing, STA_PASS_KEY);
        ok = false;
    }
    const String apName = CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, "");
    const String apPass = CONF->GetString(DEVICE_AP_AUTH_PASS_KEY, "");
    if (!isNonEmptyString(apName)) {
        appendMissing(missing, DEVICE_WIFI_HOTSPOT_NAME_KEY);
        ok = false;
    }
    if (!isNonEmptyString(apPass)) {
        appendMissing(missing, DEVICE_AP_AUTH_PASS_KEY);
        ok = false;
    }

    const float tempTrip = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
    if (!isfinite(tempTrip) || tempTrip <= 0.0f) {
        appendMissing(missing, TEMP_THRESHOLD_KEY);
        ok = false;
    }
    const float tempWarn = CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C);
    if (!isfinite(tempWarn) || tempWarn <= 0.0f) {
        appendMissing(missing, TEMP_WARN_KEY);
        ok = false;
    }
    const float floorMax = CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C);
    if (!isfinite(floorMax) || floorMax <= 0.0f) {
        appendMissing(missing, FLOOR_MAX_C_KEY);
        ok = false;
    }
    const float nichromeMax =
        CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY, DEFAULT_NICHROME_FINAL_TEMP_C);
    if (!isfinite(nichromeMax) || nichromeMax <= 0.0f) {
        appendMissing(missing, NICHROME_FINAL_TEMP_C_KEY);
        ok = false;
    }
    const float floorMargin =
        CONF->GetFloat(FLOOR_SWITCH_MARGIN_C_KEY, DEFAULT_FLOOR_SWITCH_MARGIN_C);
    if (!isfinite(floorMargin) || floorMargin <= 0.0f) {
        appendMissing(missing, FLOOR_SWITCH_MARGIN_C_KEY);
        ok = false;
    }
    const float currLimit = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
    if (!isfinite(currLimit) || currLimit < 0.0f) {
        appendMissing(missing, CURR_LIMIT_KEY);
        ok = false;
    }
    int currentSource = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
    if (currentSource != CURRENT_SRC_ACS && currentSource != CURRENT_SRC_ESTIMATE) {
        appendMissing(missing, CURRENT_SOURCE_KEY);
        ok = false;
    }

    const int acFreq = CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
    if (acFreq <= 0) {
        appendMissing(missing, AC_FREQUENCY_KEY);
        ok = false;
    }
    const float acVolt = CONF->GetFloat(AC_VOLTAGE_KEY, DEFAULT_AC_VOLTAGE);
    if (!isfinite(acVolt) || acVolt <= 0.0f) {
        appendMissing(missing, AC_VOLTAGE_KEY);
        ok = false;
    }
    const float chargeRes = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
    if (!isfinite(chargeRes) || chargeRes <= 0.0f) {
        appendMissing(missing, CHARGE_RESISTOR_KEY);
        ok = false;
    }

    const float ohmPerM = CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M);
    if (!isfinite(ohmPerM) || ohmPerM <= 0.0f) {
        appendMissing(missing, WIRE_OHM_PER_M_KEY);
        ok = false;
    }
    const int gauge = CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE);
    if (gauge <= 0) {
        appendMissing(missing, WIRE_GAUGE_KEY);
        ok = false;
    }

    const int ntcGate = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    if (ntcGate < 1 || ntcGate > HeaterManager::kWireCount) {
        appendMissing(missing, NTC_GATE_INDEX_KEY);
        ok = false;
    }

    const float ntcBeta = CONF->GetFloat(NTC_BETA_KEY, DEFAULT_NTC_BETA);
    if (!isfinite(ntcBeta) || ntcBeta <= 0.0f) {
        appendMissing(missing, NTC_BETA_KEY);
        ok = false;
    }
    const float ntcT0C = CONF->GetFloat(NTC_T0_C_KEY, DEFAULT_NTC_T0_C);
    if (!isfinite(ntcT0C)) {
        appendMissing(missing, NTC_T0_C_KEY);
        ok = false;
    }
    const float ntcR0 = CONF->GetFloat(NTC_R0_KEY, DEFAULT_NTC_R0_OHMS);
    if (!isfinite(ntcR0) || ntcR0 <= 0.0f) {
        appendMissing(missing, NTC_R0_KEY);
        ok = false;
    }
    const float ntcFixed = CONF->GetFloat(NTC_FIXED_RES_KEY, DEFAULT_NTC_FIXED_RES_OHMS);
    if (!isfinite(ntcFixed) || ntcFixed <= 0.0f) {
        appendMissing(missing, NTC_FIXED_RES_KEY);
        ok = false;
    }

    const float minDrop = CONF->GetFloat(PRESENCE_MIN_DROP_V_KEY,
                                         DEFAULT_PRESENCE_MIN_DROP_V);
    if (!isfinite(minDrop) || minDrop <= 0.0f) {
        appendMissing(missing, PRESENCE_MIN_DROP_V_KEY);
        ok = false;
    }

    bool anyEnabled = false;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const bool allowed = CONF->GetBool(kWireAccessKeys[i], false);
        if (!allowed) continue;
        anyEnabled = true;
        const float r = CONF->GetFloat(kWireResKeys[i], DEFAULT_WIRE_RES_OHMS);
        if (!isfinite(r) || r <= 0.01f) {
            appendMissing(missing, kWireResKeys[i]);
            ok = false;
        }
    }
    if (!anyEnabled) {
        appendMissing(missing, "outputs");
        ok = false;
    }

    return ok;
}

static bool checkSetupCalib(std::vector<const char*>* missing) {
    if (!CONF) return false;
    bool ok = true;

    if (!CONF->GetBool(CALIB_CAP_DONE_KEY, DEFAULT_CALIB_CAP_DONE)) {
        appendMissing(missing, CALIB_CAP_DONE_KEY);
        ok = false;
    }
    const float capF = CONF->GetFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
    if (!isfinite(capF) || capF <= 0.0f) {
        appendMissing(missing, CAP_BANK_CAP_F_KEY);
        ok = false;
    }

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const bool allowed = CONF->GetBool(kWireAccessKeys[i], false);
        if (!allowed) continue;
        if (!CONF->GetBool(kWireCalibDoneKeys[i], DEFAULT_CALIB_W_DONE)) {
            appendMissing(missing, kWireCalibDoneKeys[i]);
            ok = false;
        }
    }

    if (!CONF->GetBool(CALIB_PRESENCE_DONE_KEY, DEFAULT_CALIB_PRESENCE_DONE)) {
        appendMissing(missing, CALIB_PRESENCE_DONE_KEY);
        ok = false;
    }
    if (!CONF->GetBool(CALIB_FLOOR_DONE_KEY, DEFAULT_CALIB_FLOOR_DONE)) {
        appendMissing(missing, CALIB_FLOOR_DONE_KEY);
        ok = false;
    }

    return ok;
}

static void updateWireCalibStage(uint8_t wireIndex, int stage) {
    if (!CONF || wireIndex < 1 || wireIndex > HeaterManager::kWireCount) return;
    CONF->PutInt(kWireCalibStageKeys[wireIndex - 1], stage);
    if (RTC) {
        CONF->PutInt(kWireCalibTsKeys[wireIndex - 1],
                     static_cast<int>(RTC->getUnixTime()));
    }
}

static void updateWireCalibRunning(uint8_t wireIndex, bool running) {
    if (!CONF || wireIndex < 1 || wireIndex > HeaterManager::kWireCount) return;
    CONF->PutBool(kWireCalibRunKeys[wireIndex - 1], running);
    if (RTC) {
        CONF->PutInt(kWireCalibTsKeys[wireIndex - 1],
                     static_cast<int>(RTC->getUnixTime()));
    }
}

namespace {
constexpr float kNtcCalTargetDefaultC = DEFAULT_NTC_CAL_TARGET_C;
constexpr uint32_t kNtcCalSampleMsDefault = static_cast<uint32_t>(DEFAULT_NTC_CAL_SAMPLE_MS);
constexpr uint32_t kNtcCalTimeoutMs = static_cast<uint32_t>(DEFAULT_NTC_CAL_TIMEOUT_MS);
constexpr uint32_t kNtcCalMinSamples = 6;
constexpr uint32_t kModelCalPollMs = 500;
constexpr uint32_t kModelCalTimeoutMs = 30 * 60 * 1000;
constexpr uint32_t kModelCalSteadyMsDefault = 60000;
constexpr uint32_t kFloorCalPollMs = 500;
constexpr uint32_t kFloorCalAmbientMsDefault = 5 * 60 * 1000;
constexpr uint32_t kFloorCalHeatMsDefault = 30 * 60 * 1000;
constexpr uint32_t kFloorCalTimeoutMsDefault = 60 * 60 * 1000;
constexpr uint32_t kFloorCalSteadyMsDefault = 120000;
constexpr uint32_t kFloorCalCoolMsDefault = 10 * 60 * 1000;
constexpr float kFloorCalStableSlopeCPerMin = 0.05f;
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
static TaskHandle_t s_floorCalTask = nullptr;
static bool s_floorCalAbort = false;

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
    float    dutyFrac  = 1.0f;
    uint32_t timeoutMs = kModelCalTimeoutMs;
    uint32_t startMs   = 0;
    uint32_t calibStartMs = 0;
};

struct FloorCalTaskArgs {
    float    targetC     = NAN;
    uint8_t  wireIndex   = 1;
    uint32_t ambientMs   = kFloorCalAmbientMsDefault;
    uint32_t heatMs      = kFloorCalHeatMsDefault;
    uint32_t timeoutMs   = kFloorCalTimeoutMsDefault;
    uint32_t coolMs      = kFloorCalCoolMsDefault;
    float    dutyFrac    = 0.5f;
    uint32_t calibStartMs = 0;
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

static bool computeWireModelFromSamples(uint32_t heatStartMs,
                                        uint32_t heatStopMs,
                                        uint8_t wireIndex,
                                        float dutyFrac,
                                        double& outTau,
                                        double& outK,
                                        double& outC,
                                        float& outAmb,
                                        float& outInf,
                                        float& outPowerW,
                                        const char*& outErr);

static bool floorCalAbortRequested() {
    return s_floorCalAbort;
}

static void floorCalRequestAbort() {
    s_floorCalAbort = true;
}

static void ntcCalTask(void* param) {
    NtcCalTaskArgs args{};
    if (param) {
        args = *static_cast<NtcCalTaskArgs*>(param);
        delete static_cast<NtcCalTaskArgs*>(param);
    }

    const uint32_t startMs = args.startMs ? args.startMs : millis();
    uint32_t lastUpdateMs = startMs;

    const bool useFixedRef =
        isfinite(args.targetC) && args.targetC > 0.0f;
    const float fixedRefC = useFixedRef ? args.targetC : NAN;

    double rSum = 0.0;
    uint32_t rCount = 0;
    double refSum = 0.0;
    uint32_t refCount = 0;
    uint32_t samples = 0;

    bool failed = false;
    const char* failReason = nullptr;

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

        if (!DEVICE || !NTC) {
            failed = true;
            failReason = ERR_SENSOR_MISSING;
            break;
        }
        float refC = NAN;
        if (useFixedRef) {
            refC = fixedRefC;
        } else if (DEVICE->tempSensor) {
            refC = DEVICE->tempSensor->getHeatsinkTemp();
        }

        NTC->update();
        NtcSensor::Sample s = NTC->getLastSample();

        bool sampleOk = false;
        if (isfinite(refC) &&
            isfinite(s.rNtcOhm) && s.rNtcOhm > 0.0f &&
            !s.pressed) {
            rSum += static_cast<double>(s.rNtcOhm);
            rCount++;
            if (!useFixedRef) {
                refSum += static_cast<double>(refC);
                refCount++;
            }
            samples++;
            sampleOk = true;
        }

        if (sampleOk || (nowMs - lastUpdateMs) >= args.sampleMs) {
            ntcCalUpdateProgress(refC, s.rNtcOhm, samples, elapsedMs);
            lastUpdateMs = nowMs;
        }

        if (samples >= kNtcCalMinSamples) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(args.sampleMs));
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
        if (rCount < kNtcCalMinSamples) {
            ntcCalSetError(ERR_NOT_ENOUGH_SAMPLES, elapsedMs);
        } else {
            const float refC = useFixedRef
                                   ? fixedRefC
                                   : (refCount > 0 ? static_cast<float>(refSum / refCount) : NAN);
            if (!isfinite(refC) || refC <= 0.0f) {
                ntcCalSetError(ERR_INVALID_REF_TEMP, elapsedMs);
            } else {
                const float beta = NTC ? NTC->getBeta() : DEFAULT_NTC_BETA;
                const float t0K = DEFAULT_NTC_T0_C + 273.15f;
                const float tRefK = refC + 273.15f;
                float r0 = NAN;
                if (isfinite(beta) && beta > 0.0f && tRefK > 0.0f) {
                    const float rAvg = static_cast<float>(rSum / rCount);
                    r0 = rAvg / expf(beta * (1.0f / tRefK - 1.0f / t0K));
                }
                if (!isfinite(r0) || r0 <= 0.0f) {
                    ntcCalSetError(ERR_PERSIST_FAILED, elapsedMs);
                } else if (!NTC) {
                    ntcCalSetError(ERR_SENSOR_MISSING, elapsedMs);
                } else {
                    NTC->setR0(r0, true);
                    NTC->setModel(NtcSensor::Model::Beta, true);
                    if (CONF) {
                        CONF->PutBool(CALIB_NTC_DONE_KEY, true);
                    }
                    ntcCalFinish(NAN, NAN, NAN, samples, elapsedMs);
                }
            }
        }
    }

    if (CALREC) {
        CALREC->stop();
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
    const uint32_t calibStartMs = args.calibStartMs ? args.calibStartMs : startMs;
    bool failed = false;
    const char* failReason = nullptr;
    bool heating = true;
    float baseTempC = NAN;
    uint32_t heatStartAbs = 0;
    uint32_t heatStopAbs = 0;
    bool heatStartLocked = false;

    updateWireCalibRunning(args.wireIndex, true);
    updateWireCalibStage(args.wireIndex, 1);

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

        if (statusActive && !heatStartLocked && st.packetMs > 0 && st.updatedMs > 0) {
            heatStartAbs = st.updatedMs;
            heatStartLocked = true;
            updateWireCalibStage(args.wireIndex, 2);
        }

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
            if (heatStopAbs == 0) {
                heatStopAbs = millis();
            }
            updateWireCalibStage(args.wireIndex, 3);
        }

        if (!statusActive) {
            if (heating) {
                failed = true;
                failReason = ERR_ENERGY_STOPPED;
                break;
            }
            if (heatStopAbs == 0) {
                heatStopAbs = nowMs;
                updateWireCalibStage(args.wireIndex, 3);
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

    if (CALREC) {
        if (failed) {
            CALREC->stop();
        } else {
            CALREC->stopAndSave(5000);
        }
    }

    if (heatStopAbs == 0) heatStopAbs = millis();
    if (heatStartAbs == 0) heatStartAbs = heatStopAbs;

    const uint32_t heatStartMs =
        (heatStartAbs >= calibStartMs) ? (heatStartAbs - calibStartMs) : 0;
    const uint32_t heatStopMs =
        (heatStopAbs >= calibStartMs) ? (heatStopAbs - calibStartMs) : heatStartMs;

    if (!failed) {
        double tau = NAN;
        double kLoss = NAN;
        double capC = NAN;
        float ambC = NAN;
        float infC = NAN;
        float powerW = NAN;
        const char* calcErr = nullptr;

        if (!computeWireModelFromSamples(heatStartMs,
                                         heatStopMs,
                                         args.wireIndex,
                                         args.dutyFrac,
                                         tau,
                                         kLoss,
                                         capC,
                                         ambC,
                                         infC,
                                         powerW,
                                         calcErr)) {
            failed = true;
            failReason = calcErr ? calcErr : ERR_FAILED;
        } else if (CONF) {
            if (args.wireIndex >= 1 && args.wireIndex <= HeaterManager::kWireCount) {
                const uint8_t idx = static_cast<uint8_t>(args.wireIndex - 1);
                CONF->PutDouble(kWireModelTauKeys[idx], tau);
                CONF->PutDouble(kWireModelKKeys[idx], kLoss);
                CONF->PutDouble(kWireModelCKeys[idx], capC);
                CONF->PutBool(kWireCalibDoneKeys[idx], true);
                updateWireCalibStage(args.wireIndex, 4);
            }
            if (DEVICE) {
                DEVICE->getWireThermalModel()
                    .setWireThermalParams(args.wireIndex, tau, kLoss, capC);
            }
        }
    }

    if (failed) {
        DEBUG_PRINTF("[WiFi] Model calibration failed: %s\n",
                     failReason ? failReason : ERR_FAILED);
    }

    updateWireCalibRunning(args.wireIndex, false);

    s_modelCalTask = nullptr;
    vTaskDelete(nullptr);
}

static bool computeWireModelFromSamples(uint32_t heatStartMs,
                                        uint32_t heatStopMs,
                                        uint8_t wireIndex,
                                        float dutyFrac,
                                        double& outTau,
                                        double& outK,
                                        double& outC,
                                        float& outAmb,
                                        float& outInf,
                                        float& outPowerW,
                                        const char*& outErr) {
    outErr = ERR_FAILED;
    if (!CALREC) {
        outErr = ERR_START_FAILED;
        return false;
    }
    if (!WIRE) {
        outErr = ERR_WIRE_SUBSYSTEM_MISSING;
        return false;
    }
    const uint16_t total = CALREC->getSampleCount();
    if (total < 4) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (heatStopMs <= heatStartMs) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    float r = WIRE->getWireResistance(wireIndex);
    if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;

    float duty = dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 1.0f;
    if (duty > 1.0f) duty = 1.0f;

    const uint32_t heatWindowMs = heatStopMs - heatStartMs;
    uint32_t steadyWindowMs = kModelCalSteadyMsDefault;
    if (steadyWindowMs > heatWindowMs) steadyWindowMs = heatWindowMs;
    const uint32_t steadyStartMs =
        (heatStopMs > steadyWindowMs) ? (heatStopMs - steadyWindowMs) : heatStartMs;

    uint32_t ambientWindowMs = kModelCalSteadyMsDefault;
    if (ambientWindowMs > heatStartMs) ambientWindowMs = heatStartMs;
    const uint32_t ambientStartMs =
        (heatStartMs > ambientWindowMs) ? (heatStartMs - ambientWindowMs) : 0;

    double ambSum = 0.0;
    uint32_t ambCount = 0;
    double infSum = 0.0;
    uint32_t infCount = 0;
    double powerSum = 0.0;
    uint32_t powerCount = 0;

    CalibrationRecorder::Sample buf[32];
    uint16_t copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs >= ambientStartMs && s.tMs <= heatStartMs &&
                isfinite(s.tempC)) {
                ambSum += s.tempC;
                ambCount++;
            }
            if (s.tMs >= heatStartMs && s.tMs <= heatStopMs &&
                isfinite(s.voltageV)) {
                const double v = static_cast<double>(s.voltageV);
                double p = NAN;
                if (isfinite(s.currentA)) {
                    double iCur = static_cast<double>(s.currentA);
                    if (iCur < 0.0) iCur = 0.0;
                    p = v * iCur;
                }
                if (!isfinite(p)) {
                    p = (v * v) / static_cast<double>(r);
                    p *= static_cast<double>(duty);
                }
                powerSum += p;
                powerCount++;
            }
            if (s.tMs >= steadyStartMs && s.tMs <= heatStopMs &&
                isfinite(s.tempC)) {
                infSum += s.tempC;
                infCount++;
            }
        }
        copied += got;
    }

    if (ambCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (infCount < 3 || powerCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }

    outAmb = static_cast<float>(ambSum / static_cast<double>(ambCount));
    outInf = static_cast<float>(infSum / static_cast<double>(infCount));
    outPowerW = static_cast<float>(powerSum / static_cast<double>(powerCount));
    if (!isfinite(outAmb) || !isfinite(outInf) || !isfinite(outPowerW)) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }

    const double deltaT = static_cast<double>(outInf - outAmb);
    if (!isfinite(deltaT) || deltaT <= 0.05) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }
    if (outPowerW <= 0.0f) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    const double t63Target = static_cast<double>(outAmb) + 0.632 * deltaT;
    double t63 = NAN;
    double sumT = 0.0;
    double sumY = 0.0;
    double sumTT = 0.0;
    double sumTY = 0.0;
    uint32_t fitCount = 0;

    copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs < heatStartMs || s.tMs > heatStopMs) continue;
            if (!isfinite(s.tempC)) continue;
            const double temp = static_cast<double>(s.tempC);
            const double tSec = static_cast<double>(s.tMs - heatStartMs) * 0.001;
            if (!isfinite(t63) && temp >= t63Target && tSec > 0.0) {
                t63 = tSec;
            }
            const double frac = (temp - static_cast<double>(outAmb)) / deltaT;
            if (frac > 0.02 && frac < 0.98) {
                const double y = log(1.0 - frac);
                if (isfinite(y)) {
                    sumT += tSec;
                    sumY += y;
                    sumTT += tSec * tSec;
                    sumTY += tSec * y;
                    fitCount++;
                }
            }
        }
        copied += got;
    }

    double tau = NAN;
    if (isfinite(t63) && t63 > 0.0) {
        tau = t63;
    } else if (fitCount >= 3) {
        const double denom = static_cast<double>(fitCount) * sumTT - sumT * sumT;
        if (fabs(denom) > 1e-6) {
            const double slope =
                (static_cast<double>(fitCount) * sumTY - sumT * sumY) / denom;
            if (isfinite(slope) && slope < 0.0) {
                tau = -1.0 / slope;
            }
        }
    }

    if (!isfinite(tau) || tau <= 0.0) {
        outErr = ERR_FIT_FAILED;
        return false;
    }

    const double kLoss = static_cast<double>(outPowerW) / deltaT;
    const double capC = kLoss * tau;
    if (!isfinite(kLoss) || kLoss <= 0.0 || !isfinite(capC) || capC <= 0.0) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    outTau = tau;
    outK = kLoss;
    outC = capC;
    outErr = nullptr;
    return true;
}

static bool computeFloorModelFromSamples(uint32_t ambientEndMs,
                                         uint32_t heatStartMs,
                                         uint32_t heatStopMs,
                                         uint8_t wireIndex,
                                         float dutyFrac,
                                         double& outTau,
                                         double& outK,
                                         double& outC,
                                         float& outRoomAmb,
                                         float& outFloorInf,
                                         float& outPowerW,
                                         const char*& outErr) {
    outErr = ERR_FAILED;
    if (!CALREC) {
        outErr = ERR_START_FAILED;
        return false;
    }
    if (!WIRE) {
        outErr = ERR_WIRE_SUBSYSTEM_MISSING;
        return false;
    }
    const uint16_t total = CALREC->getSampleCount();
    if (total < 4) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (ambientEndMs == 0) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (heatStopMs <= heatStartMs) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    float r = WIRE->getWireResistance(wireIndex);
    if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;

    float duty = dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 1.0f;
    if (duty > 1.0f) duty = 1.0f;

    const uint32_t heatWindowMs = heatStopMs - heatStartMs;
    uint32_t steadyWindowMs = kFloorCalSteadyMsDefault;
    if (steadyWindowMs > heatWindowMs) steadyWindowMs = heatWindowMs;
    const uint32_t steadyStartMs =
        (heatStopMs > steadyWindowMs) ? (heatStopMs - steadyWindowMs) : heatStartMs;
    uint32_t ambientWindowMs = kFloorCalSteadyMsDefault;
    if (ambientWindowMs > ambientEndMs) ambientWindowMs = ambientEndMs;
    const uint32_t ambientStartMs =
        (ambientEndMs > ambientWindowMs) ? (ambientEndMs - ambientWindowMs) : 0;

    double roomSum = 0.0;
    uint32_t roomCount = 0;
    double floorInfSum = 0.0;
    uint32_t floorInfCount = 0;
    double powerSum = 0.0;
    uint32_t powerCount = 0;

    CalibrationRecorder::Sample buf[32];
    uint16_t copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs >= ambientStartMs && s.tMs <= ambientEndMs &&
                isfinite(s.roomTempC)) {
                roomSum += s.roomTempC;
                roomCount++;
            }
            if (s.tMs >= heatStartMs && s.tMs <= heatStopMs &&
                isfinite(s.voltageV)) {
                const double v = static_cast<double>(s.voltageV);
                double p = NAN;
                if (isfinite(s.currentA)) {
                    double i = static_cast<double>(s.currentA);
                    if (i < 0.0) i = 0.0;
                    p = v * i;
                }
                if (!isfinite(p)) {
                    p = (v * v) / static_cast<double>(r);
                    p *= static_cast<double>(duty);
                }
                powerSum += p;
                powerCount++;
            }
            if (s.tMs >= steadyStartMs && s.tMs <= heatStopMs &&
                isfinite(s.tempC)) {
                floorInfSum += s.tempC;
                floorInfCount++;
            }
        }
        copied += got;
    }

    if (roomCount < 3) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }
    if (floorInfCount < 3 || powerCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }

    outRoomAmb = static_cast<float>(roomSum / static_cast<double>(roomCount));
    outFloorInf = static_cast<float>(floorInfSum / static_cast<double>(floorInfCount));
    outPowerW = static_cast<float>(powerSum / static_cast<double>(powerCount));
    if (!isfinite(outRoomAmb) || !isfinite(outFloorInf) || !isfinite(outPowerW)) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }

    const double deltaT = static_cast<double>(outFloorInf - outRoomAmb);
    if (!isfinite(deltaT) || deltaT <= 0.05) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }
    if (outPowerW <= 0.0f) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    const double t63Target = static_cast<double>(outRoomAmb) + 0.632 * deltaT;
    double t63 = NAN;
    double sumT = 0.0;
    double sumY = 0.0;
    double sumTT = 0.0;
    double sumTY = 0.0;
    uint32_t fitCount = 0;

    copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs < heatStartMs || s.tMs > heatStopMs) continue;
            if (!isfinite(s.tempC)) continue;
            const double temp = static_cast<double>(s.tempC);
            const double tSec = static_cast<double>(s.tMs - heatStartMs) * 0.001;
            if (!isfinite(t63) && temp >= t63Target && tSec > 0.0) {
                t63 = tSec;
            }
            const double frac = (temp - static_cast<double>(outRoomAmb)) / deltaT;
            if (frac > 0.02 && frac < 0.98) {
                const double y = log(1.0 - frac);
                if (isfinite(y)) {
                    sumT += tSec;
                    sumY += y;
                    sumTT += tSec * tSec;
                    sumTY += tSec * y;
                    fitCount++;
                }
            }
        }
        copied += got;
    }

    double tau = NAN;
    if (isfinite(t63) && t63 > 0.0) {
        tau = t63;
    } else if (fitCount >= 3) {
        const double denom = static_cast<double>(fitCount) * sumTT - sumT * sumT;
        if (fabs(denom) > 1e-6) {
            const double slope =
                (static_cast<double>(fitCount) * sumTY - sumT * sumY) / denom;
            if (isfinite(slope) && slope < 0.0) {
                tau = -1.0 / slope;
            }
        }
    }

    if (!isfinite(tau) || tau <= 0.0) {
        outErr = ERR_FIT_FAILED;
        return false;
    }

    const double kLoss = static_cast<double>(outPowerW) / deltaT;
    const double capC = kLoss * tau;
    if (!isfinite(kLoss) || kLoss <= 0.0 || !isfinite(capC) || capC <= 0.0) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    outTau = tau;
    outK = kLoss;
    outC = capC;
    outErr = nullptr;
    return true;
}

static void floorCalTask(void* param) {
    FloorCalTaskArgs args{};
    if (param) {
        args = *static_cast<FloorCalTaskArgs*>(param);
        delete static_cast<FloorCalTaskArgs*>(param);
    }

    const uint32_t calibStartMs = args.calibStartMs ? args.calibStartMs : millis();
    const uint32_t startMs = millis();
    bool failed = false;
    const char* failReason = nullptr;
    uint32_t heatStartAbs = 0;
    uint32_t heatStopAbs = 0;
    float duty = args.dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 0.5f;
    if (duty > 1.0f) duty = 1.0f;
    bool heatStartLocked = false;

    if (CONF) {
        CONF->PutBool(CALIB_FLOOR_RUNNING_KEY, true);
        CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 1);
        if (RTC) {
            CONF->PutInt(CALIB_FLOOR_TS_KEY,
                         static_cast<int>(RTC->getUnixTime()));
        }
    }

    uint32_t ambientEndAbs = 0;
    uint32_t windowStartMs = 0;
    float floorStartC = NAN;
    float roomStartC = NAN;
    uint32_t lastValidMs = 0;

    while (true) {
        if (floorCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }
        const uint32_t nowMs = millis();
        if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        float floorC = NAN;
        if (NTC) {
            NTC->update();
            floorC = NTC->getLastTempC();
        }
        float roomC = NAN;
        if (DEVICE && DEVICE->tempSensor) {
            roomC = DEVICE->tempSensor->getHeatsinkTemp();
        }

        if (!isfinite(floorC) || !isfinite(roomC)) {
            if (lastValidMs == 0) lastValidMs = nowMs;
            if ((nowMs - startMs) >= args.ambientMs &&
                (nowMs - lastValidMs) > 30000) {
                failed = true;
                failReason = ERR_SENSOR_MISSING;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
            continue;
        }

        lastValidMs = nowMs;
        if (windowStartMs == 0) {
            windowStartMs = nowMs;
            floorStartC = floorC;
            roomStartC = roomC;
        }

        const uint32_t windowElapsed = nowMs - windowStartMs;
        if (windowElapsed >= kFloorCalSteadyMsDefault) {
            const double minutes = static_cast<double>(windowElapsed) / 60000.0;
            const double slopeFloor =
                (static_cast<double>(floorC) - static_cast<double>(floorStartC)) / minutes;
            const double slopeRoom =
                (static_cast<double>(roomC) - static_cast<double>(roomStartC)) / minutes;
            const bool stable =
                fabs(slopeFloor) <= kFloorCalStableSlopeCPerMin &&
                fabs(slopeRoom) <= kFloorCalStableSlopeCPerMin;
            if (stable && (nowMs - startMs) >= args.ambientMs) {
                ambientEndAbs = nowMs;
                break;
            }
            windowStartMs = nowMs;
            floorStartC = floorC;
            roomStartC = roomC;
        }

        vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
    }

    if (!failed && ambientEndAbs == 0) {
        failed = true;
        failReason = ERR_TIMEOUT;
    }

    if (!failed && DEVTRAN) {
        if (!DEVTRAN->startEnergyCalibration(args.targetC,
                                             args.wireIndex,
                                             Device::EnergyRunPurpose::FloorCal,
                                             duty)) {
            failed = true;
            failReason = ERR_START_FAILED;
        } else {
            heatStartAbs = millis();
            if (CONF) {
                CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 2);
                if (RTC) {
                    CONF->PutInt(CALIB_FLOOR_TS_KEY,
                                 static_cast<int>(RTC->getUnixTime()));
                }
            }
        }
    }

    if (!failed) {
        while (true) {
            if (floorCalAbortRequested()) {
                failed = true;
                failReason = ERR_STOPPED;
                break;
            }
            const uint32_t nowMs = millis();
            if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
                failed = true;
                failReason = ERR_TIMEOUT;
                break;
            }
            Device::WireTargetStatus st{};
            if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(st)) {
                failed = true;
                failReason = ERR_STATUS_UNAVAILABLE;
                break;
            }
            float floorC = NAN;
            if (NTC) {
                NTC->update();
                floorC = NTC->getLastTempC();
            } else {
                failed = true;
                failReason = ERR_NTC_MISSING;
                break;
            }

            const bool active =
                st.active && st.purpose == Device::EnergyRunPurpose::FloorCal;
            if (!active) {
                bool acceptStop = false;
                if (isfinite(floorC) && isfinite(args.targetC) &&
                    floorC >= args.targetC) {
                    acceptStop = true;
                }
                if (!acceptStop && heatStartAbs > 0 &&
                    (nowMs - heatStartAbs) >= args.heatMs) {
                    acceptStop = true;
                }
                if (acceptStop) {
                    if (DEVTRAN) {
                        DEVTRAN->stopWireTargetTest();
                    }
                    heatStopAbs = millis();
                    break;
                }
                failed = true;
                failReason = ERR_ENERGY_STOPPED;
                break;
            }
            if (!heatStartLocked && st.packetMs > 0 && st.updatedMs > 0) {
                heatStartAbs = st.updatedMs;
                heatStartLocked = true;
            }

            bool stopHeat = false;
            if (isfinite(args.targetC) && isfinite(floorC) &&
                floorC >= args.targetC) {
                stopHeat = true;
            }
            if (!stopHeat && heatStartAbs > 0 &&
                (nowMs - heatStartAbs) >= args.heatMs) {
                stopHeat = true;
            }
            if (stopHeat) {
                if (DEVTRAN) {
                    DEVTRAN->stopWireTargetTest();
                }
                heatStopAbs = millis();
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
        }
    }

    if (DEVTRAN) {
        DEVTRAN->stopWireTargetTest();
    }
    if (!heatStopAbs) heatStopAbs = millis();
    if (heatStartAbs == 0) heatStartAbs = heatStopAbs;

    if (!failed && args.coolMs > 0) {
        if (CONF) {
            CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 3);
            if (RTC) {
                CONF->PutInt(CALIB_FLOOR_TS_KEY,
                             static_cast<int>(RTC->getUnixTime()));
            }
        }
        const uint32_t coolStartMs = millis();
        while (true) {
            if (floorCalAbortRequested()) {
                failed = true;
                failReason = ERR_STOPPED;
                break;
            }
            const uint32_t nowMs = millis();
            if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
                failed = true;
                failReason = ERR_TIMEOUT;
                break;
            }
            if ((nowMs - coolStartMs) >= args.coolMs) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
        }
    }

    if (CALREC) {
        if (failed) {
            CALREC->stop();
        } else {
            CALREC->stopAndSave(5000);
        }
    }

    const uint32_t heatStartMs =
        (heatStartAbs >= calibStartMs) ? (heatStartAbs - calibStartMs) : 0;
    const uint32_t heatStopMs =
        (heatStopAbs >= calibStartMs) ? (heatStopAbs - calibStartMs) : heatStartMs;
    const uint32_t ambientEndMs =
        (ambientEndAbs >= calibStartMs) ? (ambientEndAbs - calibStartMs) : 0;

    if (!failed) {
        double tau = NAN;
        double kLoss = NAN;
        double capC = NAN;
        float roomAmb = NAN;
        float floorInf = NAN;
        float powerW = NAN;
        const char* calcErr = nullptr;

        if (!computeFloorModelFromSamples(ambientEndMs,
                                          heatStartMs,
                                          heatStopMs,
                                          args.wireIndex,
                                          duty,
                                          tau,
                                          kLoss,
                                          capC,
                                          roomAmb,
                                          floorInf,
                                          powerW,
                                          calcErr)) {
            failed = true;
            failReason = calcErr ? calcErr : ERR_FAILED;
        } else if (CONF) {
            CONF->PutDouble(FLOOR_MODEL_TAU_KEY, tau);
            CONF->PutDouble(FLOOR_MODEL_K_KEY, kLoss);
            CONF->PutDouble(FLOOR_MODEL_C_KEY, capC);
            CONF->PutBool(CALIB_FLOOR_DONE_KEY, true);
            CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 4);
            if (RTC) {
                CONF->PutInt(CALIB_FLOOR_TS_KEY,
                             static_cast<int>(RTC->getUnixTime()));
            }
        }
    }

    if (failed) {
        DEBUG_PRINTF("[WiFi] Floor calibration failed: %s\n",
                     failReason ? failReason : ERR_FAILED);
    }

    if (CONF) {
        CONF->PutBool(CALIB_FLOOR_RUNNING_KEY, false);
    }

    s_floorCalTask = nullptr;
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
            WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
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

            uint32_t seqStart = 0;
            uint32_t seqEnd = 0;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 3072, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "items")) return false;
                    CborEncoder items;
                    if (cbor_encoder_create_array(map, &items, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (_snapMtx &&
                        xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(20)) == pdTRUE) {
                        buildLiveBatch(&items, since, seqStart, seqEnd);
                        xSemaphoreGive(_snapMtx);
                    }
                    if (cbor_encoder_close_container(map, &items) != CborNoError) {
                        return false;
                    }
                    if (seqStart != 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "seqStart", seqStart)) return false;
                        if (!WiFiCbor::encodeKvUInt(map, "seqEnd", seqEnd)) return false;
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 256, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvText(map, "deviceId",
                                                CONF->GetString(DEV_ID_KEY, ""))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvText(map, "sw",
                                                CONF->GetString(DEV_SW_KEY,
                                                                DEVICE_SW_VERSION))) {
                        return false;
                    }
                    return WiFiCbor::encodeKvText(map, "hw",
                                                  CONF->GetString(DEV_HW_KEY,
                                                                  DEVICE_HW_VERSION));
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
            collectCborBody_(request, data, len, index, total,
                [this](AsyncWebServerRequest* request, const std::vector<uint8_t>& body) {
                    String username;
                    String password;
                    const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                        if (strcmp(key, "username") == 0) {
                            return readCborText_(it, username);
                        }
                        if (strcmp(key, "password") == 0) {
                            return readCborText_(it, password);
                        }
                        return skipCborValue_(it);
                    });
                    if (!parsed) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }

                    if (username.isEmpty() || password.isEmpty()) {
                        WiFiCbor::sendError(request, 400, ERR_MISSING_FIELDS);
                        return;
                    }

                    if (wifiStatus != WiFiStatus::NotConnected) {
                        WiFiCbor::sendError(request, 403, ERR_ALREADY_CONNECTED);
                        return;
                    }

                    String adminUser = CONF->GetString(ADMIN_ID_KEY, "");
                    String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
                    String userUser  = CONF->GetString(USER_ID_KEY, "");
                    String userPass  = CONF->GetString(USER_PASS_KEY, "");

                    auto sendLogin = [&](const char* role) {
                        const String token =
                            issueSessionToken_(request->client()->remoteIP());
                        std::vector<uint8_t> payload;
                        if (!WiFiCbor::buildMapPayload(payload, 128, [&](CborEncoder* map) {
                                if (!WiFiCbor::encodeKvBool(map, "ok", true)) return false;
                                if (!WiFiCbor::encodeKvText(map, "role", role)) return false;
                                return WiFiCbor::encodeKvText(map, "token", token);
                            })) {
                            request->send(500, CT_TEXT_PLAIN, "error");
                            return;
                        }
                        WiFiCbor::sendPayload(request, 200, payload);
                    };

                    if (username == adminUser && password == adminPass) {
                        BUZZ->successSound();
                        onAdminConnected();
                        RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
                        sendLogin("admin");
                        return;
                    }
                    if (username == userUser && password == userPass) {
                        const bool setupDone =
                            CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
                        if (!setupDone) {
                            WiFiCbor::sendError(request, 403, ERR_SETUP_REQUIRED);
                            return;
                        }
                        BUZZ->successSound();
                        onUserConnected();
                        RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
                        sendLogin("user");
                        return;
                    }

                    BUZZ->bipFault();
                    WiFiCbor::sendError(request, 401, ERR_BAD_PASSWORD);
                });
        }
    );

    // ---- Session history (CBOR) ----
    server.on(EP_SESSION_HISTORY, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }
            const uint16_t count = POWER_TRACKER->getHistoryCount();
            const size_t capacity = 256 + (size_t)count * 80;

            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "history")) return false;
                    CborEncoder arr;
                    if (cbor_encoder_create_array(map, &arr, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }

                    for (uint16_t i = 0; i < count; ++i) {
                        PowerTracker::HistoryEntry h;
                        if (!POWER_TRACKER->getHistoryEntry(i, h) || !h.valid) continue;
                        CborEncoder row;
                        if (cbor_encoder_create_map(&arr, &row, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvUInt(&row, "start_ms", h.startMs)) return false;
                        if (!WiFiCbor::encodeKvUInt(&row, "duration_s", h.stats.duration_s)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "energy_Wh", h.stats.energy_Wh)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "peakPower_W", h.stats.peakPower_W)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "peakCurrent_A", h.stats.peakCurrent_A)) return false;
                        if (cbor_encoder_close_container(&arr, &row) != CborNoError) {
                            return false;
                        }
                    }

                    return cbor_encoder_close_container(map, &arr) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
            sendOk_(request);
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
                WiFiCbor::sendError(request, 403, ERR_NOT_AUTHENTICATED);
                return;
            }
            if (lock()) { lastActivityMillis = millis(); unlock(); }
            collectCborBody_(request, data, len, index, total,
                [this](AsyncWebServerRequest* request, const std::vector<uint8_t>& body) {
                    if (!isAuthenticated(request)) {
                        return;
                    }
                    String newSsid;
                    String newPass;
                    const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                        if (strcmp(key, "apSSID") == 0) {
                            return readCborText_(it, newSsid);
                        }
                        if (strcmp(key, "apPassword") == 0) {
                            return readCborText_(it, newPass);
                        }
                        return skipCborValue_(it);
                    });
                    if (!parsed) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }

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

                    sendStatusApplied_(request);

                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                });
        }
    );

    // ---- Calibration recorder status ----
    server.on(EP_CALIB_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const CalibrationRecorder::Meta meta = CALREC->getMeta();
            const char* modeStr =
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                (meta.mode == CalibrationRecorder::Mode::Floor) ? MODE_FLOOR :
                MODE_NONE;

            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 256, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "running", meta.running)) return false;
                    if (!WiFiCbor::encodeKvText(map, "mode", modeStr)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "count", meta.count)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "capacity", meta.capacity)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "interval_ms", meta.intervalMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "start_ms", meta.startMs)) return false;
                    if (meta.startEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "start_epoch", meta.startEpoch)) return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "saved", meta.saved)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "saved_ms", meta.savedMs)) return false;
                    if (meta.savedEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "saved_epoch", meta.savedEpoch)) return false;
                    }
                    if (isfinite(meta.targetTempC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "target_c", meta.targetTempC)) return false;
                    }
                    if (meta.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "wire_index", meta.wireIndex)) return false;
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            String modeStr;
            uint32_t intervalMs = CalibrationRecorder::kDefaultIntervalMs;
            uint16_t maxSamples = CalibrationRecorder::kDefaultMaxSamples;
            uint32_t floorAmbientMs = 0;
            uint32_t floorHeatMs = 0;
            uint32_t floorTimeoutMs = 0;
            uint32_t floorCoolMs = 0;
            float floorDuty = NAN;
            float modelDuty = NAN;
            float targetC = NAN;
            uint32_t epoch = 0;
            uint8_t wireIndex = getNtcGateIndexFromConfig();
            double dutyVal = NAN;
            double dutyPctVal = NAN;

            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "mode") == 0) {
                    String tmp;
                    if (!readCborText_(it, tmp)) return false;
                    tmp.toLowerCase();
                    modeStr = tmp;
                    return true;
                }
                if (strcmp(key, "interval_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    intervalMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "max_samples") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    maxSamples = static_cast<uint16_t>(v);
                    return true;
                }
                if (strcmp(key, "ambient_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorAmbientMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "heat_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorHeatMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "timeout_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorTimeoutMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "cool_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorCoolMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "duty") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    dutyVal = v;
                    return true;
                }
                if (strcmp(key, "duty_pct") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    dutyPctVal = v;
                    return true;
                }
                if (strcmp(key, "target_c") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    targetC = static_cast<float>(v);
                    return true;
                }
                if (strcmp(key, "epoch") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    epoch = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "wire_index") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    wireIndex = static_cast<uint8_t>(v);
                    return true;
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            auto sendCalibError = [&](int status,
                                      const char* error,
                                      const String& detail,
                                      const char* state) {
                const char* detailStr = detail.length() ? detail.c_str() : nullptr;
                WiFiCbor::sendError(request, status, error, detailStr, state);
            };

            CalibrationRecorder::Mode mode = CalibrationRecorder::Mode::None;
            if (modeStr == MODE_NTC) mode = CalibrationRecorder::Mode::Ntc;
            else if (modeStr == MODE_MODEL) mode = CalibrationRecorder::Mode::Model;
            else if (modeStr == MODE_FLOOR) mode = CalibrationRecorder::Mode::Floor;

            if (mode == CalibrationRecorder::Mode::None) {
                sendCalibError(400, ERR_INVALID_MODE, "", nullptr);
                return;
            }
            if (!BUS_SAMPLER) {
                sendCalibError(503, ERR_BUS_SAMPLER_MISSING, "", nullptr);
                return;
            }
            if (CALREC && CALREC->isRunning()) {
                sendCalibError(409, ERR_ALREADY_RUNNING, "", nullptr);
                return;
            }
            if (s_ntcCalTask != nullptr ||
                s_modelCalTask != nullptr ||
                s_floorCalTask != nullptr) {
                sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                return;
            }

            if (epoch > 0 && RTC) {
                RTC->setUnixTime(epoch);
            }
            const uint8_t ntcGate = getNtcGateIndexFromConfig();
            if (mode == CalibrationRecorder::Mode::Ntc) {
                wireIndex = ntcGate;
            }
            if (CONF) {
                CONF->PutInt(SETUP_WIRE_INDEX_KEY, static_cast<int>(wireIndex));
            }

            if (mode == CalibrationRecorder::Mode::Model) {
                if (isfinite(dutyVal)) {
                    modelDuty = static_cast<float>(dutyVal);
                } else if (isfinite(dutyPctVal)) {
                    modelDuty = static_cast<float>(dutyPctVal * 0.01f);
                }
                if (!isfinite(modelDuty) || modelDuty <= 0.0f) {
                    modelDuty = 1.0f;
                }
                if (modelDuty > 1.0f) modelDuty = 1.0f;
                if (modelDuty < 0.05f) modelDuty = 0.05f;
            }

            if (mode == CalibrationRecorder::Mode::Floor) {
                if (floorAmbientMs == 0) floorAmbientMs = kFloorCalAmbientMsDefault;
                if (floorHeatMs == 0) floorHeatMs = kFloorCalHeatMsDefault;
                if (floorTimeoutMs == 0) floorTimeoutMs = kFloorCalTimeoutMsDefault;
                if (floorCoolMs == 0) floorCoolMs = kFloorCalCoolMsDefault;
                if (isfinite(dutyVal)) {
                    floorDuty = static_cast<float>(dutyVal);
                } else if (isfinite(dutyPctVal)) {
                    floorDuty = static_cast<float>(dutyPctVal * 0.01f);
                }

                if (floorAmbientMs < 10000) floorAmbientMs = 10000;
                if (floorHeatMs < 10000) floorHeatMs = 10000;
                if (floorCoolMs > (30u * 60u * 1000u)) {
                    floorCoolMs = 30u * 60u * 1000u;
                }
                const uint32_t totalFloorMs = floorAmbientMs + floorHeatMs + floorCoolMs;
                if (floorTimeoutMs < totalFloorMs) {
                    floorTimeoutMs = totalFloorMs + 60000;
                }
                if (floorTimeoutMs > (2u * 60u * 60u * 1000u)) {
                    floorTimeoutMs = 2u * 60u * 60u * 1000u;
                }
                if (!isfinite(floorDuty) || floorDuty <= 0.0f) {
                    floorDuty = 0.5f;
                }
                if (floorDuty > 1.0f) floorDuty = 1.0f;
                if (floorDuty < 0.05f) floorDuty = 0.05f;

                if (!isfinite(targetC) || targetC <= 0.0f) {
                    float floorMax = DEFAULT_FLOOR_MAX_C;
                    if (CONF) {
                        floorMax = CONF->GetFloat(FLOOR_MAX_C_KEY,
                                                  DEFAULT_FLOOR_MAX_C);
                    }
                    if (!isfinite(floorMax) || floorMax <= 0.0f) {
                        floorMax = DEFAULT_FLOOR_MAX_C;
                    }
                    targetC = floorMax;
                }

                if (intervalMs < 50) intervalMs = 50;
                if (intervalMs > 5000) intervalMs = 5000;
                if (maxSamples == 0) maxSamples = CalibrationRecorder::kDefaultMaxSamples;
                if (maxSamples > CalibrationRecorder::kAbsoluteMaxSamples) {
                    maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                }

                const uint32_t totalMs = floorAmbientMs + floorHeatMs + floorCoolMs;
                if (intervalMs > 0 && totalMs > 0) {
                    uint32_t required = (totalMs / intervalMs) + 4;
                    if (required > maxSamples) {
                        if (required <= CalibrationRecorder::kAbsoluteMaxSamples) {
                            maxSamples = static_cast<uint16_t>(required);
                        } else {
                            const uint32_t minInterval =
                                (totalMs / (CalibrationRecorder::kAbsoluteMaxSamples - 1)) + 1;
                            if (intervalMs < minInterval) intervalMs = minInterval;
                            if (intervalMs > 5000) intervalMs = 5000;
                            maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                        }
                    }
                }
            }

            const bool ok = CALREC->start(mode, intervalMs, maxSamples, targetC, wireIndex);
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
                    CALREC->stop();
                    sendCalibError(503, ERR_DEVICE_TRANSPORT_MISSING, "", nullptr);
                    return;
                }
                DeviceState lastState = DeviceState::Shutdown;
                if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                    CALREC->stop();
                    String detail = (lastState == DeviceState::Shutdown) ? "wake_timeout" : "";
                    sendCalibError(409, ERR_DEVICE_NOT_IDLE, detail, stateName(lastState));
                    return;
                }
                if (!WIRE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_WIRE_SUBSYSTEM_MISSING, "", nullptr);
                    return;
                }
                if (CONF && DEVICE) {
                    if (!DEVICE->getWireConfigStore().getAccessFlag(wireIndex)) {
                        CALREC->stop();
                        sendCalibError(403, ERR_WIRE_ACCESS_BLOCKED,
                                       String("wire=") + String(wireIndex), nullptr);
                        return;
                    }
                }
                if (!DEVTRAN->startEnergyCalibration(runTargetC,
                                                     wireIndex,
                                                     Device::EnergyRunPurpose::ModelCal,
                                                     modelDuty)) {
                    CALREC->stop();
                    sendCalibError(500, ERR_ENERGY_START_FAILED, "", nullptr);
                    return;
                }
                if (s_modelCalTask != nullptr) {
                    DEVTRAN->stopWireTargetTest();
                    CALREC->stop();
                    sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                    return;
                }
                s_modelCalAbort = false;
                ModelCalTaskArgs* args = new ModelCalTaskArgs();
                if (!args) {
                    DEVTRAN->stopWireTargetTest();
                    CALREC->stop();
                    sendCalibError(500, ERR_ALLOC_FAILED, "", nullptr);
                    return;
                }
                args->targetC = runTargetC;
                args->wireIndex = wireIndex;
                args->dutyFrac = modelDuty;
                args->timeoutMs = kModelCalTimeoutMs;
                args->startMs = millis();
                args->calibStartMs = CALREC->getMeta().startMs;

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
                    CALREC->stop();
                    sendCalibError(500, ERR_TASK_FAILED, "", nullptr);
                    return;
                }
            }
            else if (mode == CalibrationRecorder::Mode::Floor) {
                if (!DEVTRAN || !DEVICE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_DEVICE_MISSING, "", nullptr);
                    return;
                }
                if (!NTC || !DEVICE->tempSensor) {
                    CALREC->stop();
                    sendCalibError(503, ERR_SENSOR_MISSING, "", nullptr);
                    return;
                }
                DeviceState lastState = DeviceState::Shutdown;
                if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                    CALREC->stop();
                    String detail = (lastState == DeviceState::Shutdown) ? "wake_timeout" : "";
                    sendCalibError(409, ERR_DEVICE_NOT_IDLE, detail, stateName(lastState));
                    return;
                }
                if (!WIRE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_WIRE_SUBSYSTEM_MISSING, "", nullptr);
                    return;
                }
                if (CONF && DEVICE) {
                    if (!DEVICE->getWireConfigStore().getAccessFlag(wireIndex)) {
                        CALREC->stop();
                        sendCalibError(403, ERR_WIRE_ACCESS_BLOCKED,
                                       String("wire=") + String(wireIndex), nullptr);
                        return;
                    }
                }
                auto wi = WIRE->getWireInfo(wireIndex);
                if (!wi.connected) {
                    CALREC->stop();
                    sendCalibError(400, ERR_WIRE_NOT_CONNECTED,
                                   String("wire=") + String(wireIndex), nullptr);
                    return;
                }
                if (s_floorCalTask != nullptr) {
                    CALREC->stop();
                    sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                    return;
                }

                s_floorCalAbort = false;
                FloorCalTaskArgs* args = new FloorCalTaskArgs();
                if (!args) {
                    CALREC->stop();
                    sendCalibError(500, ERR_ALLOC_FAILED, "", nullptr);
                    return;
                }
                args->targetC = targetC;
                args->wireIndex = wireIndex;
                args->ambientMs = floorAmbientMs ? floorAmbientMs : kFloorCalAmbientMsDefault;
                args->heatMs = floorHeatMs ? floorHeatMs : kFloorCalHeatMsDefault;
                args->timeoutMs = floorTimeoutMs ? floorTimeoutMs : kFloorCalTimeoutMsDefault;
                args->coolMs = floorCoolMs ? floorCoolMs : kFloorCalCoolMsDefault;
                args->dutyFrac = floorDuty;
                args->calibStartMs = CALREC->getMeta().startMs;

                BaseType_t okTask = xTaskCreate(
                    floorCalTask,
                    "FloorCal",
                    4096,
                    args,
                    2,
                    &s_floorCalTask
                );
                if (okTask != pdPASS) {
                    delete args;
                    s_floorCalTask = nullptr;
                    CALREC->stop();
                    sendCalibError(500, ERR_TASK_FAILED, "", nullptr);
                    return;
                }
            }

            sendStatusRunning_(request, true);
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

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                uint32_t epoch = 0;
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "epoch") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        epoch = static_cast<uint32_t>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (parsed && epoch > 0 && RTC) {
                    RTC->setUnixTime(epoch);
                }
            }
            body.clear();

            const bool saved = CALREC->stopAndSave();
            modelCalRequestAbort();
            floorCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            sendStatusRunningSaved_(request, saved);
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
            CALREC->clear();
            modelCalRequestAbort();
            floorCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }

            bool removed = false;
            size_t removedCount = 0;
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(CALIB_MODEL_CBOR_FILE)) {
                    removed = SPIFFS.remove(CALIB_MODEL_CBOR_FILE);
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

            sendStatusClearedFile_(request, removed, removedCount);
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

            const CalibrationRecorder::Meta meta = CALREC->getMeta();
            const uint16_t total = meta.count;

            const size_t capacity = 4096 + (size_t)count * 160;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "meta")) return false;
                    CborEncoder metaMap;
                    if (cbor_encoder_create_map(map, &metaMap, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    const char* modeStr =
                        (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                        (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                        (meta.mode == CalibrationRecorder::Mode::Floor) ? MODE_FLOOR :
                        MODE_NONE;
                    if (!WiFiCbor::encodeKvText(&metaMap, "mode", modeStr)) return false;
                    if (!WiFiCbor::encodeKvBool(&metaMap, "running", meta.running)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "count", total)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "capacity", meta.capacity)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "interval_ms", meta.intervalMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "start_ms", meta.startMs)) return false;
                    if (meta.startEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "start_epoch", meta.startEpoch)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvBool(&metaMap, "saved", meta.saved)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "saved_ms", meta.savedMs)) return false;
                    if (meta.savedEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "saved_epoch", meta.savedEpoch)) {
                            return false;
                        }
                    }
                    if (isfinite(meta.targetTempC)) {
                        if (!WiFiCbor::encodeKvFloat(&metaMap, "target_c", meta.targetTempC)) {
                            return false;
                        }
                    }
                    if (meta.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "wire_index", meta.wireIndex)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "offset", offset)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "limit", count)) return false;
                    if (cbor_encoder_close_container(map, &metaMap) != CborNoError) {
                        return false;
                    }

                    if (!WiFiCbor::encodeText(map, "samples")) return false;
                    CborEncoder samples;
                    if (cbor_encoder_create_array(map, &samples, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }

                    CalibrationRecorder::Sample buf[32];
                    uint16_t copied = 0;
                    while (copied < count) {
                        const size_t chunk = (count - copied) < 32 ? (count - copied) : 32;
                        const size_t got = CALREC->copySamples(offset + copied, buf, chunk);
                        if (got == 0) break;
                        for (size_t i = 0; i < got; ++i) {
                            CborEncoder row;
                            if (cbor_encoder_create_map(&samples, &row, CborIndefiniteLength) != CborNoError) {
                                return false;
                            }
                            if (!WiFiCbor::encodeKvUInt(&row, "t_ms", buf[i].tMs)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "v", buf[i].voltageV)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "i", buf[i].currentA)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "temp_c", buf[i].tempC)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "room_c", buf[i].roomTempC)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "ntc_v", buf[i].ntcVolts)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "ntc_ohm", buf[i].ntcOhm)) return false;
                            if (!WiFiCbor::encodeKvInt(&row, "ntc_adc", buf[i].ntcAdc)) return false;
                            if (!WiFiCbor::encodeKvBool(&row, "ntc_ok", buf[i].ntcValid)) return false;
                            if (!WiFiCbor::encodeKvBool(&row, "pressed", buf[i].pressed)) return false;
                            if (cbor_encoder_close_container(&samples, &row) != CborNoError) {
                                return false;
                            }
                        }
                        copied += got;
                    }

                    return cbor_encoder_close_container(map, &samples) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Calibration recorder file (CBOR) ----
    server.on(EP_CALIB_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!SPIFFS.begin(false) || !SPIFFS.exists(CALIB_MODEL_CBOR_FILE)) {
                WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
                return;
            }
            request->send(SPIFFS, CALIB_MODEL_CBOR_FILE, CT_APP_CBOR);
        }
    );

    // ---- Calibration history list (CBOR) ----
    server.on(EP_CALIB_HISTORY_LIST, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            std::vector<String> names;
            std::vector<uint32_t> epochs;

            if (SPIFFS.begin(false)) {
                auto addItem = [&](const String& rawName) {
                    String fullName;
                    String baseName;
                    uint32_t epoch = 0;
                    if (!normalizeHistoryPath(rawName, fullName, baseName, &epoch)) return;
                    for (const auto& existing : names) {
                        if (fullName == existing) return;
                    }
                    names.push_back(fullName);
                    epochs.push_back(epoch);
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

            const size_t capacity = 256 + names.size() * 128;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "items")) return false;
                    CborEncoder items;
                    if (cbor_encoder_create_array(map, &items, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (size_t i = 0; i < names.size(); ++i) {
                        CborEncoder row;
                        if (cbor_encoder_create_map(&items, &row, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvText(&row, "name", names[i])) return false;
                        if (epochs[i] > 0) {
                            if (!WiFiCbor::encodeKvUInt(&row, "start_epoch", epochs[i])) return false;
                        }
                        if (cbor_encoder_close_container(&items, &row) != CborNoError) {
                            return false;
                        }
                    }
                    return cbor_encoder_close_container(map, &items) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Calibration history file (CBOR) ----
    server.on(EP_CALIB_HISTORY_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!request->hasParam("name")) {
                WiFiCbor::sendError(request, 400, ERR_MISSING_NAME);
                return;
            }
            String name = request->getParam("name")->value();
            String fullName;
            String baseName;
            if (!normalizeHistoryPath(name, fullName, baseName, nullptr)) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_NAME);
                return;
            }
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(fullName)) {
                    request->send(SPIFFS, fullName, CT_APP_CBOR);
                    return;
                }
                String legacyPath = "/" + baseName;
                if (legacyPath != fullName && SPIFFS.exists(legacyPath)) {
                    request->send(SPIFFS, legacyPath, CT_APP_CBOR);
                    return;
                }
            }
            WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
        }
    );

    // ---- Wire target test status ----
    server.on(EP_WIRE_TEST_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Device::WireTargetStatus st{};
            if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(st)) {
                WiFiCbor::sendError(request, 503, ERR_STATUS_UNAVAILABLE);
                return;
            }

            const char* purpose = PURPOSE_NONE;
            switch (st.purpose) {
                case Device::EnergyRunPurpose::WireTest: purpose = PURPOSE_WIRE_TEST; break;
                case Device::EnergyRunPurpose::ModelCal: purpose = PURPOSE_MODEL_CAL; break;
                case Device::EnergyRunPurpose::NtcCal:   purpose = PURPOSE_NTC_CAL; break;
                case Device::EnergyRunPurpose::FloorCal: purpose = PURPOSE_FLOOR_CAL; break;
                default: break;
            }
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 256, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "running", st.active)) return false;
                    if (isfinite(st.targetC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "target_c", st.targetC)) return false;
                    }
                    if (st.activeWire > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "active_wire", st.activeWire)) return false;
                    }
                    if (isfinite(st.ntcTempC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntc_temp_c", st.ntcTempC)) return false;
                    }
                    if (isfinite(st.activeTempC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "active_temp_c", st.activeTempC)) return false;
                    }
                    if (!WiFiCbor::encodeKvUInt(map, "packet_ms", st.packetMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "frame_ms", st.frameMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "updated_ms", st.updatedMs)) return false;
                    if (!WiFiCbor::encodeKvText(map, "mode", MODE_ENERGY)) return false;
                    return WiFiCbor::encodeKvText(map, "purpose", purpose);
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            float targetC = NAN;
            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "target_c") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    targetC = static_cast<float>(v);
                    return true;
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            if (!isfinite(targetC) || targetC <= 0.0f) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_TARGET);
                return;
            }

            if (!DEVTRAN) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }
            if (!WIRE) {
                WiFiCbor::sendError(request, 503, ERR_WIRE_SUBSYSTEM_MISSING);
                return;
            }
            const uint8_t wireIndex = getNtcGateIndexFromConfig();
            DeviceState lastState = DeviceState::Shutdown;
            if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }
            if (!DEVTRAN->startWireTargetTest(targetC, wireIndex)) {
                WiFiCbor::sendError(request, 400, ERR_START_FAILED);
                return;
            }

            sendStatusRunning_(request, true);
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
            sendStatusRunning_(request, false);
        }
    );

    // ---- Presence probe (admin-only) ----
    server.on(EP_PRESENCE_PROBE, HTTP_POST,
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
                WiFiCbor::sendError(request, 403, ERR_NOT_AUTHENTICATED);
                return;
            }
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            float minDropV = CONF->GetFloat(PRESENCE_MIN_DROP_V_KEY,
                                            DEFAULT_PRESENCE_MIN_DROP_V);

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "presenceMinDropV") == 0) {
                        double v = NAN;
                        if (!readCborDouble_(it, v)) return false;
                        minDropV = static_cast<float>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (!isfinite(minDropV) || minDropV <= 0.0f) {
                minDropV = DEFAULT_PRESENCE_MIN_DROP_V;
            }
            if (minDropV < 5.0f) minDropV = 5.0f;
            if (minDropV > 100.0f) minDropV = 100.0f;

            CONF->PutFloat(PRESENCE_MIN_DROP_V_KEY, minDropV);

            if (!DEVTRAN) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }

            DeviceState lastState = DeviceState::Shutdown;
            if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }

            if (!DEVTRAN->probeWirePresence()) {
                WiFiCbor::sendError(request, 500, ERR_FAILED);
                return;
            }

            CONF->PutBool(CALIB_PRESENCE_DONE_KEY, true);
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 256, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvText(map, "status", STATUS_OK)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "calibrated", true)) return false;
                    if (!WiFiCbor::encodeText(map, "wirePresent")) return false;
                    CborEncoder present;
                    if (cbor_encoder_create_array(map, &present, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
                        if (cbor_encode_boolean(&present,
                                                WIRE ? WIRE->getWireInfo(i).connected : false)
                            != CborNoError) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &present) != CborNoError) {
                        return false;
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Setup wizard status ----
    server.on(EP_SETUP_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const bool setupDone =
                CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
            const int stage = CONF->GetInt(SETUP_STAGE_KEY, DEFAULT_SETUP_STAGE);
            const int substage = CONF->GetInt(SETUP_SUBSTAGE_KEY, DEFAULT_SETUP_SUBSTAGE);
            const int wireIndex = CONF->GetInt(SETUP_WIRE_INDEX_KEY, DEFAULT_SETUP_WIRE_INDEX);

            std::vector<const char*> missingConfig;
            std::vector<const char*> missingCalib;
            const bool configOk = checkSetupConfig(&missingConfig);
            const bool calibOk = checkSetupCalib(&missingCalib);

            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 1024, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "setupDone", setupDone)) return false;
                    if (!WiFiCbor::encodeKvInt(map, "stage", stage)) return false;
                    if (!WiFiCbor::encodeKvInt(map, "substage", substage)) return false;
                    if (!WiFiCbor::encodeKvInt(map, "wireIndex", wireIndex)) return false;

                    if (!WiFiCbor::encodeText(map, "missingConfig")) return false;
                    CborEncoder missingCfg;
                    if (cbor_encoder_create_array(map, &missingCfg, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (const auto* key : missingConfig) {
                        if (!WiFiCbor::encodeText(&missingCfg, key)) return false;
                    }
                    if (cbor_encoder_close_container(map, &missingCfg) != CborNoError) {
                        return false;
                    }

                    if (!WiFiCbor::encodeText(map, "missingCalib")) return false;
                    CborEncoder missingCal;
                    if (cbor_encoder_create_array(map, &missingCal, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (const auto* key : missingCalib) {
                        if (!WiFiCbor::encodeText(&missingCal, key)) return false;
                    }
                    if (cbor_encoder_close_container(map, &missingCal) != CborNoError) {
                        return false;
                    }

                    if (!WiFiCbor::encodeKvBool(map, "configOk", configOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "calibOk", calibOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "ready", configOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "runAllowed", setupDone && configOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "calibPending",
                                                setupDone && configOk && !calibOk)) return false;

                    if (!WiFiCbor::encodeText(map, "wireStage")) return false;
                    CborEncoder wireStage;
                    if (cbor_encoder_create_map(map, &wireStage, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (!WiFiCbor::encodeText(map, "wireRunning")) return false;
                    CborEncoder wireRunning;
                    if (cbor_encoder_create_map(map, &wireRunning, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (!WiFiCbor::encodeText(map, "wireCalibrated")) return false;
                    CborEncoder wireCal;
                    if (cbor_encoder_create_map(map, &wireCal, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (int i = 0; i < HeaterManager::kWireCount; ++i) {
                        char key[4];
                        snprintf(key, sizeof(key), "%d", i + 1);
                        if (!WiFiCbor::encodeKvInt(&wireStage, key,
                                                   CONF->GetInt(kWireCalibStageKeys[i],
                                                                DEFAULT_CALIB_W_STAGE))) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvBool(&wireRunning, key,
                                                    CONF->GetBool(kWireCalibRunKeys[i],
                                                                  DEFAULT_CALIB_W_RUNNING))) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvBool(&wireCal, key,
                                                    CONF->GetBool(kWireCalibDoneKeys[i],
                                                                  DEFAULT_CALIB_W_DONE))) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &wireStage) != CborNoError) return false;
                    if (cbor_encoder_close_container(map, &wireRunning) != CborNoError) return false;
                    if (cbor_encoder_close_container(map, &wireCal) != CborNoError) return false;

                    if (!WiFiCbor::encodeKvInt(map, "floorStage",
                                               CONF->GetInt(CALIB_FLOOR_STAGE_KEY,
                                                            DEFAULT_CALIB_FLOOR_STAGE))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "floorRunning",
                                                CONF->GetBool(CALIB_FLOOR_RUNNING_KEY,
                                                              DEFAULT_CALIB_FLOOR_RUNNING))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "floorCalibrated",
                                                CONF->GetBool(CALIB_FLOOR_DONE_KEY,
                                                              DEFAULT_CALIB_FLOOR_DONE))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "presenceCalibrated",
                                                CONF->GetBool(CALIB_PRESENCE_DONE_KEY,
                                                              DEFAULT_CALIB_PRESENCE_DONE))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "capCalibrated",
                                                CONF->GetBool(CALIB_CAP_DONE_KEY,
                                                              DEFAULT_CALIB_CAP_DONE))) {
                        return false;
                    }

                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Setup wizard progress update (admin-only) ----
    server.on(EP_SETUP_UPDATE, HTTP_POST,
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
                WiFiCbor::sendError(request, 403, ERR_NOT_AUTHENTICATED);
                return;
            }
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            bool setupDoneReq = false;
            bool setupDoneHas = false;
            int stage = DEFAULT_SETUP_STAGE;
            bool stageHas = false;
            int substage = DEFAULT_SETUP_SUBSTAGE;
            bool substageHas = false;
            int wireIndex = DEFAULT_SETUP_WIRE_INDEX;
            bool wireIndexHas = false;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "setup_done") == 0) {
                        if (!readCborBool_(it, setupDoneReq)) return false;
                        setupDoneHas = true;
                        return true;
                    }
                    if (strcmp(key, "stage") == 0) {
                        int64_t v = 0;
                        if (!readCborInt64_(it, v)) return false;
                        stage = static_cast<int>(v);
                        stageHas = true;
                        return true;
                    }
                    if (strcmp(key, "substage") == 0) {
                        int64_t v = 0;
                        if (!readCborInt64_(it, v)) return false;
                        substage = static_cast<int>(v);
                        substageHas = true;
                        return true;
                    }
                    if (strcmp(key, "wire_index") == 0) {
                        int64_t v = 0;
                        if (!readCborInt64_(it, v)) return false;
                        wireIndex = static_cast<int>(v);
                        wireIndexHas = true;
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (setupDoneHas && setupDoneReq) {
                const bool configOk = checkSetupConfig(nullptr);
                if (!configOk) {
                    WiFiCbor::sendError(request, 409, ERR_SETUP_INCOMPLETE);
                    return;
                }
            }

            if (setupDoneHas) {
                CONF->PutBool(SETUP_DONE_KEY, setupDoneReq);
            }
            if (stageHas) {
                const int v = (stage < 0) ? 0 : stage;
                CONF->PutInt(SETUP_STAGE_KEY, v);
            }
            if (substageHas) {
                const int v = (substage < 0) ? 0 : substage;
                CONF->PutInt(SETUP_SUBSTAGE_KEY, v);
            }
            if (wireIndexHas) {
                int v = wireIndex;
                if (v < 0) v = 0;
                if (v > HeaterManager::kWireCount) v = HeaterManager::kWireCount;
                CONF->PutInt(SETUP_WIRE_INDEX_KEY, v);
            }

            const bool configOk = checkSetupConfig(nullptr);
            const bool calibOk = checkSetupCalib(nullptr);
            const bool setupDone = CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 192, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "ok", true)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupDone", setupDone)) return false;
                    if (!WiFiCbor::encodeKvInt(map, "stage",
                                               CONF->GetInt(SETUP_STAGE_KEY,
                                                            DEFAULT_SETUP_STAGE))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvInt(map, "substage",
                                               CONF->GetInt(SETUP_SUBSTAGE_KEY,
                                                            DEFAULT_SETUP_SUBSTAGE))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvInt(map, "wireIndex",
                                               CONF->GetInt(SETUP_WIRE_INDEX_KEY,
                                                            DEFAULT_SETUP_WIRE_INDEX))) {
                        return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "configOk", configOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "calibOk", calibOk)) return false;
                    return WiFiCbor::encodeKvBool(map, "calibPending",
                                                  setupDone && configOk && !calibOk);
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Setup wizard reset (admin-only) ----
    server.on(EP_SETUP_RESET, HTTP_POST,
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
                WiFiCbor::sendError(request, 403, ERR_NOT_AUTHENTICATED);
                return;
            }
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            bool clearModels = false;
            bool clearWireParams = false;
            bool clearFloorParams = false;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "clear_models") == 0) {
                        if (!readCborBool_(it, clearModels)) return false;
                        return true;
                    }
                    if (strcmp(key, "clear_wire_params") == 0) {
                        if (!readCborBool_(it, clearWireParams)) return false;
                        return true;
                    }
                    if (strcmp(key, "clear_floor_params") == 0) {
                        if (!readCborBool_(it, clearFloorParams)) return false;
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (CALREC && CALREC->isRunning()) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }
            if (s_ntcCalTask != nullptr ||
                s_modelCalTask != nullptr ||
                s_floorCalTask != nullptr) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            if (clearModels) {
                clearWireParams = true;
                clearFloorParams = true;
            }

            CONF->PutBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
            CONF->PutInt(SETUP_STAGE_KEY, DEFAULT_SETUP_STAGE);
            CONF->PutInt(SETUP_SUBSTAGE_KEY, DEFAULT_SETUP_SUBSTAGE);
            CONF->PutInt(SETUP_WIRE_INDEX_KEY, DEFAULT_SETUP_WIRE_INDEX);

            CONF->PutBool(CALIB_CAP_DONE_KEY, DEFAULT_CALIB_CAP_DONE);
            CONF->PutBool(CALIB_NTC_DONE_KEY, DEFAULT_CALIB_NTC_DONE);
            CONF->PutBool(CALIB_PRESENCE_DONE_KEY, DEFAULT_CALIB_PRESENCE_DONE);
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                CONF->PutBool(kWireCalibDoneKeys[i], DEFAULT_CALIB_W_DONE);
                CONF->PutInt(kWireCalibStageKeys[i], DEFAULT_CALIB_W_STAGE);
                CONF->PutBool(kWireCalibRunKeys[i], DEFAULT_CALIB_W_RUNNING);
                CONF->PutInt(kWireCalibTsKeys[i], DEFAULT_CALIB_W_TS);
            }
            CONF->PutBool(CALIB_FLOOR_DONE_KEY, DEFAULT_CALIB_FLOOR_DONE);
            CONF->PutInt(CALIB_FLOOR_STAGE_KEY, DEFAULT_CALIB_FLOOR_STAGE);
            CONF->PutBool(CALIB_FLOOR_RUNNING_KEY, DEFAULT_CALIB_FLOOR_RUNNING);
            CONF->PutInt(CALIB_FLOOR_TS_KEY, DEFAULT_CALIB_FLOOR_TS);
            CONF->PutFloat(PRESENCE_MIN_DROP_V_KEY, DEFAULT_PRESENCE_MIN_DROP_V);
            CONF->PutFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);

            if (clearWireParams) {
                for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                    CONF->PutDouble(kWireModelTauKeys[i], DEFAULT_WIRE_MODEL_TAU);
                    CONF->PutDouble(kWireModelKKeys[i], DEFAULT_WIRE_MODEL_K);
                    CONF->PutDouble(kWireModelCKeys[i], DEFAULT_WIRE_MODEL_C);
                }
            }
            if (clearFloorParams) {
                CONF->PutDouble(FLOOR_MODEL_TAU_KEY, DEFAULT_FLOOR_MODEL_TAU);
                CONF->PutDouble(FLOOR_MODEL_K_KEY, DEFAULT_FLOOR_MODEL_K);
                CONF->PutDouble(FLOOR_MODEL_C_KEY, DEFAULT_FLOOR_MODEL_C);
            }

            sendOk_(request);
        }
    );

    // ---- NTC multi-point calibration ----
    server.on(EP_NTC_CALIBRATE, HTTP_POST,
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

            float targetC = NAN;
            uint32_t sampleMs = 0;
            uint32_t timeoutMs = 0;
            uint16_t maxSamples = CalibrationRecorder::kDefaultMaxSamples;

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "target_c") == 0) {
                        double v = NAN;
                        if (!readCborDouble_(it, v)) return false;
                        targetC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "sample_ms") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        sampleMs = static_cast<uint32_t>(v);
                        return true;
                    }
                    if (strcmp(key, "timeout_ms") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        timeoutMs = static_cast<uint32_t>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (sampleMs == 0) {
                sampleMs = getNtcCalSampleMsFromConfig();
            }
            if (timeoutMs == 0) {
                timeoutMs = getNtcCalTimeoutMsFromConfig();
            }

            if (!isfinite(targetC) || targetC <= 0.0f) {
                targetC = NAN; // default to heatsink reference
            }
            if (sampleMs < 50) sampleMs = 50;
            if (sampleMs > 5000) sampleMs = 5000;
            if (timeoutMs < 1000) timeoutMs = 1000;
            if (timeoutMs > 3600000) timeoutMs = 3600000;

            if (maxSamples > CalibrationRecorder::kAbsoluteMaxSamples) {
                maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
            }
            const uint32_t totalMs = timeoutMs;
            if (sampleMs > 0 && totalMs > 0) {
                uint32_t required = (totalMs / sampleMs) + 4;
                if (required > maxSamples) {
                    if (required <= CalibrationRecorder::kAbsoluteMaxSamples) {
                        maxSamples = static_cast<uint16_t>(required);
                    } else {
                        const uint32_t minInterval =
                            (totalMs / (CalibrationRecorder::kAbsoluteMaxSamples - 1)) + 1;
                        if (sampleMs < minInterval) sampleMs = minInterval;
                        if (sampleMs > 5000) sampleMs = 5000;
                        maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                    }
                }
            }

            if (CONF) {
                if (isfinite(targetC)) {
                    CONF->PutFloat(NTC_CAL_TARGET_C_KEY, targetC);
                }
                CONF->PutInt(NTC_CAL_SAMPLE_MS_KEY, static_cast<int>(sampleMs));
                CONF->PutInt(NTC_CAL_TIMEOUT_MS_KEY, static_cast<int>(timeoutMs));
            }

            if (s_ntcCalTask != nullptr) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            if (!DEVICE) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }
            if (!NTC) {
                WiFiCbor::sendError(request, 503, ERR_NTC_MISSING);
                return;
            }
            const DeviceState st = DEVICE->getState();
            if (st == DeviceState::Running) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }

            if (!BUS_SAMPLER) {
                WiFiCbor::sendError(request, 503, ERR_BUS_SAMPLER_MISSING);
                return;
            }
            if (CALREC && CALREC->isRunning()) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            const uint8_t wireIndex = getNtcGateIndexFromConfig();
            if (!CALREC || !CALREC->start(CalibrationRecorder::Mode::Ntc,
                                        sampleMs,
                                        maxSamples,
                                        targetC,
                                        wireIndex)) {
                WiFiCbor::sendError(request, 500, ERR_START_FAILED);
                return;
            }

            NtcCalTaskArgs* args = new NtcCalTaskArgs();
            if (!args) {
                if (CALREC) {
                    CALREC->stop();
                }
                WiFiCbor::sendError(request, 500, ERR_ALLOC_FAILED);
                return;
            }
            args->targetC = targetC;
            args->wireIndex = wireIndex;
            args->sampleMs = sampleMs;
            args->timeoutMs = timeoutMs;
            args->startMs = CALREC->getMeta().startMs;

            ntcCalStartStatus(*args);

            BaseType_t okTask = xTaskCreate(
                ntcCalTask,
                "NtcCal",
                4096,
                args,
                2,
                &s_ntcCalTask
            );
            if (okTask != pdPASS) {
                delete args;
                s_ntcCalTask = nullptr;
                if (CALREC) {
                    CALREC->stop();
                }
                WiFiCbor::sendError(request, 500, ERR_TASK_FAILED);
                return;
            }

            sendStatusRunning_(request, true);
        }
    );

    // ---- NTC calibration status ----
    server.on(EP_NTC_CAL_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const NtcCalStatus st = ntcCalGetStatus();
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 512, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "running", st.running)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "done", st.done)) return false;
                    if (st.error) {
                        if (!WiFiCbor::encodeKvText(map, "error",
                                                    st.errorMsg[0] ? st.errorMsg : ERR_FAILED)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvUInt(map, "start_ms", st.startMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "elapsed_ms", st.elapsedMs)) return false;
                    if (isfinite(st.targetC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "target_c", st.targetC)) return false;
                    }
                    if (isfinite(st.heatsinkC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "heatsink_c", st.heatsinkC)) return false;
                    }
                    if (isfinite(st.ntcOhm)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntc_ohm", st.ntcOhm)) return false;
                    }
                    if (!WiFiCbor::encodeKvUInt(map, "sample_ms", st.sampleMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "samples", st.samples)) return false;
                    if (isfinite(st.shA)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_a", st.shA)) return false;
                    }
                    if (isfinite(st.shB)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_b", st.shB)) return false;
                    }
                    if (isfinite(st.shC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_c", st.shC)) return false;
                    }
                    if (st.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "wire_index", st.wireIndex)) return false;
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- NTC calibration stop ----
    server.on(EP_NTC_CAL_STOP, HTTP_POST,
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

            ntcCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            sendStatusRunning_(request, false);
        }
    );

    // ---- NTC single-point beta calibration ----
    server.on(EP_NTC_BETA_CALIBRATE, HTTP_POST,
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

            float refC = NAN;
            float refTempC = NAN;
            float refAliasC = NAN;
            float tempC = NAN;
            float targetC = NAN;

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    double v = NAN;
                    if (strcmp(key, "ref_temp_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        refTempC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "ref_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        refAliasC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "temp_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        tempC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "target_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        targetC = static_cast<float>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (isfinite(refTempC)) {
                refC = refTempC;
            } else if (isfinite(refAliasC)) {
                refC = refAliasC;
            } else if (isfinite(tempC)) {
                refC = tempC;
            } else if (isfinite(targetC)) {
                refC = targetC;
            }

            if (!isfinite(refC) || refC <= 0.0f) {
                if (DEVICE && DEVICE->tempSensor) {
                    const float hsC = DEVICE->tempSensor->getHeatsinkTemp();
                    if (isfinite(hsC) && hsC > 0.0f) {
                        refC = hsC;
                    }
                }
            }
            if (!isfinite(refC) || refC <= 0.0f) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_REF_TEMP);
                return;
            }

            if (s_ntcCalTask != nullptr) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            if (!DEVTRAN || !DEVICE) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }
            if (!NTC) {
                WiFiCbor::sendError(request, 503, ERR_NTC_MISSING);
                return;
            }

            DeviceState lastState = DeviceState::Shutdown;
            if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }

            if (!NTC->calibrateAtTempC(refC)) {
                WiFiCbor::sendError(request, 500, ERR_CALIBRATION_FAILED);
                return;
            }
            NTC->setModel(NtcSensor::Model::Beta, true);
            if (CONF) {
                CONF->PutBool(CALIB_NTC_DONE_KEY, true);
            }
            sendStatusApplied_(request);
        }
    );

    server.on(EP_HISTORY_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const uint16_t count = POWER_TRACKER->getHistoryCount();
            if (count == 0) {
                sendHistoryEmpty_(request);
                return;
            }
            const size_t capacity = 256 + (size_t)count * 80;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "history")) return false;
                    CborEncoder arr;
                    if (cbor_encoder_create_array(map, &arr, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (uint16_t i = 0; i < count; ++i) {
                        PowerTracker::HistoryEntry h;
                        if (!POWER_TRACKER->getHistoryEntry(i, h) || !h.valid) continue;
                        CborEncoder row;
                        if (cbor_encoder_create_map(&arr, &row, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvUInt(&row, "start_ms", h.startMs)) return false;
                        if (!WiFiCbor::encodeKvUInt(&row, "duration_s", h.stats.duration_s)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "energy_Wh", h.stats.energy_Wh)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "peakPower_W", h.stats.peakPower_W)) return false;
                        if (!WiFiCbor::encodeKvFloat(&row, "peakCurrent_A", h.stats.peakCurrent_A)) return false;
                        if (cbor_encoder_close_container(&arr, &row) != CborNoError) {
                            return false;
                        }
                    }
                    return cbor_encoder_close_container(map, &arr) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            String action;
            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "action") == 0) {
                    return readCborText_(it, action);
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            if (action != "disconnect") {
                WiFiCbor::sendError(request, 400, ERR_INVALID_ACTION);
                return;
            }

            onDisconnected();
            if (lock()) {
                lastActivityMillis = millis();
                keepAlive = false;
                unlock();
            }
            RGB->postOverlay(OverlayEvent::WIFI_LOST);
            sendOk_(request);
        }
    );

    // ---- Monitor (uses snapshot) ----
    server.on(EP_MONITOR, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); keepAlive = true; unlock(); }

            std::vector<uint8_t> payload;
            if (!getMonitorCbor(payload)) {
                WiFiCbor::sendError(request, 503, ERR_SNAPSHOT_BUSY);
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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

            const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 3072, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvText(map, SSE_EVENT_STATE, stateName(snap.state))) {
                        return false;
                    }

                    if (DEVICE) {
                        if (markRead) {
                            DEVICE->markEventHistoryRead();
                        }

                        Device::LastEventInfo info = DEVICE->getLastEventInfo();
                        if (!WiFiCbor::encodeText(map, "last_error")) return false;
                        CborEncoder err;
                        if (cbor_encoder_create_map(map, &err, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (info.hasError) {
                            if (!WiFiCbor::encodeKvText(&err, "reason", info.errorReason)) return false;
                            if (info.errorMs) {
                                if (!WiFiCbor::encodeKvUInt(&err, "ms", info.errorMs)) return false;
                            }
                            if (info.errorEpoch) {
                                if (!WiFiCbor::encodeKvUInt(&err, "epoch", info.errorEpoch)) return false;
                            }
                        }
                        if (cbor_encoder_close_container(map, &err) != CborNoError) return false;

                        if (!WiFiCbor::encodeText(map, "last_stop")) return false;
                        CborEncoder stop;
                        if (cbor_encoder_create_map(map, &stop, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (info.hasStop) {
                            if (!WiFiCbor::encodeKvText(&stop, "reason", info.stopReason)) return false;
                            if (info.stopMs) {
                                if (!WiFiCbor::encodeKvUInt(&stop, "ms", info.stopMs)) return false;
                            }
                            if (info.stopEpoch) {
                                if (!WiFiCbor::encodeKvUInt(&stop, "epoch", info.stopEpoch)) return false;
                            }
                        }
                        if (cbor_encoder_close_container(map, &stop) != CborNoError) return false;

                        uint8_t warnCount = 0;
                        uint8_t errCount = 0;
                        DEVICE->getUnreadEventCounts(warnCount, errCount);
                        if (!WiFiCbor::encodeText(map, "unread")) return false;
                        CborEncoder unread;
                        if (cbor_encoder_create_map(map, &unread, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvUInt(&unread, "warn", warnCount)) return false;
                        if (!WiFiCbor::encodeKvUInt(&unread, "error", errCount)) return false;
                        if (cbor_encoder_close_container(map, &unread) != CborNoError) return false;

                        Device::EventEntry warnEntries[10]{};
                        Device::EventEntry errEntries[10]{};
                        const size_t warnHistory = DEVICE->getWarningHistory(warnEntries, 10);
                        const size_t errHistory = DEVICE->getErrorHistory(errEntries, 10);

                        if (!WiFiCbor::encodeText(map, "warnings")) return false;
                        CborEncoder warnings;
                        if (cbor_encoder_create_array(map, &warnings, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        for (size_t i = 0; i < warnHistory; ++i) {
                            const Device::EventEntry& e = warnEntries[i];
                            CborEncoder item;
                            if (cbor_encoder_create_map(&warnings, &item, CborIndefiniteLength) != CborNoError) {
                                return false;
                            }
                            if (!WiFiCbor::encodeKvText(&item, "reason", e.reason)) return false;
                            if (e.ms) {
                                if (!WiFiCbor::encodeKvUInt(&item, "ms", e.ms)) return false;
                            }
                            if (e.epoch) {
                                if (!WiFiCbor::encodeKvUInt(&item, "epoch", e.epoch)) return false;
                            }
                            if (cbor_encoder_close_container(&warnings, &item) != CborNoError) return false;
                        }
                        if (cbor_encoder_close_container(map, &warnings) != CborNoError) return false;

                        if (!WiFiCbor::encodeText(map, "errors")) return false;
                        CborEncoder errors;
                        if (cbor_encoder_create_array(map, &errors, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        for (size_t i = 0; i < errHistory; ++i) {
                            const Device::EventEntry& e = errEntries[i];
                            CborEncoder item;
                            if (cbor_encoder_create_map(&errors, &item, CborIndefiniteLength) != CborNoError) {
                                return false;
                            }
                            if (!WiFiCbor::encodeKvText(&item, "reason", e.reason)) return false;
                            if (e.ms) {
                                if (!WiFiCbor::encodeKvUInt(&item, "ms", e.ms)) return false;
                            }
                            if (e.epoch) {
                                if (!WiFiCbor::encodeKvUInt(&item, "epoch", e.epoch)) return false;
                            }
                            if (cbor_encoder_close_container(&errors, &item) != CborNoError) return false;
                        }
                        if (cbor_encoder_close_container(map, &errors) != CborNoError) return false;
                    }

                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;
            if (!isAuthenticated(request)) {
                body.clear();
                return;
            }

            String action;
            String target;
            CborValue valueIt;
            bool hasValue = false;
            uint32_t epoch = 0;
            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "action") == 0) {
                    return readCborText_(it, action);
                }
                if (strcmp(key, "target") == 0) {
                    return readCborText_(it, target);
                }
                if (strcmp(key, "value") == 0) {
                    valueIt = *it;
                    hasValue = true;
                    return skipCborValue_(it);
                }
                if (strcmp(key, "epoch") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    epoch = static_cast<uint32_t>(v);
                    return true;
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            ControlCmd c{};
            if (epoch > 0 && RTC) {
                RTC->setUnixTime(epoch);
            }

            if (action == "set") {
                String valStr = "null";
                if (hasValue) {
                    CborValue tmp = valueIt;
                    if (cbor_value_is_text_string(&tmp)) {
                        readCborText_(&tmp, valStr);
                    } else if (cbor_value_is_boolean(&tmp)) {
                        bool b = false;
                        if (cbor_value_get_boolean(&tmp, &b) == CborNoError) {
                            valStr = b ? "true" : "false";
                        }
                    } else if (cbor_value_is_integer(&tmp)) {
                        int64_t v = 0;
                        if (cbor_value_get_int64(&tmp, &v) == CborNoError) {
                            valStr = String(static_cast<long long>(v));
                        }
                    } else if (cbor_value_is_float(&tmp) || cbor_value_is_double(&tmp)) {
                        double v = 0.0;
                        if (cbor_value_get_double(&tmp, &v) == CborNoError) {
                            valStr = String(v, 3);
                        }
                    } else if (cbor_value_is_map(&tmp) || cbor_value_is_array(&tmp)) {
                        valStr = "[complex]";
                    }
                }
                DEBUG_PRINTF("[WiFi] /control set target=%s value=%s\n",
                             target.c_str(),
                             valStr.c_str());

                auto readValueBool = [&](bool& out) -> bool {
                    if (!hasValue) {
                        out = false;
                        return true;
                    }
                    CborValue tmp = valueIt;
                    if (cbor_value_is_boolean(&tmp)) {
                        return readCborBool_(&tmp, out);
                    }
                    if (cbor_value_is_integer(&tmp)) {
                        int64_t v = 0;
                        if (!readCborInt64_(&tmp, v)) return false;
                        out = (v != 0);
                        return true;
                    }
                    return false;
                };

                auto readValueInt = [&](int& out) -> bool {
                    if (!hasValue) {
                        out = 0;
                        return true;
                    }
                    CborValue tmp = valueIt;
                    if (cbor_value_is_integer(&tmp)) {
                        int64_t v = 0;
                        if (!readCborInt64_(&tmp, v)) return false;
                        out = static_cast<int>(v);
                        return true;
                    }
                    if (cbor_value_is_float(&tmp) || cbor_value_is_double(&tmp)) {
                        double v = 0.0;
                        if (!readCborDouble_(&tmp, v)) return false;
                        out = static_cast<int>(lround(v));
                        return true;
                    }
                    return false;
                };

                auto readValueFloat = [&](float& out) -> bool {
                    if (!hasValue) {
                        out = 0.0f;
                        return true;
                    }
                    CborValue tmp = valueIt;
                    double v = 0.0;
                    if (!readCborDouble_(&tmp, v)) return false;
                    out = static_cast<float>(v);
                    return true;
                };

                auto readValueDouble = [&](double& out) -> bool {
                    if (!hasValue) {
                        out = 0.0;
                        return true;
                    }
                    CborValue tmp = valueIt;
                    return readCborDouble_(&tmp, out);
                };

                auto readValueString = [&](String& out) -> bool {
                    if (!hasValue) {
                        out = "";
                        return true;
                    }
                    CborValue tmp = valueIt;
                    return readCborText_(&tmp, out);
                };

                auto parseWireIndex = [&](const String& name,
                                          const char* prefix) -> int {
                    if (!name.startsWith(prefix)) return 0;
                    const int idx = name.substring(strlen(prefix)).toInt();
                    if (idx < 1 || idx > HeaterManager::kWireCount) return 0;
                    return idx;
                };

                int wireIdx = parseWireIndex(target, "wireTau");
                if (wireIdx > 0) {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_MODEL_TAU;
                    CONF->PutDouble(kWireModelTauKeys[wireIdx - 1], v);
                    if (DEVICE) {
                        const double k = CONF->GetDouble(kWireModelKKeys[wireIdx - 1],
                                                         DEFAULT_WIRE_MODEL_K);
                        const double c = CONF->GetDouble(kWireModelCKeys[wireIdx - 1],
                                                         DEFAULT_WIRE_MODEL_C);
                        DEVICE->getWireThermalModel()
                            .setWireThermalParams(wireIdx, v, k, c);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                wireIdx = parseWireIndex(target, "wireK");
                if (wireIdx > 0) {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_MODEL_K;
                    CONF->PutDouble(kWireModelKKeys[wireIdx - 1], v);
                    if (DEVICE) {
                        const double tau = CONF->GetDouble(kWireModelTauKeys[wireIdx - 1],
                                                           DEFAULT_WIRE_MODEL_TAU);
                        const double c = CONF->GetDouble(kWireModelCKeys[wireIdx - 1],
                                                         DEFAULT_WIRE_MODEL_C);
                        DEVICE->getWireThermalModel()
                            .setWireThermalParams(wireIdx, tau, v, c);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                wireIdx = parseWireIndex(target, "wireC");
                if (wireIdx > 0) {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_WIRE_MODEL_C;
                    CONF->PutDouble(kWireModelCKeys[wireIdx - 1], v);
                    if (DEVICE) {
                        const double tau = CONF->GetDouble(kWireModelTauKeys[wireIdx - 1],
                                                           DEFAULT_WIRE_MODEL_TAU);
                        const double k = CONF->GetDouble(kWireModelKKeys[wireIdx - 1],
                                                         DEFAULT_WIRE_MODEL_K);
                        DEVICE->getWireThermalModel()
                            .setWireThermalParams(wireIdx, tau, k, v);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                wireIdx = parseWireIndex(target, "wireCalibrated");
                if (wireIdx > 0) {
                    bool v = false;
                    if (!readValueBool(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    CONF->PutBool(kWireCalibDoneKeys[wireIdx - 1], v);
                    sendStatusApplied_(request);
                    return;
                }

                if (target == "reboot")                       c.type = CTRL_REBOOT;
                else if (target == "systemReset")             c.type = CTRL_SYS_RESET;
                else if (target == "ledFeedback")             { c.type = CTRL_LED_FEEDBACK_BOOL; if (!readValueBool(c.b1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "relay")                   { c.type = CTRL_RELAY_BOOL;        if (!readValueBool(c.b1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target.startsWith("output"))         { c.type = CTRL_OUTPUT_BOOL;       c.i1 = target.substring(6).toInt(); if (!readValueBool(c.b1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "acFrequency")             { c.type = CTRL_AC_FREQ;           if (!readValueInt(c.i1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "chargeResistor")          { c.type = CTRL_CHARGE_RES;        if (!readValueFloat(c.f1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target.startsWith("Access"))         { c.type = CTRL_ACCESS_BOOL;       c.i1 = target.substring(6).toInt(); if (!readValueBool(c.b1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "systemStart")             c.type = CTRL_SYSTEM_START;
                else if (target == "systemWake")              c.type = CTRL_SYSTEM_WAKE;
                else if (target == "systemShutdown")          c.type = CTRL_SYSTEM_SHUTDOWN;
                else if (target == "fanSpeed")                { c.type = CTRL_FAN_SPEED;         if (!readValueInt(c.i1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } c.i1 = constrain(c.i1, 0, 100); }
                else if (target == "buzzerMute")              { c.type = CTRL_BUZZER_MUTE;       if (!readValueBool(c.b1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target.startsWith("wireRes"))        { c.type = CTRL_WIRE_RES;          c.i1 = target.substring(7).toInt(); if (!readValueFloat(c.f1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "wireOhmPerM")             { c.type = CTRL_WIRE_OHM_PER_M;    if (!readValueFloat(c.f1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "wireGauge")               { c.type = CTRL_WIRE_GAUGE;        if (!readValueInt(c.i1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "currLimit")               { c.type = CTRL_CURR_LIMIT;        if (!readValueFloat(c.f1)) { WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR); return; } }
                else if (target == "confirmWiresCool")        { c.type = CTRL_CONFIRM_WIRES_COOL; }
                else if (target == "adminCredentials") {
                    String current;
                    String newUser;
                    String newPass;
                    String newSsid;
                    String newWifiPass;
                    if (hasValue && cbor_value_is_map(&valueIt)) {
                        CborValue tmp = valueIt;
                        const bool parsedMap = parseCborValueMap_(&tmp, [&](const char* key, CborValue* it) {
                            if (strcmp(key, "current") == 0) {
                                return readCborText_(it, current);
                            }
                            if (strcmp(key, "username") == 0) {
                                return readCborText_(it, newUser);
                            }
                            if (strcmp(key, "password") == 0) {
                                return readCborText_(it, newPass);
                            }
                            if (strcmp(key, "wifiSSID") == 0) {
                                return readCborText_(it, newSsid);
                            }
                            if (strcmp(key, "wifiPassword") == 0) {
                                return readCborText_(it, newWifiPass);
                            }
                            return skipCborValue_(it);
                        });
                        if (!parsedMap) {
                            WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                            return;
                        }
                    }

                    const String storedUser = CONF->GetString(ADMIN_ID_KEY, DEFAULT_ADMIN_ID);
                    const String storedPass = CONF->GetString(ADMIN_PASS_KEY, DEFAULT_ADMIN_PASS);
                    const String storedSsid = CONF->GetString(STA_SSID_KEY, DEFAULT_STA_SSID);
                    const String storedWifiPass = CONF->GetString(STA_PASS_KEY, DEFAULT_STA_PASS);
                    if (current.length() && current != storedPass) {
                        WiFiCbor::sendError(request, 403, ERR_BAD_PASSWORD);
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

                    sendStatusApplied_(request);
                    if (sessionChanged) {
                        onDisconnected();
                    }
                    if (wifiChanged) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "userCredentials") {
                    String current;
                    String newPass;
                    String newId;
                    if (hasValue && cbor_value_is_map(&valueIt)) {
                        CborValue tmp = valueIt;
                        const bool parsedMap = parseCborValueMap_(&tmp, [&](const char* key, CborValue* it) {
                            if (strcmp(key, "current") == 0) {
                                return readCborText_(it, current);
                            }
                            if (strcmp(key, "newPass") == 0) {
                                return readCborText_(it, newPass);
                            }
                            if (strcmp(key, "newId") == 0) {
                                return readCborText_(it, newId);
                            }
                            return skipCborValue_(it);
                        });
                        if (!parsedMap) {
                            WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                            return;
                        }
                    }
                    const String storedPass = CONF->GetString(USER_PASS_KEY, DEFAULT_USER_PASS);
                    if (current.length() && current != storedPass) {
                        WiFiCbor::sendError(request, 403, ERR_BAD_PASSWORD);
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
                    sendStatusApplied_(request);
                    if (sessionChanged) {
                        onDisconnected();
                    }
                    return;
                }
                else if (target == "wifiSSID") {
                    String ssid;
                    if (!readValueString(ssid)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    bool changed = false;
                    if (ssid.length()) {
                        const String stored = CONF->GetString(STA_SSID_KEY, DEFAULT_STA_SSID);
                        if (ssid != stored) {
                            CONF->PutString(STA_SSID_KEY, ssid);
                            changed = true;
                        }
                    }
                    sendStatusApplied_(request);
                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "wifiPassword") {
                    String pw;
                    if (!readValueString(pw)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    bool changed = false;
                    if (pw.length()) {
                        const String stored = CONF->GetString(STA_PASS_KEY, DEFAULT_STA_PASS);
                        if (pw != stored) {
                            CONF->PutString(STA_PASS_KEY, pw);
                            changed = true;
                        }
                    }
                    sendStatusApplied_(request);
                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                    return;
                }
                else if (target == "tempWarnC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v < 0.0f) v = 0.0f;
                    CONF->PutFloat(TEMP_WARN_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "tempTripC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_TEMP_THRESHOLD;
                    CONF->PutFloat(TEMP_THRESHOLD_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorThicknessMm") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v < 0.0f) {
                        v = DEFAULT_FLOOR_THICKNESS_MM;
                    } else if (v > 0.0f) {
                        if (v < FLOOR_THICKNESS_MIN_MM) v = FLOOR_THICKNESS_MIN_MM;
                        if (v > FLOOR_THICKNESS_MAX_MM) v = FLOOR_THICKNESS_MAX_MM;
                    }
                    CONF->PutFloat(FLOOR_THICKNESS_MM_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorMaterial") {
                    const int fallback = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
                    int code = fallback;
                    if (hasValue) {
                        CborValue tmp = valueIt;
                        if (cbor_value_is_text_string(&tmp)) {
                            String s;
                            if (!readCborText_(&tmp, s)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            code = parseFloorMaterialCode(s, fallback);
                        } else if (cbor_value_is_integer(&tmp)) {
                            int64_t v = 0;
                            if (!readCborInt64_(&tmp, v)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            if (v >= FLOOR_MAT_WOOD && v <= FLOOR_MAT_GRANITE) {
                                code = static_cast<int>(v);
                            }
                        }
                    }
                    CONF->PutInt(FLOOR_MATERIAL_KEY, code);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorMaxC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_FLOOR_MAX_C;
                    if (v > DEFAULT_FLOOR_MAX_C) v = DEFAULT_FLOOR_MAX_C;
                    CONF->PutFloat(FLOOR_MAX_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorSwitchMarginC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_FLOOR_SWITCH_MARGIN_C;
                    CONF->PutFloat(FLOOR_SWITCH_MARGIN_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorTau") {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_FLOOR_MODEL_TAU;
                    CONF->PutDouble(FLOOR_MODEL_TAU_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorK") {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_FLOOR_MODEL_K;
                    CONF->PutDouble(FLOOR_MODEL_K_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorC") {
                    double v = 0.0;
                    if (!readValueDouble(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0) v = DEFAULT_FLOOR_MODEL_C;
                    CONF->PutDouble(FLOOR_MODEL_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "nichromeFinalTempC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v < 0.0f) v = DEFAULT_NICHROME_FINAL_TEMP_C;
                    CONF->PutFloat(NICHROME_FINAL_TEMP_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "currentSource") {
                    int src = DEFAULT_CURRENT_SOURCE;
                    if (hasValue) {
                        CborValue tmp = valueIt;
                        if (cbor_value_is_text_string(&tmp)) {
                            String s;
                            if (!readCborText_(&tmp, s)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            s.toLowerCase();
                            src = (s.indexOf("acs") >= 0) ? CURRENT_SRC_ACS : CURRENT_SRC_ESTIMATE;
                        } else if (cbor_value_is_integer(&tmp)) {
                            int64_t v = 0;
                            if (!readCborInt64_(&tmp, v)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            src = (v == CURRENT_SRC_ACS) ? CURRENT_SRC_ACS : CURRENT_SRC_ESTIMATE;
                        }
                    }
                    CONF->PutInt(CURRENT_SOURCE_KEY, src);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "presenceMinDropV") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_PRESENCE_MIN_DROP_V;
                    if (v < 5.0f) v = 5.0f;
                    if (v > 100.0f) v = 100.0f;
                    CONF->PutFloat(PRESENCE_MIN_DROP_V_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "presenceCalibrated") {
                    bool v = false;
                    if (!readValueBool(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    CONF->PutBool(CALIB_PRESENCE_DONE_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "floorCalibrated") {
                    bool v = false;
                    if (!readValueBool(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    CONF->PutBool(CALIB_FLOOR_DONE_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcModel") {
                    int model = DEFAULT_NTC_MODEL;
                    if (hasValue) {
                        CborValue tmp = valueIt;
                        if (cbor_value_is_text_string(&tmp)) {
                            String s;
                            if (!readCborText_(&tmp, s)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            s.toLowerCase();
                            model = (s.indexOf("stein") >= 0 || s.indexOf("sh") >= 0) ? 1 : 0;
                        } else if (cbor_value_is_integer(&tmp)) {
                            int64_t v = 0;
                            if (!readCborInt64_(&tmp, v)) {
                                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                                return;
                            }
                            model = (v == 1) ? 1 : 0;
                        }
                    }
                    if (NTC) {
                        NTC->setModel(model == 1 ? NtcSensor::Model::Steinhart
                                                 : NtcSensor::Model::Beta, true);
                    } else {
                        CONF->PutInt(NTC_MODEL_KEY, model);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcBeta") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_BETA;
                    if (NTC) NTC->setBeta(v, true);
                    else CONF->PutFloat(NTC_BETA_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcT0C") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v)) v = DEFAULT_NTC_T0_C;
                    if (NTC) NTC->setT0C(v, true);
                    else CONF->PutFloat(NTC_T0_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcR0") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_R0_OHMS;
                    if (NTC) NTC->setR0(v, true);
                    else CONF->PutFloat(NTC_R0_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcShA" || target == "ntcShB" || target == "ntcShC") {
                    float a = DEFAULT_NTC_SH_A;
                    float b = DEFAULT_NTC_SH_B;
                    float c = DEFAULT_NTC_SH_C;
                    if (CONF) {
                        a = CONF->GetFloat(NTC_SH_A_KEY, DEFAULT_NTC_SH_A);
                        b = CONF->GetFloat(NTC_SH_B_KEY, DEFAULT_NTC_SH_B);
                        c = CONF->GetFloat(NTC_SH_C_KEY, DEFAULT_NTC_SH_C);
                    }
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (target == "ntcShA") a = v;
                    else if (target == "ntcShB") b = v;
                    else c = v;

                    bool persisted = false;
                    if (NTC) {
                        persisted = NTC->setSteinhartCoefficients(a, b, c, true);
                    }
                    if (!persisted && CONF) {
                        CONF->PutFloat(NTC_SH_A_KEY, a);
                        CONF->PutFloat(NTC_SH_B_KEY, b);
                        CONF->PutFloat(NTC_SH_C_KEY, c);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcFixedRes") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_FIXED_RES_OHMS;
                    if (NTC) NTC->setFixedRes(v, true);
                    else CONF->PutFloat(NTC_FIXED_RES_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcMinC" || target == "ntcMaxC") {
                    float minC = DEFAULT_NTC_MIN_C;
                    float maxC = DEFAULT_NTC_MAX_C;
                    if (CONF) {
                        minC = CONF->GetFloat(NTC_MIN_C_KEY, DEFAULT_NTC_MIN_C);
                        maxC = CONF->GetFloat(NTC_MAX_C_KEY, DEFAULT_NTC_MAX_C);
                    }
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (target == "ntcMinC") minC = v;
                    else maxC = v;
                    if (!isfinite(minC)) minC = DEFAULT_NTC_MIN_C;
                    if (!isfinite(maxC)) maxC = DEFAULT_NTC_MAX_C;
                    if (minC >= maxC) {
                        minC = DEFAULT_NTC_MIN_C;
                        maxC = DEFAULT_NTC_MAX_C;
                    }
                    if (NTC) {
                        NTC->setTempLimits(minC, maxC, true);
                    } else {
                        CONF->PutFloat(NTC_MIN_C_KEY, minC);
                        CONF->PutFloat(NTC_MAX_C_KEY, maxC);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcSamples") {
                    int v = 0;
                    if (!readValueInt(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (v < 1) v = 1;
                    if (v > 64) v = 64;
                    if (NTC) NTC->setSampleCount(static_cast<uint8_t>(v), true);
                    else CONF->PutInt(NTC_SAMPLES_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcPressMv" || target == "ntcReleaseMv" ||
                         target == "ntcDebounceMs") {
                    float pressMv = DEFAULT_NTC_PRESS_MV;
                    float releaseMv = DEFAULT_NTC_RELEASE_MV;
                    int debounceMs = DEFAULT_NTC_DEBOUNCE_MS;
                    if (CONF) {
                        pressMv = CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV);
                        releaseMv = CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV);
                        debounceMs = CONF->GetInt(NTC_DEBOUNCE_MS_KEY, DEFAULT_NTC_DEBOUNCE_MS);
                    }
                    if (target == "ntcPressMv") {
                        float v = 0.0f;
                        if (!readValueFloat(v)) {
                            WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                            return;
                        }
                        pressMv = v;
                    } else if (target == "ntcReleaseMv") {
                        float v = 0.0f;
                        if (!readValueFloat(v)) {
                            WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                            return;
                        }
                        releaseMv = v;
                    } else {
                        int v = 0;
                        if (!readValueInt(v)) {
                            WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                            return;
                        }
                        debounceMs = v;
                    }
                    if (!isfinite(pressMv) || pressMv < 0.0f) pressMv = DEFAULT_NTC_PRESS_MV;
                    if (!isfinite(releaseMv) || releaseMv < pressMv) releaseMv = pressMv;
                    if (debounceMs < 0) debounceMs = 0;
                    if (NTC) {
                        NTC->setButtonThresholdsMv(pressMv, releaseMv,
                                                   static_cast<uint32_t>(debounceMs), true);
                    } else {
                        CONF->PutFloat(NTC_PRESS_MV_KEY, pressMv);
                        CONF->PutFloat(NTC_RELEASE_MV_KEY, releaseMv);
                        CONF->PutInt(NTC_DEBOUNCE_MS_KEY, debounceMs);
                    }
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcCalTargetC") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (!isfinite(v) || v <= 0.0f) v = DEFAULT_NTC_CAL_TARGET_C;
                    CONF->PutFloat(NTC_CAL_TARGET_C_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcCalSampleMs") {
                    int v = 0;
                    if (!readValueInt(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (v < 50) v = 50;
                    if (v > 5000) v = 5000;
                    CONF->PutInt(NTC_CAL_SAMPLE_MS_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcCalTimeoutMs") {
                    int v = 0;
                    if (!readValueInt(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (v < 1000) v = 1000;
                    if (v > 3600000) v = 3600000;
                    CONF->PutInt(NTC_CAL_TIMEOUT_MS_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcCalibrated") {
                    bool v = false;
                    if (!readValueBool(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    CONF->PutBool(CALIB_NTC_DONE_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "ntcGateIndex") {
                    int v = 0;
                    if (!readValueInt(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    if (v < 1) v = 1;
                    if (v > HeaterManager::kWireCount) v = HeaterManager::kWireCount;
                    CONF->PutInt(NTC_GATE_INDEX_KEY, v);
                    sendStatusApplied_(request);
                    return;
                }
                else if (target == "calibrate")              { c.type = CTRL_CALIBRATE; }
                else {
                    WiFiCbor::sendError(request, 400, ERR_UNKNOWN_TARGET);
                    return;
                }

                const bool ok = sendCmd(c);
                if (ok) {
                    sendStatusQueued_(request);
                } else {
                    WiFiCbor::sendError(request, 503, ERR_CTRL_QUEUE_FULL);
                }
            } else if (action == "get" && target == "status") {
                const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
                sendState_(request, stateName(snap.state));
            } else {
                WiFiCbor::sendError(request, 400, ERR_INVALID_ACTION_TARGET);
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
                WiFiCbor::sendError(request, 503, ERR_SNAPSHOT_BUSY);
                return;
            }

            const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
            const int floorMatCode = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
            const float shA = CONF->GetFloat(NTC_SH_A_KEY, DEFAULT_NTC_SH_A);
            const float shB = CONF->GetFloat(NTC_SH_B_KEY, DEFAULT_NTC_SH_B);
            const float shC = CONF->GetFloat(NTC_SH_C_KEY, DEFAULT_NTC_SH_C);
            const bool setupDone =
                CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
            const bool setupConfigOk = checkSetupConfig(nullptr);
            const bool setupCalibOk = checkSetupCalib(nullptr);

            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 8192, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "ledFeedback",
                                                CONF->GetBool(LED_FEEDBACK_KEY, false))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "acFrequency",
                                               CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "chargeResistor",
                                                 CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f))) return false;
                    if (!WiFiCbor::encodeKvText(map, "deviceId",
                                                CONF->GetString(DEV_ID_KEY, ""))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "wireOhmPerM",
                                                 CONF->GetFloat(WIRE_OHM_PER_M_KEY,
                                                                DEFAULT_WIRE_OHM_PER_M))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "wireGauge",
                                               CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE))) return false;
                    if (!WiFiCbor::encodeKvBool(map, "buzzerMute",
                                                CONF->GetBool(BUZMUT_KEY, BUZMUT_DEFAULT))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "tempTripC",
                                                 CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "tempWarnC",
                                                 CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorThicknessMm",
                                                 CONF->GetFloat(FLOOR_THICKNESS_MM_KEY,
                                                                DEFAULT_FLOOR_THICKNESS_MM))) return false;
                    if (!WiFiCbor::encodeKvText(map, "floorMaterial",
                                                floorMaterialToString(floorMatCode))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "floorMaterialCode", floorMatCode)) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorMaxC",
                                                 CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorSwitchMarginC",
                                                 CONF->GetFloat(FLOOR_SWITCH_MARGIN_C_KEY,
                                                                DEFAULT_FLOOR_SWITCH_MARGIN_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "nichromeFinalTempC",
                                                 CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                                                DEFAULT_NICHROME_FINAL_TEMP_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorTau",
                                                 CONF->GetDouble(FLOOR_MODEL_TAU_KEY,
                                                                 DEFAULT_FLOOR_MODEL_TAU))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorK",
                                                 CONF->GetDouble(FLOOR_MODEL_K_KEY,
                                                                 DEFAULT_FLOOR_MODEL_K))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "floorC",
                                                 CONF->GetDouble(FLOOR_MODEL_C_KEY,
                                                                 DEFAULT_FLOOR_MODEL_C))) return false;
                    if (!WiFiCbor::encodeKvBool(map, "floorCalibrated",
                                                CONF->GetBool(CALIB_FLOOR_DONE_KEY,
                                                              DEFAULT_CALIB_FLOOR_DONE))) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "ntcGateIndex",
                                                getNtcGateIndexFromConfig())) return false;
                    if (!WiFiCbor::encodeKvInt(map, "ntcModel",
                                               CONF->GetInt(NTC_MODEL_KEY, DEFAULT_NTC_MODEL))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcBeta",
                                                 CONF->GetFloat(NTC_BETA_KEY, DEFAULT_NTC_BETA))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcT0C",
                                                 CONF->GetFloat(NTC_T0_C_KEY, DEFAULT_NTC_T0_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcR0",
                                                 CONF->GetFloat(NTC_R0_KEY, DEFAULT_NTC_R0_OHMS))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcFixedRes",
                                                 CONF->GetFloat(NTC_FIXED_RES_KEY,
                                                                DEFAULT_NTC_FIXED_RES_OHMS))) return false;
                    if (isfinite(shA)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntcShA", shA)) return false;
                    }
                    if (isfinite(shB)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntcShB", shB)) return false;
                    }
                    if (isfinite(shC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntcShC", shC)) return false;
                    }
                    if (!WiFiCbor::encodeKvFloat(map, "ntcMinC",
                                                 CONF->GetFloat(NTC_MIN_C_KEY, DEFAULT_NTC_MIN_C))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcMaxC",
                                                 CONF->GetFloat(NTC_MAX_C_KEY, DEFAULT_NTC_MAX_C))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "ntcSamples",
                                               CONF->GetInt(NTC_SAMPLES_KEY, DEFAULT_NTC_SAMPLES))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcPressMv",
                                                 CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcReleaseMv",
                                                 CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "ntcDebounceMs",
                                               CONF->GetInt(NTC_DEBOUNCE_MS_KEY,
                                                            DEFAULT_NTC_DEBOUNCE_MS))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "ntcCalTargetC",
                                                 CONF->GetFloat(NTC_CAL_TARGET_C_KEY,
                                                                DEFAULT_NTC_CAL_TARGET_C))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "ntcCalSampleMs",
                                               CONF->GetInt(NTC_CAL_SAMPLE_MS_KEY,
                                                            DEFAULT_NTC_CAL_SAMPLE_MS))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "ntcCalTimeoutMs",
                                               CONF->GetInt(NTC_CAL_TIMEOUT_MS_KEY,
                                                            DEFAULT_NTC_CAL_TIMEOUT_MS))) return false;
                    if (!WiFiCbor::encodeKvBool(map, "ntcCalibrated",
                                                CONF->GetBool(CALIB_NTC_DONE_KEY,
                                                              DEFAULT_CALIB_NTC_DONE))) return false;
                    if (!WiFiCbor::encodeKvBool(map, "presenceCalibrated",
                                                CONF->GetBool(CALIB_PRESENCE_DONE_KEY,
                                                              DEFAULT_CALIB_PRESENCE_DONE))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "presenceMinDropV",
                                                 CONF->GetFloat(PRESENCE_MIN_DROP_V_KEY,
                                                                DEFAULT_PRESENCE_MIN_DROP_V))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "currLimit",
                                                 CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "currentSource",
                                               CONF->GetInt(CURRENT_SOURCE_KEY,
                                                            DEFAULT_CURRENT_SOURCE))) return false;
                    if (!WiFiCbor::encodeKvFloat(map, "capacitanceF",
                                                 DEVICE ? DEVICE->getCapBankCapF() : 0.0f)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "fanSpeed", FAN->getSpeedPercent())) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupDone", setupDone)) return false;
                    if (!WiFiCbor::encodeKvInt(map, "setupStage",
                                               CONF->GetInt(SETUP_STAGE_KEY, DEFAULT_SETUP_STAGE))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "setupSubstage",
                                               CONF->GetInt(SETUP_SUBSTAGE_KEY, DEFAULT_SETUP_SUBSTAGE))) return false;
                    if (!WiFiCbor::encodeKvInt(map, "setupWireIndex",
                                               CONF->GetInt(SETUP_WIRE_INDEX_KEY,
                                                            DEFAULT_SETUP_WIRE_INDEX))) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupConfigOk", setupConfigOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupCalibOk", setupCalibOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupReady", setupConfigOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupRunAllowed",
                                                setupDone && setupConfigOk)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "setupCalibPending",
                                                setupDone && setupConfigOk && !setupCalibOk)) return false;

                    if (!WiFiCbor::encodeKvBool(map, "relay", s.relayOn)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "ready",
                                                snap.state == DeviceState::Idle)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "off",
                                                snap.state == DeviceState::Shutdown)) return false;

                    if (!WiFiCbor::encodeText(map, "outputs")) return false;
                    CborEncoder outputs;
                    if (cbor_encoder_create_map(map, &outputs, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (int i = 0; i < HeaterManager::kWireCount; ++i) {
                        char key[12];
                        snprintf(key, sizeof(key), "output%d", i + 1);
                        if (!WiFiCbor::encodeKvBool(&outputs, key, s.outputs[i])) return false;
                    }
                    if (cbor_encoder_close_container(map, &outputs) != CborNoError) return false;

                    const char* accessKeys[10] = {
                        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                        OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                        OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                        OUT10_ACCESS_KEY
                    };
                    if (!WiFiCbor::encodeText(map, "outputAccess")) return false;
                    CborEncoder access;
                    if (cbor_encoder_create_map(map, &access, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (int i = 0; i < 10; ++i) {
                        char key[12];
                        snprintf(key, sizeof(key), "output%d", i + 1);
                        if (!WiFiCbor::encodeKvBool(&access, key,
                                                    CONF->GetBool(accessKeys[i], false))) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &access) != CborNoError) return false;

                    const char* rkeys[10] = {
                        R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
                        R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
                    };
                    if (!WiFiCbor::encodeText(map, "wireRes")) return false;
                    CborEncoder wireRes;
                    if (cbor_encoder_create_map(map, &wireRes, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (int i = 0; i < 10; ++i) {
                        char key[4];
                        snprintf(key, sizeof(key), "%d", i + 1);
                        if (!WiFiCbor::encodeKvFloat(&wireRes, key,
                                                     CONF->GetFloat(rkeys[i],
                                                                    DEFAULT_WIRE_RES_OHMS))) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &wireRes) != CborNoError) return false;

                    if (!WiFiCbor::encodeText(map, "wireTau")) return false;
                    CborEncoder wireTau;
                    if (cbor_encoder_create_map(map, &wireTau, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (!WiFiCbor::encodeText(map, "wireK")) return false;
                    CborEncoder wireK;
                    if (cbor_encoder_create_map(map, &wireK, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (!WiFiCbor::encodeText(map, "wireC")) return false;
                    CborEncoder wireC;
                    if (cbor_encoder_create_map(map, &wireC, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    if (!WiFiCbor::encodeText(map, "wireCalibrated")) return false;
                    CborEncoder wireCal;
                    if (cbor_encoder_create_map(map, &wireCal, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (int i = 0; i < 10; ++i) {
                        char key[4];
                        snprintf(key, sizeof(key), "%d", i + 1);
                        if (!WiFiCbor::encodeKvFloat(&wireTau, key,
                                                     CONF->GetDouble(kWireModelTauKeys[i],
                                                                     DEFAULT_WIRE_MODEL_TAU))) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvFloat(&wireK, key,
                                                     CONF->GetDouble(kWireModelKKeys[i],
                                                                     DEFAULT_WIRE_MODEL_K))) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvFloat(&wireC, key,
                                                     CONF->GetDouble(kWireModelCKeys[i],
                                                                     DEFAULT_WIRE_MODEL_C))) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvBool(&wireCal, key,
                                                    CONF->GetBool(kWireCalibDoneKeys[i],
                                                                  DEFAULT_CALIB_W_DONE))) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &wireTau) != CborNoError) return false;
                    if (cbor_encoder_close_container(map, &wireK) != CborNoError) return false;
                    if (cbor_encoder_close_container(map, &wireC) != CborNoError) return false;
                    if (cbor_encoder_close_container(map, &wireCal) != CborNoError) return false;

                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, "error");
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFi] Handling root request");
    if (lock()) { keepAlive = true; unlock(); }
    WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
}


