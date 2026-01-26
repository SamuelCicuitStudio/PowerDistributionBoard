#include <WiFiRoutesShared.hpp>

void WiFiManager::registerAuthRoutes_() {
    // ---- Login page ----
    server.on(EP_LOGIN, HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (lock()) { lastActivityMillis = millis(); unlock(); }
        handleRoot(request);
    });

    // ---- Login connect ----
    server.on(EP_CONNECT, HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            collectCborBody_(request, data, len, index, total,
                [this](AsyncWebServerRequest* request, const std::vector<uint8_t>& body) {
                    String username;
                    String password;
                    const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                        if (strcmp(key, "username") == 0) {
                            return readCborText_(it, username);
                        }
                        if (strcmp(key, "password") == 0) {
                            return readCborText_(it, password);
                        }
                        return skipCborValue_(it);
                    });
                    if (!parsed) {
                        WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                        return;
                    }

                    if (username.isEmpty() || password.isEmpty()) {
                        WiFiCbor::sendError(request, 400, ERR_MISSING_FIELDS);
                        return;
                    }

                    if (wifiStatus != WiFiStatus::NotConnected) {
                        WiFiCbor::sendError(request, 403, ERR_ALREADY_CONNECTED);
                        return;
                    }

                    String adminUser = CONF->GetString(ADMIN_ID_KEY, "");
                    String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
                    String userUser  = CONF->GetString(USER_ID_KEY, "");
                    String userPass  = CONF->GetString(USER_PASS_KEY, "");

                    auto sendLogin = [&](const char* role) {
                        const String token =
                            issueSessionToken_(request->client()->remoteIP());
                        const bool setupDone =
                            CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
                        const bool setupConfigOk = checkSetupConfig(nullptr);
                        const bool setupCalibOk = checkSetupCalib(nullptr);
                        std::vector<uint8_t> payload;
                        if (!WiFiCbor::buildMapPayload(payload, 192, [&](CborEncoder* map) {
                                if (!WiFiCbor::encodeKvBool(map, "ok", true)) return false;
                                if (!WiFiCbor::encodeKvText(map, "role", role)) return false;
                                if (!WiFiCbor::encodeKvText(map, "token", token)) return false;
                                if (!WiFiCbor::encodeKvBool(map, "setupDone", setupDone)) return false;
                                if (!WiFiCbor::encodeKvBool(map, "setupRunAllowed",
                                                            setupDone && setupConfigOk)) return false;
                                return WiFiCbor::encodeKvBool(map, "setupCalibPending",
                                                              setupDone && setupConfigOk && !setupCalibOk);
                            })) {
                            request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                            return;
                        }
                        WiFiCbor::sendPayload(request, 200, payload);
                    };

                    if (username == adminUser && password == adminPass) {
                        BUZZ->successSound();
                        onAdminConnected();
                        RGB->postOverlay(OverlayEvent::WEB_ADMIN_ACTIVE);
                        sendLogin("admin");
                        return;
                    }
                    if (username == userUser && password == userPass) {
                        const bool setupDone =
                            CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
                        if (!setupDone) {
                            WiFiCbor::sendError(request, 403, ERR_SETUP_REQUIRED);
                            return;
                        }
                        BUZZ->successSound();
                        onUserConnected();
                        RGB->postOverlay(OverlayEvent::WEB_USER_ACTIVE);
                        sendLogin("user");
                        return;
                    }

                    BUZZ->bipFault();
                    WiFiCbor::sendError(request, 401, ERR_BAD_PASSWORD);
                });
        }
    );

    // ---- Disconnect ----
    server.on(EP_DISCONNECT, HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            String action;
            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "action") == 0) {
                    return readCborText_(it, action);
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            if (action != "disconnect") {
                WiFiCbor::sendError(request, 400, ERR_INVALID_ACTION);
                return;
            }

            onDisconnected();
            if (lock()) {
                lastActivityMillis = millis();
                keepAlive = false;
                unlock();
            }
            RGB->postOverlay(OverlayEvent::WIFI_LOST);
            sendOk_(request);
        }
    );
}
