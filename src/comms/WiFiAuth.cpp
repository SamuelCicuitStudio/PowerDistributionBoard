#include <WiFiManager.hpp>
#include <WiFiCbor.hpp>
#include <Utils.hpp>
#include <esp_system.h>

String WiFiManager::issueSessionToken_(const IPAddress& ip) {
    char buf[25];
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const uint32_t r3 = esp_random();
    snprintf(buf, sizeof(buf), "%08lx%08lx%08lx",
             static_cast<unsigned long>(r1),
             static_cast<unsigned long>(r2),
             static_cast<unsigned long>(r3));

    if (lock()) {
        _sessionToken = buf;
        _sessionIp = ip;
        unlock();
    } else {
        _sessionToken = buf;
        _sessionIp = ip;
    }

    return _sessionToken;
}

bool WiFiManager::validateSession_(AsyncWebServerRequest* request) const {
    String sessionToken;
    IPAddress sessionIp;
    if (lock()) {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
        unlock();
    } else {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
    }

    if (sessionToken.isEmpty()) return false;

    String token;
    if (request->hasHeader("X-Session-Token")) {
        token = request->getHeader("X-Session-Token")->value();
    }
    if (token.isEmpty() && request->hasParam("token")) {
        token = request->getParam("token")->value();
    }
    if (token.isEmpty() || token != sessionToken) return false;

    if (sessionIp != IPAddress(0, 0, 0, 0)) {
        IPAddress ip =
            request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (ip != sessionIp) return false;
    }

    return true;
}

bool WiFiManager::sessionIpMatches_(const IPAddress& ip) const {
    String sessionToken;
    IPAddress sessionIp;
    if (lock()) {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
        unlock();
    } else {
        sessionToken = _sessionToken;
        sessionIp = _sessionIp;
    }

    if (sessionToken.isEmpty()) return false;
    if (sessionIp == IPAddress(0, 0, 0, 0)) return true;
    return ip == sessionIp;
}

void WiFiManager::clearSession_() {
    if (lock()) {
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
        unlock();
    } else {
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
    }
}

void WiFiManager::onUserConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::UserConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] User connected");
    RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
}

void WiFiManager::onAdminConnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::AdminConnected;
        unlock();
    }
    heartbeat();
    DEBUG_PRINTLN("[WiFi] Admin connected ");
    RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
}

void WiFiManager::onDisconnected() {
    if (lock()) {
        wifiStatus = WiFiStatus::NotConnected;
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
        unlock();
    } else {
        wifiStatus = WiFiStatus::NotConnected;
        _sessionToken = "";
        _sessionIp = IPAddress(0, 0, 0, 0);
    }
    DEBUG_PRINTLN("[WiFi] All clients disconnected");
    RGB->postOverlay(OverlayEvent::WIFI_LOST);
}

bool WiFiManager::isUserConnected() const {
    return wifiStatus == WiFiStatus::UserConnected;
}

bool WiFiManager::isAdminConnected() const {
    return wifiStatus == WiFiStatus::AdminConnected;
}

bool WiFiManager::isAuthenticated(AsyncWebServerRequest* request) {
    if (wifiStatus == WiFiStatus::NotConnected) {
        WiFiCbor::sendError(request, 401, ERR_NOT_AUTHENTICATED);
        return false;
    }
    if (!validateSession_(request)) {
        WiFiCbor::sendError(request, 401, ERR_NOT_AUTHENTICATED);
        return false;
    }
    if (lock()) {
        lastActivityMillis = millis();
        keepAlive = true;
        unlock();
    }
    return true;
}

bool WiFiManager::isWifiOn() const {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool on = WifiState;
        xSemaphoreGive(_mutex);
        return on;
    }
    return WifiState;
}

