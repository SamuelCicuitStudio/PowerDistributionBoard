#pragma once

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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
        return;
    }
    WiFiCbor::sendPayload(request, status, payload);
}

void sendState_(AsyncWebServerRequest* request, const char* state, int status = 200) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, 96, [&](CborEncoder* map) {
            return WiFiCbor::encodeKvText(map, "state", state ? state : STATE_UNKNOWN);
        })) {
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
        request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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

static constexpr uint32_t kCalibWakeTimeoutMs = 15000;

bool ntcCalIsRunning_();
bool modelCalIsRunning_();
bool floorCalIsRunning_();
