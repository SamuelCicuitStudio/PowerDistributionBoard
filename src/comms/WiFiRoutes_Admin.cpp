#include <WiFiRoutesShared.hpp>

void WiFiManager::registerAdminRoutes_() {
    // ---- Device log ----
    server.on(EP_DEVICE_LOG, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            AsyncResponseStream* response =
                request->beginResponseStream(CT_TEXT_PLAIN);
            Debug::writeMemoryLog(*response);
            request->send(response);
        }
    );

    server.on(EP_DEVICE_LOG_CLEAR, HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            Debug::clearMemoryLog();
            sendOk_(request);
        }
    );

    // ---- Access Point settings ----
    server.on(EP_AP_CONFIG, HTTP_POST,
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
            collectCborBody_(request, data, len, index, total,
                [this](AsyncWebServerRequest* request, const std::vector<uint8_t>& body) {
                    if (!isAuthenticated(request)) {
                        return;
                    }
                    String newSsid;
                    String newPass;
                    const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                        if (strcmp(key, "apSSID") == 0) {
                            return readCborText_(it, newSsid);
                        }
                        if (strcmp(key, "apPassword") == 0) {
                            return readCborText_(it, newPass);
                        }
                        return skipCborValue_(it);
                    });
                    if (!parsed) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }

                    bool changed = false;
                    if (newSsid.length()) {
                        const String current =
                            CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY,
                                            DEVICE_WIFI_HOTSPOT_NAME);
                        if (newSsid != current) {
                            CONF->PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY, newSsid);
                            changed = true;
                        }
                    }
                    if (newPass.length()) {
                        const String current =
                            CONF->GetString(DEVICE_AP_AUTH_PASS_KEY,
                                            DEVICE_AP_AUTH_PASS_DEFAULT);
                        if (newPass != current) {
                            CONF->PutString(DEVICE_AP_AUTH_PASS_KEY, newPass);
                            changed = true;
                        }
                    }

                    sendStatusApplied_(request);

                    if (changed) {
                        CONF->RestartSysDelayDown(3000);
                    }
                });
        }
    );
}
