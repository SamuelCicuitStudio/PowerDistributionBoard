#include <WiFiRoutesShared.hpp>

void WiFiManager::registerPresenceRoutes_() {
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

            float minRatio = CONF->GetFloat(PRESENCE_MIN_RATIO_KEY,
                                            DEFAULT_PRESENCE_MIN_RATIO);

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "presenceMinRatio") == 0) {
                        double v = NAN;
                        if (!readCborDouble_(it, v)) return false;
                        if (isfinite(v)) {
                            minRatio = static_cast<float>((v > 1.0) ? (v / 100.0) : v);
                        }
                        return true;
                    }
                    if (strcmp(key, "presenceMinRatioPct") == 0) {
                        double v = NAN;
                        if (!readCborDouble_(it, v)) return false;
                        if (isfinite(v)) {
                            minRatio = static_cast<float>(v / 100.0);
                        }
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

            if (!isfinite(minRatio) || minRatio <= 0.0f) {
                minRatio = DEFAULT_PRESENCE_MIN_RATIO;
            }
            if (minRatio < 0.10f) minRatio = 0.10f;
            if (minRatio > 1.00f) minRatio = 1.00f;

            CONF->PutFloat(PRESENCE_MIN_RATIO_KEY, minRatio);

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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );
}
