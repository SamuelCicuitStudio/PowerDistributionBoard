#include <WiFiManager.hpp>
#include <WiFiCbor.hpp>
#include <WiFiLocalization.hpp>
#include <Utils.hpp>
#include <DeviceTransport.hpp>
#include <NtcSensor.hpp>
#include <math.h>

namespace {
static uint8_t getNtcGateIndexFromConfig() {
    int idx = DEFAULT_NTC_GATE_INDEX;
    if (CONF) {
        idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    }
    if (idx < 1) idx = 1;
    if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
    return static_cast<uint8_t>(idx);
}

static int getCurrentSourceSetting() {
    int src = DEFAULT_CURRENT_SOURCE;
    if (CONF) {
        src = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
    }
    if (src != CURRENT_SRC_ACS) {
        src = CURRENT_SRC_ESTIMATE;
    }
    return src;
}

static float readAcsCurrent() {
    if (DEVICE && DEVICE->currentSensor) {
        const float i = DEVICE->currentSensor->readCurrent();
        if (isfinite(i)) {
            return i;
        }
    }
    return NAN;
}

static float sampleCurrentFromSource(float busVoltage, uint16_t mask) {
    if (getCurrentSourceSetting() == CURRENT_SRC_ACS) {
        const float i = readAcsCurrent();
        if (isfinite(i)) {
            return i;
        }
    }
    if (WIRE && isfinite(busVoltage)) {
        const float i = WIRE->estimateCurrentFromVoltage(busVoltage, mask);
        if (isfinite(i)) {
            return i;
        }
    }
    return NAN;
}

constexpr size_t kMonitorCborMax = 4096;

static void appendBase64_(String& out, const uint8_t* data, size_t len) {
    static const char kBase64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16)
            | (static_cast<uint32_t>(data[i + 1]) << 8)
            | static_cast<uint32_t>(data[i + 2]);
        out += kBase64Table[(n >> 18) & 0x3F];
        out += kBase64Table[(n >> 12) & 0x3F];
        out += kBase64Table[(n >> 6) & 0x3F];
        out += kBase64Table[n & 0x3F];
        i += 3;
    }
    if (i < len) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        out += kBase64Table[(n >> 18) & 0x3F];
        out += kBase64Table[(n >> 12) & 0x3F];
        if (i + 1 < len) {
            out += kBase64Table[(n >> 6) & 0x3F];
            out += '=';
        } else {
            out += '=';
            out += '=';
        }
    }
}

template <typename BuildFn>
static bool buildCborBase64_(String& out, size_t capacity, BuildFn build) {
    std::vector<uint8_t> payload;
    if (!WiFiCbor::buildMapPayload(payload, capacity, build)) {
        return false;
    }
    out = "";
    const size_t outLen = ((payload.size() + 2) / 3) * 4;
    out.reserve(outLen);
    appendBase64_(out, payload.data(), payload.size());
    return true;
}
} // namespace

const char* WiFiManager::stateName(DeviceState s) {
    switch (s) {
        case DeviceState::Idle:     return STATE_IDLE;
        case DeviceState::Running:  return STATE_RUNNING;
        case DeviceState::Error:    return STATE_ERROR;
        case DeviceState::Shutdown: return STATE_SHUTDOWN;
        default:                    return STATE_UNKNOWN;
    }
}

void WiFiManager::startStateStreamTask() {
    if (stateStreamTaskHandle) return;

    // Send current snapshot on connect
    stateSse.onConnect([this](AsyncEventSourceClient* client) {
        if (wifiStatus == WiFiStatus::NotConnected) {
            client->close();
            return;
        }

        IPAddress ip =
            client->client() ? client->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (!sessionIpMatches_(ip)) {
            client->close();
            return;
        }

        Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
        String payload;
        const char* state = stateName(snap.state);
        if (!buildCborBase64_(payload, 96, [&](CborEncoder* map) {
                if (!WiFiCbor::encodeKvText(map, "state", state)) return false;
                if (!WiFiCbor::encodeKvUInt(map, "seq", snap.seq)) return false;
                return WiFiCbor::encodeKvUInt(map, "sinceMs", snap.sinceMs);
            })) {
            return;
        }
        client->send(payload.c_str(), SSE_EVENT_STATE, snap.seq);
    });

    BaseType_t ok = xTaskCreate(
        WiFiManager::stateStreamTask,
        "StateStreamTask",
        3072,
        this,
        1,
        &stateStreamTaskHandle
    );
    if (ok != pdPASS) {
        stateStreamTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFi] Failed to start StateStreamTask");
    }
}

void WiFiManager::stateStreamTask(void* pv) {
    WiFiManager* self = static_cast<WiFiManager*>(pv);
    DeviceTransport* dt = DEVTRAN;

    for (;;) {
        Device::StateSnapshot snap{};
        if (!dt->waitForStateEvent(snap, portMAX_DELAY)) continue;

        String payload;
        const char* state = stateName(snap.state);
        if (!buildCborBase64_(payload, 96, [&](CborEncoder* map) {
                if (!WiFiCbor::encodeKvText(map, "state", state)) return false;
                if (!WiFiCbor::encodeKvUInt(map, "seq", snap.seq)) return false;
                return WiFiCbor::encodeKvUInt(map, "sinceMs", snap.sinceMs);
            })) {
            continue;
        }
        self->stateSse.send(payload.c_str(), SSE_EVENT_STATE, snap.seq);
    }
}

void WiFiManager::startEventStreamTask() {
    if (eventStreamTaskHandle) return;

    eventSse.onConnect([this](AsyncEventSourceClient* client) {
        if (wifiStatus == WiFiStatus::NotConnected) {
            client->close();
            return;
        }

        IPAddress ip =
            client->client() ? client->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        if (!sessionIpMatches_(ip)) {
            client->close();
            return;
        }

        if (!DEVICE) return;

        uint8_t warnCount = 0;
        uint8_t errCount = 0;
        DEVICE->getUnreadEventCounts(warnCount, errCount);

        Device::EventEntry warnEntries[1]{};
        Device::EventEntry errEntries[1]{};
        const bool hasWarn = (DEVICE->getWarningHistory(warnEntries, 1) > 0);
        const bool hasErr = (DEVICE->getErrorHistory(errEntries, 1) > 0);
        const WiFiLang::UiLanguage lang = WiFiLang::getCurrentLanguage();

        String payload;
        if (!buildCborBase64_(payload, 512, [&](CborEncoder* map) {
                if (!WiFiCbor::encodeKvText(map, "kind", "snapshot")) return false;
                if (!WiFiCbor::encodeText(map, "unread")) return false;
                CborEncoder unread;
                if (cbor_encoder_create_map(map, &unread, CborIndefiniteLength) != CborNoError) {
                    return false;
                }
                if (!WiFiCbor::encodeKvUInt(&unread, "warn", warnCount)) return false;
                if (!WiFiCbor::encodeKvUInt(&unread, "error", errCount)) return false;
                if (cbor_encoder_close_container(map, &unread) != CborNoError) return false;

                if (hasWarn) {
                    if (!WiFiCbor::encodeText(map, "last_warning")) return false;
                    CborEncoder lastWarn;
                    if (cbor_encoder_create_map(map, &lastWarn, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    const String warnReason =
                        WiFiLang::translateReason(warnEntries[0].reason, lang);
                    if (!WiFiCbor::encodeKvText(&lastWarn, "reason", warnReason)) {
                        return false;
                    }
                    if (warnEntries[0].ms) {
                        if (!WiFiCbor::encodeKvUInt(&lastWarn, "ms", warnEntries[0].ms)) {
                            return false;
                        }
                    }
                    if (warnEntries[0].epoch) {
                        if (!WiFiCbor::encodeKvUInt(&lastWarn, "epoch", warnEntries[0].epoch)) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &lastWarn) != CborNoError) {
                        return false;
                    }
                }

                if (hasErr) {
                    if (!WiFiCbor::encodeText(map, "last_error")) return false;
                    CborEncoder lastErr;
                    if (cbor_encoder_create_map(map, &lastErr, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    const String errReason =
                        WiFiLang::translateReason(errEntries[0].reason, lang);
                    if (!WiFiCbor::encodeKvText(&lastErr, "reason", errReason)) {
                        return false;
                    }
                    if (errEntries[0].ms) {
                        if (!WiFiCbor::encodeKvUInt(&lastErr, "ms", errEntries[0].ms)) {
                            return false;
                        }
                    }
                    if (errEntries[0].epoch) {
                        if (!WiFiCbor::encodeKvUInt(&lastErr, "epoch", errEntries[0].epoch)) {
                            return false;
                        }
                    }
                    if (cbor_encoder_close_container(map, &lastErr) != CborNoError) {
                        return false;
                    }
                }

                return true;
            })) {
            return;
        }
        client->send(payload.c_str(), SSE_EVENT_EVENT, ++eventSeq);
    });

    BaseType_t ok = xTaskCreate(
        WiFiManager::eventStreamTask,
        "EventStreamTask",
        3072,
        this,
        1,
        &eventStreamTaskHandle
    );
    if (ok != pdPASS) {
        eventStreamTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFi] Failed to start EventStreamTask");
    }
}

void WiFiManager::eventStreamTask(void* pv) {
    WiFiManager* self = static_cast<WiFiManager*>(pv);

    for (;;) {
        if (!DEVICE) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        Device::EventNotice note{};
        if (!DEVICE->waitForEventNotice(note, portMAX_DELAY)) continue;

        String payload;
        const WiFiLang::UiLanguage lang = WiFiLang::getCurrentLanguage();
        const char* kind =
            (note.kind == Device::EventKind::Warning) ? "warning" : "error";
        if (!buildCborBase64_(payload, 256, [&](CborEncoder* map) {
                if (!WiFiCbor::encodeKvText(map, "kind", kind)) return false;
                const String reason = WiFiLang::translateReason(note.reason, lang);
                if (!WiFiCbor::encodeKvText(map, "reason", reason)) return false;
                if (note.ms) {
                    if (!WiFiCbor::encodeKvUInt(map, "ms", note.ms)) return false;
                }
                if (note.epoch) {
                    if (!WiFiCbor::encodeKvUInt(map, "epoch", note.epoch)) return false;
                }
                if (!WiFiCbor::encodeText(map, "unread")) return false;
                CborEncoder unread;
                if (cbor_encoder_create_map(map, &unread, CborIndefiniteLength) != CborNoError) {
                    return false;
                }
                if (!WiFiCbor::encodeKvUInt(&unread, "warn", note.unreadWarn)) return false;
                if (!WiFiCbor::encodeKvUInt(&unread, "error", note.unreadErr)) return false;
                return cbor_encoder_close_container(map, &unread) == CborNoError;
            })) {
            continue;
        }
        self->eventSse.send(payload.c_str(), SSE_EVENT_EVENT, ++self->eventSeq);
    }
}

void WiFiManager::startSnapshotTask(uint32_t periodMs) {
    if (_snapMtx == nullptr) {
        _snapMtx = xSemaphoreCreateMutex();
    }
    _monitorCbor.reserve(kMonitorCborMax);
    if (snapshotTaskHandle == nullptr) {
        xTaskCreate(
            WiFiManager::snapshotTask,
            "WiFiSnapshot",
            4096,
            reinterpret_cast<void*>(periodMs),
            1, // low priority
            &snapshotTaskHandle
        );
    }
}

void WiFiManager::snapshotTask(void* param) {
    const uint32_t periodMs =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    const TickType_t periodTicks =
        pdMS_TO_TICKS(periodMs ? periodMs : 250);

    WiFiManager* self = WiFiManager::Get();
    if (!self) {
        vTaskDelete(nullptr);
    }

    StatusSnapshot local{};
    std::vector<uint8_t> monitorCbor;
    monitorCbor.reserve(kMonitorCborMax);
    constexpr float kWireTargetMaxC = 150.0f;

    for (;;) {
        // Cap voltage & current (these should be cheap / cached)
        if (DEVICE && DEVICE->discharger) {
            local.capVoltage = DEVICE->discharger->readCapVoltage();
            local.capAdcScaled = DEVICE->discharger->readCapAdcScaled();
        } else {
            local.capVoltage = 0.0f;
            local.capAdcScaled = 0.0f;
        }
        const uint16_t mask = WIRE ? WIRE->getOutputMask() : 0;
        float currentA = sampleCurrentFromSource(local.capVoltage, mask);
        if (!isfinite(currentA)) currentA = 0.0f;
        local.current = currentA;
        float currentAcs = readAcsCurrent();
        if (!isfinite(currentAcs)) currentAcs = 0.0f;
        local.currentAcs = currentAcs;

        // Physical sensor temperatures for dashboard gauges.
        uint8_t n = 0;
        if (DEVICE && DEVICE->tempSensor) {
            n = DEVICE->tempSensor->getSensorCount();
            if (n > MAX_TEMP_SENSORS) n = MAX_TEMP_SENSORS;
            for (uint8_t i = 0; i < n; ++i) {
                const float t = DEVICE->tempSensor->getTemperature(i);
                local.temps[i] = isfinite(t) ? t : -127.0f;
            }
        }
        for (uint8_t i = n; i < MAX_TEMP_SENSORS; ++i) {
            local.temps[i] = -127.0f; // show as "off" when absent
        }

        float board0 = NAN;
        float board1 = NAN;
        float heatsink = NAN;
        if (DEVICE && DEVICE->tempSensor) {
            board0 = DEVICE->tempSensor->getBoardTemp(0);
            board1 = DEVICE->tempSensor->getBoardTemp(1);
            heatsink = DEVICE->tempSensor->getHeatsinkTemp();
        }
        float boardTemp = NAN;
        if (isfinite(board0) && isfinite(board1)) boardTemp = (board0 > board1) ? board0 : board1;
        else if (isfinite(board0)) boardTemp = board0;
        else if (isfinite(board1)) boardTemp = board1;

        // Virtual wire temps + outputs
        const WireConfigStore* cfg = (DEVICE ? &DEVICE->getWireConfigStore() : nullptr);
        for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
            const double wt = (WIRE
                               ? WIRE->getWireEstimatedTemp(i)
                               : NAN);
            const bool allowed = cfg ? cfg->getAccessFlag(i) : true;
            local.wireTemps[i - 1] = allowed ? (isfinite(wt) ? wt : NAN) : NAN;
            local.outputs[i - 1]   =
                (WIRE ? WIRE->getOutputState(i) : false);
            local.wirePresent[i - 1] =
                (WIRE ? WIRE->getWireInfo(i).connected : false);
        }
        if (NTC) {
            const uint8_t ntcIdx = getNtcGateIndexFromConfig();
            const float ntcTemp = NTC->getLastTempC();
            if (isfinite(ntcTemp)) {
                const bool allowed = cfg ? cfg->getAccessFlag(ntcIdx) : true;
                if (allowed) {
                    local.wireTemps[ntcIdx - 1] = ntcTemp;
                }
            }
        }

        // AC detect + relay state
        local.acPresent =
            (digitalRead(DETECT_12V_PIN) == HIGH);
        local.relayOn =
            (DEVICE && DEVICE->relayControl
             ? DEVICE->relayControl->isOn()
             : false);

        local.updatedMs = millis();

        // Prebuild the /monitor CBOR once per snapshot.
        float targetC = NAN;
        if (DEVICE) {
            const Device::WireTargetStatus wt = DEVICE->getWireTargetStatus();
            if (wt.active && isfinite(wt.targetC)) {
                targetC = wt.targetC;
            } else {
                const Device::FloorControlStatus fc = DEVICE->getFloorControlStatus();
                if (fc.active && isfinite(fc.wireTargetC)) {
                    targetC = fc.wireTargetC;
                } else {
                    float v = DEFAULT_NICHROME_FINAL_TEMP_C;
                    if (CONF) {
                        v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                           DEFAULT_NICHROME_FINAL_TEMP_C);
                    }
                    if (isfinite(v) && v > 0.0f) targetC = v;
                }
            }
        }
        if (isfinite(targetC)) {
            if (targetC > kWireTargetMaxC) targetC = kWireTargetMaxC;
            if (targetC < 0.0f) targetC = 0.0f;
        }

        monitorCbor.assign(kMonitorCborMax, 0);
        CborEncoder root;
        CborEncoder map;
        cbor_encoder_init(&root, monitorCbor.data(), monitorCbor.size(), 0);
        bool cborOk =
            (cbor_encoder_create_map(&root, &map, CborIndefiniteLength) == CborNoError);

        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "capVoltage", local.capVoltage);
        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "capAdcRaw", local.capAdcScaled);
        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "current", local.current);
        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "currentAcs", local.currentAcs);
        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "capacitanceF",
                                                     DEVICE ? DEVICE->getCapBankCapF() : 0.0f);

        if (cborOk) {
            cborOk = WiFiCbor::encodeText(&map, "temperatures");
        }
        CborEncoder temps;
        if (cborOk &&
            cbor_encoder_create_array(&map, &temps, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            for (uint8_t i = 0; i < MAX_TEMP_SENSORS; ++i) {
                if (cbor_encode_double(&temps, local.temps[i]) != CborNoError) {
                    cborOk = false;
                    break;
                }
            }
        }
        if (cborOk &&
            cbor_encoder_close_container(&map, &temps) != CborNoError) {
            cborOk = false;
        }

        if (cborOk) {
            const float boardOut = isfinite(boardTemp) ? boardTemp : -127.0f;
            const float heatOut = isfinite(heatsink) ? heatsink : -127.0f;
            cborOk = WiFiCbor::encodeKvFloat(&map, "boardTemp", boardOut);
            if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&map, "heatsinkTemp", heatOut);
        }
        if (cborOk && isfinite(targetC)) {
            cborOk = WiFiCbor::encodeKvFloat(&map, "wireTargetC", targetC);
        }

        if (cborOk && DEVICE) {
            const Device::FloorControlStatus fc = DEVICE->getFloorControlStatus();
            float floorTempC = NAN;
            if (NTC) {
                const float t = NTC->getLastTempC();
                if (isfinite(t)) floorTempC = t;
            }
            cborOk = WiFiCbor::encodeText(&map, "floor");
            CborEncoder floorMap;
            if (cborOk &&
                cbor_encoder_create_map(&map, &floorMap, CborIndefiniteLength) != CborNoError) {
                cborOk = false;
            }
            if (cborOk) cborOk = WiFiCbor::encodeKvBool(&floorMap, "active", fc.active);
            if (cborOk) cborOk = WiFiCbor::encodeKvFloatIfFinite(&floorMap, "target_c", fc.targetC);
            if (cborOk) {
                const float tempOut = isfinite(floorTempC) ? floorTempC : fc.tempC;
                if (isfinite(tempOut)) {
                    cborOk = WiFiCbor::encodeKvFloat(&floorMap, "temp_c", tempOut);
                }
            }
            if (cborOk) cborOk = WiFiCbor::encodeKvFloatIfFinite(&floorMap, "wire_target_c",
                                                                  fc.wireTargetC);
            if (cborOk && fc.updatedMs) {
                cborOk = WiFiCbor::encodeKvUInt(&floorMap, "updated_ms", fc.updatedMs);
            }
            if (cborOk &&
                cbor_encoder_close_container(&map, &floorMap) != CborNoError) {
                cborOk = false;
            }
        }

        if (cborOk) cborOk = WiFiCbor::encodeText(&map, "wireTemps");
        CborEncoder wireTemps;
        if (cborOk &&
            cbor_encoder_create_array(&map, &wireTemps, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                const double t = local.wireTemps[i];
                const int32_t v = isfinite(t) ? (int32_t)lround(t) : -127;
                if (cbor_encode_int(&wireTemps, v) != CborNoError) {
                    cborOk = false;
                    break;
                }
            }
        }
        if (cborOk &&
            cbor_encoder_close_container(&map, &wireTemps) != CborNoError) {
            cborOk = false;
        }

        if (cborOk) cborOk = WiFiCbor::encodeText(&map, "wirePresent");
        CborEncoder wirePresent;
        if (cborOk &&
            cbor_encoder_create_array(&map, &wirePresent, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                if (cbor_encode_boolean(&wirePresent, local.wirePresent[i]) != CborNoError) {
                    cborOk = false;
                    break;
                }
            }
        }
        if (cborOk &&
            cbor_encoder_close_container(&map, &wirePresent) != CborNoError) {
            cborOk = false;
        }

        if (cborOk) cborOk = WiFiCbor::encodeText(&map, "outputs");
        CborEncoder outputs;
        if (cborOk &&
            cbor_encoder_create_map(&map, &outputs, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                char key[12];
                snprintf(key, sizeof(key), "output%u", (unsigned)(i + 1));
                if (!WiFiCbor::encodeKvBool(&outputs, key, local.outputs[i])) {
                    cborOk = false;
                    break;
                }
            }
        }
        if (cborOk &&
            cbor_encoder_close_container(&map, &outputs) != CborNoError) {
            cborOk = false;
        }

        const Device::StateSnapshot snap = DEVTRAN->getStateSnapshot();
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "ready",
                                                    snap.state == DeviceState::Idle);
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "off",
                                                    snap.state == DeviceState::Shutdown);
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "ac", local.acPresent);
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "relay", local.relayOn);

        if (cborOk && DEVICE) {
            uint8_t warnCount = 0;
            uint8_t errCount = 0;
            DEVICE->getUnreadEventCounts(warnCount, errCount);
            cborOk = WiFiCbor::encodeText(&map, "eventUnread");
            CborEncoder unread;
            if (cborOk &&
                cbor_encoder_create_map(&map, &unread, CborIndefiniteLength) != CborNoError) {
                cborOk = false;
            }
            if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&unread, "warn", warnCount);
            if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&unread, "error", errCount);
            if (cborOk &&
                cbor_encoder_close_container(&map, &unread) != CborNoError) {
                cborOk = false;
            }

            Device::AmbientWaitStatus wait = DEVICE->getAmbientWaitStatus();
            if (cborOk) cborOk = WiFiCbor::encodeText(&map, "ambientWait");
            CborEncoder waitMap;
            if (cborOk &&
                cbor_encoder_create_map(&map, &waitMap, CborIndefiniteLength) != CborNoError) {
                cborOk = false;
            }
            if (cborOk) cborOk = WiFiCbor::encodeKvBool(&waitMap, "active", wait.active);
            if (cborOk && wait.active) {
                if (wait.sinceMs) {
                    cborOk = WiFiCbor::encodeKvUInt(&waitMap, "since_ms", wait.sinceMs);
                }
                if (cborOk && isfinite(wait.tolC)) {
                    cborOk = WiFiCbor::encodeKvFloat(&waitMap, "tol_c", wait.tolC);
                }
                if (cborOk && wait.reason[0]) {
                    const WiFiLang::UiLanguage lang = WiFiLang::getCurrentLanguage();
                    const String waitReason = WiFiLang::translateReason(wait.reason, lang);
                    cborOk = WiFiCbor::encodeKvText(&waitMap, "reason", waitReason);
                }
            }
            if (cborOk &&
                cbor_encoder_close_container(&map, &waitMap) != CborNoError) {
                cborOk = false;
            }
        }

        if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&map, "fanSpeed", FAN->getSpeedPercent());
        const wifi_mode_t mode = WiFi.getMode();
        const bool staMode = (mode == WIFI_STA || mode == WIFI_AP_STA);
        const bool staConnected = (WiFi.status() == WL_CONNECTED);
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "wifiSta", staMode);
        if (cborOk) cborOk = WiFiCbor::encodeKvBool(&map, "wifiConnected", staConnected);
        if (cborOk && staMode && staConnected) {
            cborOk = WiFiCbor::encodeKvInt(&map, "wifiRssi", WiFi.RSSI());
        }

        if (cborOk) cborOk = WiFiCbor::encodeText(&map, "sessionTotals");
        CborEncoder totals;
        if (cborOk &&
            cbor_encoder_create_map(&map, &totals, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&totals, "totalEnergy_Wh",
                                                     POWER_TRACKER->getTotalEnergy_Wh());
        if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&totals, "totalSessions",
                                                    POWER_TRACKER->getTotalSessions());
        if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&totals, "totalSessionsOk",
                                                    POWER_TRACKER->getTotalSuccessful());
        if (cborOk &&
            cbor_encoder_close_container(&map, &totals) != CborNoError) {
            cborOk = false;
        }

        if (cborOk) cborOk = WiFiCbor::encodeText(&map, "session");
        CborEncoder sess;
        if (cborOk &&
            cbor_encoder_create_map(&map, &sess, CborIndefiniteLength) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            PowerTracker::SessionStats cur =
                POWER_TRACKER->getCurrentSessionSnapshot();
            const auto& last = POWER_TRACKER->getLastSession();

            if (cur.valid) {
                cborOk = WiFiCbor::encodeKvBool(&sess, "valid", true);
                if (cborOk) cborOk = WiFiCbor::encodeKvBool(&sess, "running", true);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "energy_Wh", cur.energy_Wh);
                if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&sess, "duration_s", cur.duration_s);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "peakPower_W", cur.peakPower_W);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "peakCurrent_A",
                                                            cur.peakCurrent_A);
            } else if (last.valid) {
                cborOk = WiFiCbor::encodeKvBool(&sess, "valid", true);
                if (cborOk) cborOk = WiFiCbor::encodeKvBool(&sess, "running", false);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "energy_Wh", last.energy_Wh);
                if (cborOk) cborOk = WiFiCbor::encodeKvUInt(&sess, "duration_s", last.duration_s);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "peakPower_W", last.peakPower_W);
                if (cborOk) cborOk = WiFiCbor::encodeKvFloat(&sess, "peakCurrent_A",
                                                            last.peakCurrent_A);
            } else {
                cborOk = WiFiCbor::encodeKvBool(&sess, "valid", false);
                if (cborOk) cborOk = WiFiCbor::encodeKvBool(&sess, "running", false);
            }
        }
        if (cborOk &&
            cbor_encoder_close_container(&map, &sess) != CborNoError) {
            cborOk = false;
        }

        if (cborOk &&
            cbor_encoder_close_container(&root, &map) != CborNoError) {
            cborOk = false;
        }
        if (cborOk) {
            const size_t size = cbor_encoder_get_buffer_size(&root, monitorCbor.data());
            monitorCbor.resize(size);
        } else {
            monitorCbor.clear();
        }

        // Commit snapshot under lock
        if (self->_snapMtx &&
            xSemaphoreTake(self->_snapMtx, portMAX_DELAY) == pdTRUE)
        {
            self->_snap = local;
            self->_monitorCbor.swap(monitorCbor);
            self->pushLiveSample(local);
            xSemaphoreGive(self->_snapMtx);
        }

        vTaskDelay(periodTicks);
    }
}

bool WiFiManager::getSnapshot(StatusSnapshot& out) {
    if (_snapMtx == nullptr) return false;
    if (xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(25)) != pdTRUE) {
        return false;
    }
    out = _snap;
    xSemaphoreGive(_snapMtx);
    return true;
}

bool WiFiManager::getMonitorCbor(std::vector<uint8_t>& out) {
    if (_snapMtx == nullptr) return false;
    if (xSemaphoreTake(_snapMtx, pdMS_TO_TICKS(25)) != pdTRUE) {
        return false;
    }
    if (_monitorCbor.empty()) {
        xSemaphoreGive(_snapMtx);
        return false;
    }
    out = _monitorCbor;
    xSemaphoreGive(_snapMtx);
    return true;
}

void WiFiManager::pushLiveSample(const StatusSnapshot& s) {
    LiveSample sm{};
    sm.seq = ++_liveSeqCtr;
    sm.tsMs = s.updatedMs ? s.updatedMs : millis();
    sm.capV = s.capVoltage;
    sm.currentA = s.current;

    uint16_t mask = 0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (s.outputs[i]) {
            mask |= static_cast<uint16_t>(1u << i);
        }
        const double t = s.wireTemps[i];
        sm.wireTemps[i] = static_cast<int16_t>(
            (isfinite(t) ? lround(t) : -127));
    }
    sm.outputsMask = mask;
    sm.relay = s.relayOn;
    sm.ac = s.acPresent;
    sm.fanPct = FAN ? FAN->getSpeedPercent() : 0;

    _liveBuf[_liveHead] = sm;
    _liveHead = (_liveHead + 1) % kLiveBufSize;
    if (_liveCount < kLiveBufSize) {
        _liveCount++;
    }
}

bool WiFiManager::buildLiveBatch(CborEncoder* items, uint32_t sinceSeq, uint32_t& seqStart, uint32_t& seqEnd) {
    if (!items) return false;

    seqStart = 0;
    seqEnd = 0;

    size_t count = _liveCount;
    if (count == 0) return false;

    size_t tail = (_liveHead + kLiveBufSize - count) % kLiveBufSize;

    for (size_t i = 0; i < count; ++i) {
        const size_t idx = (tail + i) % kLiveBufSize;
        const LiveSample& sm = _liveBuf[idx];
        if (sm.seq <= sinceSeq) continue;

        if (seqStart == 0) seqStart = sm.seq;
        seqEnd = sm.seq;

        CborEncoder entry;
        if (cbor_encoder_create_map(items, &entry, CborIndefiniteLength) != CborNoError) {
            return false;
        }
        if (!WiFiCbor::encodeKvUInt(&entry, "seq", sm.seq)) return false;
        if (!WiFiCbor::encodeKvUInt(&entry, "ts", sm.tsMs)) return false;
        if (!WiFiCbor::encodeKvFloat(&entry, "capV", sm.capV)) return false;
        if (!WiFiCbor::encodeKvFloat(&entry, "i", sm.currentA)) return false;
        if (!WiFiCbor::encodeKvUInt(&entry, "mask", sm.outputsMask)) return false;
        if (!WiFiCbor::encodeKvBool(&entry, "relay", sm.relay)) return false;
        if (!WiFiCbor::encodeKvBool(&entry, "ac", sm.ac)) return false;
        if (!WiFiCbor::encodeKvUInt(&entry, "fan", sm.fanPct)) return false;

        if (!WiFiCbor::encodeText(&entry, "wireTemps")) return false;
        CborEncoder wt;
        if (cbor_encoder_create_array(&entry, &wt, CborIndefiniteLength) != CborNoError) {
            return false;
        }
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            if (cbor_encode_int(&wt, sm.wireTemps[w]) != CborNoError) {
                return false;
            }
        }
        if (cbor_encoder_close_container(&entry, &wt) != CborNoError) {
            return false;
        }
        if (cbor_encoder_close_container(items, &entry) != CborNoError) {
            return false;
        }
    }

    return (seqStart != 0);
}

void WiFiManager::startLiveStreamTask(uint32_t /*emitPeriodMs*/) {
    // Live streaming disabled; clients poll snapshots instead.
}

void WiFiManager::liveStreamTask(void* /*pv*/) {
    // Live streaming task disabled.
    vTaskDelete(nullptr);
}

