#include <WiFiRoutesShared.hpp>

void WiFiManager::registerStaticRoutes_() {
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
