#include <WiFiRoutesShared.hpp>

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

    registerStateRoutes_();
    registerMonitorRoutes_();
    registerAuthRoutes_();
    registerDeviceInfoRoutes_();
    registerHistoryRoutes_();
    registerAdminRoutes_();
    registerCalibrationRoutes_();
    registerWireTestRoutes_();
    registerPresenceRoutes_();
    registerSetupRoutes_();
    registerNtcCalRoutes_();
    registerControlRoutes_();
    registerStaticRoutes_();
}
