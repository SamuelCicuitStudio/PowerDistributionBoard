#ifndef WIFI_CBOR_H
#define WIFI_CBOR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cbor.h>
#include <math.h>
#include <vector>

#include <WifiEnpoin.hpp>
#include <WiFiLocalization.hpp>

namespace WiFiCbor {

inline bool encodeText(CborEncoder* enc, const char* text) {
    return text && cbor_encode_text_stringz(enc, text) == CborNoError;
}

inline bool encodeKvText(CborEncoder* map, const char* key, const char* value) {
    return encodeText(map, key) && encodeText(map, value ? value : "");
}

inline bool encodeKvText(CborEncoder* map, const char* key, const String& value) {
    return encodeText(map, key) && encodeText(map, value.c_str());
}

inline bool encodeKvBool(CborEncoder* map, const char* key, bool value) {
    return encodeText(map, key) && cbor_encode_boolean(map, value) == CborNoError;
}

inline bool encodeKvUInt(CborEncoder* map, const char* key, uint64_t value) {
    return encodeText(map, key) && cbor_encode_uint(map, value) == CborNoError;
}

inline bool encodeKvInt(CborEncoder* map, const char* key, int64_t value) {
    return encodeText(map, key) && cbor_encode_int(map, value) == CborNoError;
}

inline bool encodeKvFloat(CborEncoder* map, const char* key, double value) {
    return encodeText(map, key) && cbor_encode_double(map, value) == CborNoError;
}

inline bool encodeKvFloatIfFinite(CborEncoder* map, const char* key, double value) {
    if (!isfinite(value)) return true;
    return encodeKvFloat(map, key, value);
}

template <typename BuildFn>
bool buildMapPayload(std::vector<uint8_t>& out, size_t capacity, BuildFn&& build) {
    out.assign(capacity, 0);
    CborEncoder root;
    CborEncoder map;
    cbor_encoder_init(&root, out.data(), out.size(), 0);
    if (cbor_encoder_create_map(&root, &map, CborIndefiniteLength) != CborNoError) {
        return false;
    }
    if (!build(&map)) {
        return false;
    }
    if (cbor_encoder_close_container(&root, &map) != CborNoError) {
        return false;
    }
    const size_t size = cbor_encoder_get_buffer_size(&root, out.data());
    out.resize(size);
    return true;
}

inline void sendPayload(AsyncWebServerRequest* request,
                        int status,
                        const std::vector<uint8_t>& payload,
                        const char* cacheControl = nullptr) {
    if (!request) return;
    AsyncResponseStream* response = request->beginResponseStream(CT_APP_CBOR);
    response->setCode(status);
    if (cacheControl && *cacheControl) {
        response->addHeader("Cache-Control", cacheControl);
    }
    response->write(payload.data(), payload.size());
    request->send(response);
}

inline bool buildErrorPayload(std::vector<uint8_t>& out,
                              size_t capacity,
                              const char* message,
                              const char* detail = nullptr,
                              const char* state = nullptr) {
    if (!message) return false;
    return buildMapPayload(out, capacity, [&](CborEncoder* map) {
        if (!encodeKvText(map, "error", message)) return false;
        if (detail && *detail) {
            if (!encodeKvText(map, "detail", detail)) return false;
        }
        if (state && *state) {
            if (!encodeKvText(map, SSE_EVENT_STATE, state)) return false;
        }
        return true;
    });
}

inline void sendError(AsyncWebServerRequest* request,
                      int status,
                      const char* message,
                      const char* detail = nullptr,
                      const char* state = nullptr,
                      const char* cacheControl = nullptr) {
    const WiFiLang::UiLanguage lang = WiFiLang::getCurrentLanguage();
    const char* localized = WiFiLang::translateErrorMessage(message, lang);
    std::vector<uint8_t> payload;
    if (!buildErrorPayload(payload, 192, localized, detail, state)) {
        request->send(status, CT_TEXT_PLAIN, WiFiLang::getPlainError());
        return;
    }
    sendPayload(request, status, payload, cacheControl);
}

} // namespace WiFiCbor

#endif // WIFI_CBOR_H
