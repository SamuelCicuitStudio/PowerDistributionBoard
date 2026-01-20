#ifndef CBOR_STREAM_H
#define CBOR_STREAM_H

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace CborStream {

inline bool writeByte(Print& out, uint8_t value) {
    return out.write(&value, 1) == 1;
}

inline bool writeUintBE(Print& out, uint64_t value, uint8_t bytes) {
    for (uint8_t i = 0; i < bytes; ++i) {
        const uint8_t shift = static_cast<uint8_t>(8U * (bytes - 1U - i));
        if (!writeByte(out, static_cast<uint8_t>(value >> shift))) {
            return false;
        }
    }
    return true;
}

inline bool writeMajorAndLen(Print& out, uint8_t major, uint64_t len) {
    if (len < 24) {
        return writeByte(out, static_cast<uint8_t>((major << 5) | len));
    }
    if (len <= 0xFF) {
        if (!writeByte(out, static_cast<uint8_t>((major << 5) | 24))) return false;
        return writeByte(out, static_cast<uint8_t>(len));
    }
    if (len <= 0xFFFF) {
        if (!writeByte(out, static_cast<uint8_t>((major << 5) | 25))) return false;
        return writeUintBE(out, len, 2);
    }
    if (len <= 0xFFFFFFFFULL) {
        if (!writeByte(out, static_cast<uint8_t>((major << 5) | 26))) return false;
        return writeUintBE(out, len, 4);
    }
    if (!writeByte(out, static_cast<uint8_t>((major << 5) | 27))) return false;
    return writeUintBE(out, len, 8);
}

inline bool writeUInt(Print& out, uint64_t value) {
    return writeMajorAndLen(out, 0, value);
}

inline bool writeInt(Print& out, int64_t value) {
    if (value >= 0) {
        return writeMajorAndLen(out, 0, static_cast<uint64_t>(value));
    }
    const uint64_t neg = static_cast<uint64_t>(-1 - value);
    return writeMajorAndLen(out, 1, neg);
}

inline bool writeBool(Print& out, bool value) {
    return writeByte(out, value ? 0xF5 : 0xF4);
}

inline bool writeNull(Print& out) {
    return writeByte(out, 0xF6);
}

inline bool writeDouble(Print& out, double value) {
    union {
        double d;
        uint64_t u;
    } conv;
    conv.d = value;
    if (!writeByte(out, 0xFB)) return false;
    return writeUintBE(out, conv.u, 8);
}

inline bool writeFloatOrNull(Print& out, double value) {
    if (!isfinite(value)) {
        return writeNull(out);
    }
    return writeDouble(out, value);
}

inline bool writeText(Print& out, const char* text) {
    const char* safe = text ? text : "";
    const size_t len = strlen(safe);
    if (!writeMajorAndLen(out, 3, len)) return false;
    return out.write(reinterpret_cast<const uint8_t*>(safe), len) == len;
}

inline bool writeText(Print& out, const String& text) {
    if (!writeMajorAndLen(out, 3, text.length())) return false;
    return out.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()) == text.length();
}

inline bool writeArrayHeader(Print& out, uint64_t count) {
    return writeMajorAndLen(out, 4, count);
}

inline bool writeMapHeader(Print& out, uint64_t count) {
    return writeMajorAndLen(out, 5, count);
}

} // namespace CborStream

#endif // CBOR_STREAM_H
