#include <WiFiRoutesShared.hpp>

void WiFiManager::registerWireTestRoutes_() {
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
}
