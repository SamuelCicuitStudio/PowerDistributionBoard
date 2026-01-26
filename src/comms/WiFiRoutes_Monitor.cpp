#include <WiFiRoutesShared.hpp>

void WiFiManager::registerMonitorRoutes_() {
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
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );
    // ---- Live monitor stream (SSE) ----
    server.addHandler(&liveSse);

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
}
