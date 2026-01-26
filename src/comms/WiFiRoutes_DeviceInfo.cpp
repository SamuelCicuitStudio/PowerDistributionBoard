#include <WiFiRoutesShared.hpp>
#include <WiFiLocalization.hpp>

void WiFiManager::registerDeviceInfoRoutes_() {
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
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
                        const WiFiLang::UiLanguage lang = WiFiLang::getCurrentLanguage();
                        if (!WiFiCbor::encodeText(map, "last_error")) return false;
                        CborEncoder err;
                        if (cbor_encoder_create_map(map, &err, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (info.hasError) {
                            const String errReason =
                                WiFiLang::translateReason(info.errorReason, lang);
                            if (!WiFiCbor::encodeKvText(&err, "reason", errReason)) return false;
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
                            const String stopReason =
                                WiFiLang::translateReason(info.stopReason, lang);
                            if (!WiFiCbor::encodeKvText(&stop, "reason", stopReason)) return false;
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
                            const String warnReason = WiFiLang::translateReason(e.reason, lang);
                            if (!WiFiCbor::encodeKvText(&item, "reason", warnReason)) return false;
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
                            const String errReason = WiFiLang::translateReason(e.reason, lang);
                            if (!WiFiCbor::encodeKvText(&item, "reason", errReason)) return false;
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );
}
