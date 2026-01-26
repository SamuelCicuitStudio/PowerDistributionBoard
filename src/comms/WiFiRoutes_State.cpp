#include <WiFiRoutesShared.hpp>

void WiFiManager::registerStateRoutes_() {
    // ---- State stream (SSE) ----
    server.addHandler(&stateSse);
    // ---- Event stream (SSE) ----
    server.addHandler(&eventSse);
}
