#include <WiFiRoutesShared.hpp>

void WiFiManager::registerSetupRoutes_() {
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
            if (ntcCalIsRunning_() ||
                modelCalIsRunning_() ||
                floorCalIsRunning_()) {
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
}
