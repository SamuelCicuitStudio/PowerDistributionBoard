#include <WiFiRoutesShared.hpp>

void WiFiManager::registerHistoryRoutes_() {
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );
}
