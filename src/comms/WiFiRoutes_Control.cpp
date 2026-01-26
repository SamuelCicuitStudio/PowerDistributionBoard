#include <WiFiRoutesShared.hpp>
#include <WiFiLocalization.hpp>

void WiFiManager::registerControlRoutes_() {
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
                else if (target == "uiLanguage" || target == "language") {
                    String langRaw;
                    if (!readValueString(langRaw)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    const String norm = WiFiLang::normalizeLanguageCode(langRaw);
                    CONF->PutString(UI_LANGUAGE_KEY, norm);
                    sendStatusApplied_(request);
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
                else if (target == "presenceMinRatio" || target == "presenceMinRatioPct") {
                    float v = 0.0f;
                    if (!readValueFloat(v)) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }
                    float ratio = v;
                    if (target == "presenceMinRatioPct" || ratio > 1.0f) {
                        ratio = v / 100.0f;
                    }
                    if (!isfinite(ratio) || ratio <= 0.0f) {
                        ratio = DEFAULT_PRESENCE_MIN_RATIO;
                    }
                    if (ratio < 0.10f) ratio = 0.10f;
                    if (ratio > 1.00f) ratio = 1.00f;
                    CONF->PutFloat(PRESENCE_MIN_RATIO_KEY, ratio);
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
                    if (!WiFiCbor::encodeKvText(map, "uiLanguage",
                                                CONF->GetString(UI_LANGUAGE_KEY,
                                                                DEFAULT_UI_LANGUAGE))) return false;
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
                    {
                        float ratio = CONF->GetFloat(PRESENCE_MIN_RATIO_KEY,
                                                     DEFAULT_PRESENCE_MIN_RATIO);
                        if (!isfinite(ratio) || ratio <= 0.0f) {
                            ratio = DEFAULT_PRESENCE_MIN_RATIO;
                        }
                        const float pct = ratio * 100.0f;
                        if (!WiFiCbor::encodeKvFloat(map, "presenceMinRatioPct", pct)) return false;
                    }
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );
}
