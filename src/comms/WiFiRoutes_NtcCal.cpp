#include <WiFiRoutesShared.hpp>

namespace {
constexpr float kNtcCalTargetDefaultC = DEFAULT_NTC_CAL_TARGET_C;
constexpr uint32_t kNtcCalSampleMsDefault = static_cast<uint32_t>(DEFAULT_NTC_CAL_SAMPLE_MS);
constexpr uint32_t kNtcCalTimeoutMs = static_cast<uint32_t>(DEFAULT_NTC_CAL_TIMEOUT_MS);
constexpr uint32_t kNtcCalMinSamples = 6;

struct NtcCalStatus {
    bool     running   = false;
    bool     done      = false;
    bool     error     = false;
    char     errorMsg[96] = {0};
    uint32_t startMs   = 0;
    uint32_t elapsedMs = 0;
    float    targetC   = NAN;
    float    heatsinkC = NAN;
    float    ntcOhm    = NAN;
    uint32_t sampleMs  = 0;
    uint32_t samples   = 0;
    float    shA       = NAN;
    float    shB       = NAN;
    float    shC       = NAN;
    uint8_t  wireIndex = 0;
};

struct NtcCalTaskArgs {
    float    targetC   = kNtcCalTargetDefaultC;
    uint8_t  wireIndex = 1;
    uint32_t sampleMs  = kNtcCalSampleMsDefault;
    uint32_t timeoutMs = kNtcCalTimeoutMs;
    uint32_t startMs   = 0;
};

static SemaphoreHandle_t s_ntcCalMtx = nullptr;
static TaskHandle_t s_ntcCalTask = nullptr;
static NtcCalStatus s_ntcCalStatus{};
static bool s_ntcCalAbort = false;

static void ntcCalEnsureMutex() {
    if (!s_ntcCalMtx) {
        s_ntcCalMtx = xSemaphoreCreateMutex();
    }
}

static bool ntcCalLock(TickType_t timeoutTicks = portMAX_DELAY) {
    ntcCalEnsureMutex();
    if (!s_ntcCalMtx) return true;
    return (xSemaphoreTake(s_ntcCalMtx, timeoutTicks) == pdTRUE);
}

static void ntcCalUnlock() {
    if (s_ntcCalMtx) xSemaphoreGive(s_ntcCalMtx);
}

static void ntcCalStartStatus(const NtcCalTaskArgs& args) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = true;
    s_ntcCalStatus.done = false;
    s_ntcCalStatus.error = false;
    s_ntcCalStatus.errorMsg[0] = '\0';
    s_ntcCalAbort = false;
    s_ntcCalStatus.startMs = args.startMs;
    s_ntcCalStatus.elapsedMs = 0;
    s_ntcCalStatus.targetC = args.targetC;
    s_ntcCalStatus.heatsinkC = NAN;
    s_ntcCalStatus.ntcOhm = NAN;
    s_ntcCalStatus.sampleMs = args.sampleMs;
    s_ntcCalStatus.samples = 0;
    s_ntcCalStatus.shA = NAN;
    s_ntcCalStatus.shB = NAN;
    s_ntcCalStatus.shC = NAN;
    s_ntcCalStatus.wireIndex = args.wireIndex;
    ntcCalUnlock();
}

static void ntcCalUpdateProgress(float heatsinkC, float ntcOhm, uint32_t samples, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(25))) return;
    s_ntcCalStatus.heatsinkC = heatsinkC;
    s_ntcCalStatus.ntcOhm = ntcOhm;
    s_ntcCalStatus.samples = samples;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    ntcCalUnlock();
}

static void ntcCalSetError(const char* msg, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = false;
    s_ntcCalStatus.done = false;
    s_ntcCalStatus.error = true;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    if (msg && msg[0]) {
        strlcpy(s_ntcCalStatus.errorMsg, msg, sizeof(s_ntcCalStatus.errorMsg));
    } else {
        s_ntcCalStatus.errorMsg[0] = '\0';
    }
    ntcCalUnlock();
}

static void ntcCalFinish(float a, float b, float c, uint32_t samples, uint32_t elapsedMs) {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalStatus.running = false;
    s_ntcCalStatus.done = true;
    s_ntcCalStatus.error = false;
    s_ntcCalStatus.errorMsg[0] = '\0';
    s_ntcCalStatus.shA = a;
    s_ntcCalStatus.shB = b;
    s_ntcCalStatus.shC = c;
    s_ntcCalStatus.samples = samples;
    s_ntcCalStatus.elapsedMs = elapsedMs;
    ntcCalUnlock();
}

static NtcCalStatus ntcCalGetStatus() {
    NtcCalStatus out{};
    if (ntcCalLock(pdMS_TO_TICKS(25))) {
        out = s_ntcCalStatus;
        ntcCalUnlock();
    } else {
        out = s_ntcCalStatus;
    }
    return out;
}

static void ntcCalRequestAbort() {
    if (!ntcCalLock(pdMS_TO_TICKS(50))) return;
    s_ntcCalAbort = true;
    ntcCalUnlock();
}

static bool ntcCalAbortRequested() {
    bool abort = false;
    if (ntcCalLock(pdMS_TO_TICKS(25))) {
        abort = s_ntcCalAbort;
        ntcCalUnlock();
    } else {
        abort = s_ntcCalAbort;
    }
    return abort;
}

static void ntcCalTask(void* param) {
    NtcCalTaskArgs args{};
    if (param) {
        args = *static_cast<NtcCalTaskArgs*>(param);
        delete static_cast<NtcCalTaskArgs*>(param);
    }

    const uint32_t startMs = args.startMs ? args.startMs : millis();
    uint32_t lastUpdateMs = startMs;

    const bool useFixedRef =
        isfinite(args.targetC) && args.targetC > 0.0f;
    const float fixedRefC = useFixedRef ? args.targetC : NAN;

    double rSum = 0.0;
    uint32_t rCount = 0;
    double refSum = 0.0;
    uint32_t refCount = 0;
    uint32_t samples = 0;

    bool failed = false;
    const char* failReason = nullptr;

    while (true) {
        const uint32_t nowMs = millis();
        const uint32_t elapsedMs = (nowMs >= startMs) ? (nowMs - startMs) : 0;

        if (ntcCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }

        if (elapsedMs >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        if (!DEVICE || !NTC) {
            failed = true;
            failReason = ERR_SENSOR_MISSING;
            break;
        }
        float refC = NAN;
        if (useFixedRef) {
            refC = fixedRefC;
        } else if (DEVICE->tempSensor) {
            refC = DEVICE->tempSensor->getHeatsinkTemp();
        }

        NTC->update();
        NtcSensor::Sample s = NTC->getLastSample();

        bool sampleOk = false;
        if (isfinite(refC) &&
            isfinite(s.rNtcOhm) && s.rNtcOhm > 0.0f &&
            !s.pressed) {
            rSum += static_cast<double>(s.rNtcOhm);
            rCount++;
            if (!useFixedRef) {
                refSum += static_cast<double>(refC);
                refCount++;
            }
            samples++;
            sampleOk = true;
        }

        if (sampleOk || (nowMs - lastUpdateMs) >= args.sampleMs) {
            ntcCalUpdateProgress(refC, s.rNtcOhm, samples, elapsedMs);
            lastUpdateMs = nowMs;
        }

        if (samples >= kNtcCalMinSamples) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(args.sampleMs));
    }

    const uint32_t endMs = millis();
    const uint32_t elapsedMs = (endMs >= startMs) ? (endMs - startMs) : 0;

    if (!failed && samples < kNtcCalMinSamples) {
        failed = true;
        failReason = ERR_NOT_ENOUGH_SAMPLES;
    }

    if (failed) {
        ntcCalSetError(failReason ? failReason : ERR_FAILED, elapsedMs);
    } else {
        if (rCount < kNtcCalMinSamples) {
            ntcCalSetError(ERR_NOT_ENOUGH_SAMPLES, elapsedMs);
        } else {
            const float refC = useFixedRef
                                   ? fixedRefC
                                   : (refCount > 0 ? static_cast<float>(refSum / refCount) : NAN);
            if (!isfinite(refC) || refC <= 0.0f) {
                ntcCalSetError(ERR_INVALID_REF_TEMP, elapsedMs);
            } else {
                const float beta = NTC ? NTC->getBeta() : DEFAULT_NTC_BETA;
                const float t0K = DEFAULT_NTC_T0_C + 273.15f;
                const float tRefK = refC + 273.15f;
                float r0 = NAN;
                if (isfinite(beta) && beta > 0.0f && tRefK > 0.0f) {
                    const float rAvg = static_cast<float>(rSum / rCount);
                    r0 = rAvg / expf(beta * (1.0f / tRefK - 1.0f / t0K));
                }
                if (!isfinite(r0) || r0 <= 0.0f) {
                    ntcCalSetError(ERR_PERSIST_FAILED, elapsedMs);
                } else if (!NTC) {
                    ntcCalSetError(ERR_SENSOR_MISSING, elapsedMs);
                } else {
                    NTC->setR0(r0, true);
                    NTC->setModel(NtcSensor::Model::Beta, true);
                    if (CONF) {
                        CONF->PutBool(CALIB_NTC_DONE_KEY, true);
                    }
                    ntcCalFinish(NAN, NAN, NAN, samples, elapsedMs);
                }
            }
        }
    }

    if (CALREC) {
        CALREC->stop();
    }

    if (ntcCalLock(pdMS_TO_TICKS(50))) {
        s_ntcCalTask = nullptr;
        ntcCalUnlock();
    } else {
        s_ntcCalTask = nullptr;
    }

    vTaskDelete(nullptr);
}
} // namespace

bool ntcCalIsRunning_() {
    return s_ntcCalTask != nullptr;
}

void WiFiManager::registerNtcCalRoutes_() {
    // ---- NTC multi-point calibration ----
    server.on(EP_NTC_CALIBRATE, HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            float targetC = NAN;
            uint32_t sampleMs = 0;
            uint32_t timeoutMs = 0;
            uint16_t maxSamples = CalibrationRecorder::kDefaultMaxSamples;

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "target_c") == 0) {
                        double v = NAN;
                        if (!readCborDouble_(it, v)) return false;
                        targetC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "sample_ms") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        sampleMs = static_cast<uint32_t>(v);
                        return true;
                    }
                    if (strcmp(key, "timeout_ms") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        timeoutMs = static_cast<uint32_t>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (sampleMs == 0) {
                sampleMs = getNtcCalSampleMsFromConfig();
            }
            if (timeoutMs == 0) {
                timeoutMs = getNtcCalTimeoutMsFromConfig();
            }

            if (!isfinite(targetC) || targetC <= 0.0f) {
                targetC = NAN; // default to heatsink reference
            }
            if (sampleMs < 50) sampleMs = 50;
            if (sampleMs > 5000) sampleMs = 5000;
            if (timeoutMs < 1000) timeoutMs = 1000;
            if (timeoutMs > 3600000) timeoutMs = 3600000;

            if (maxSamples > CalibrationRecorder::kAbsoluteMaxSamples) {
                maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
            }
            const uint32_t totalMs = timeoutMs;
            if (sampleMs > 0 && totalMs > 0) {
                uint32_t required = (totalMs / sampleMs) + 4;
                if (required > maxSamples) {
                    if (required <= CalibrationRecorder::kAbsoluteMaxSamples) {
                        maxSamples = static_cast<uint16_t>(required);
                    } else {
                        const uint32_t minInterval =
                            (totalMs / (CalibrationRecorder::kAbsoluteMaxSamples - 1)) + 1;
                        if (sampleMs < minInterval) sampleMs = minInterval;
                        if (sampleMs > 5000) sampleMs = 5000;
                        maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                    }
                }
            }

            if (CONF) {
                if (isfinite(targetC)) {
                    CONF->PutFloat(NTC_CAL_TARGET_C_KEY, targetC);
                }
                CONF->PutInt(NTC_CAL_SAMPLE_MS_KEY, static_cast<int>(sampleMs));
                CONF->PutInt(NTC_CAL_TIMEOUT_MS_KEY, static_cast<int>(timeoutMs));
            }

            if (s_ntcCalTask != nullptr) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            if (!DEVICE) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }
            if (!NTC) {
                WiFiCbor::sendError(request, 503, ERR_NTC_MISSING);
                return;
            }
            const DeviceState st = DEVICE->getState();
            if (st == DeviceState::Running) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }

            if (!BUS_SAMPLER) {
                WiFiCbor::sendError(request, 503, ERR_BUS_SAMPLER_MISSING);
                return;
            }
            if (CALREC && CALREC->isRunning()) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            const uint8_t wireIndex = getNtcGateIndexFromConfig();
            if (!CALREC || !CALREC->start(CalibrationRecorder::Mode::Ntc,
                                        sampleMs,
                                        maxSamples,
                                        targetC,
                                        wireIndex)) {
                WiFiCbor::sendError(request, 500, ERR_START_FAILED);
                return;
            }

            NtcCalTaskArgs* args = new NtcCalTaskArgs();
            if (!args) {
                if (CALREC) {
                    CALREC->stop();
                }
                WiFiCbor::sendError(request, 500, ERR_ALLOC_FAILED);
                return;
            }
            args->targetC = targetC;
            args->wireIndex = wireIndex;
            args->sampleMs = sampleMs;
            args->timeoutMs = timeoutMs;
            args->startMs = CALREC->getMeta().startMs;

            ntcCalStartStatus(*args);

            BaseType_t okTask = xTaskCreate(
                ntcCalTask,
                "NtcCal",
                4096,
                args,
                2,
                &s_ntcCalTask
            );
            if (okTask != pdPASS) {
                delete args;
                s_ntcCalTask = nullptr;
                if (CALREC) {
                    CALREC->stop();
                }
                WiFiCbor::sendError(request, 500, ERR_TASK_FAILED);
                return;
            }

            sendStatusRunning_(request, true);
        }
    );

    // ---- NTC calibration status ----
    server.on(EP_NTC_CAL_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const NtcCalStatus st = ntcCalGetStatus();
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 512, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "running", st.running)) return false;
                    if (!WiFiCbor::encodeKvBool(map, "done", st.done)) return false;
                    if (st.error) {
                        if (!WiFiCbor::encodeKvText(map, "error",
                                                    st.errorMsg[0] ? st.errorMsg : ERR_FAILED)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvUInt(map, "start_ms", st.startMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "elapsed_ms", st.elapsedMs)) return false;
                    if (isfinite(st.targetC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "target_c", st.targetC)) return false;
                    }
                    if (isfinite(st.heatsinkC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "heatsink_c", st.heatsinkC)) return false;
                    }
                    if (isfinite(st.ntcOhm)) {
                        if (!WiFiCbor::encodeKvFloat(map, "ntc_ohm", st.ntcOhm)) return false;
                    }
                    if (!WiFiCbor::encodeKvUInt(map, "sample_ms", st.sampleMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "samples", st.samples)) return false;
                    if (isfinite(st.shA)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_a", st.shA)) return false;
                    }
                    if (isfinite(st.shB)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_b", st.shB)) return false;
                    }
                    if (isfinite(st.shC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "sh_c", st.shC)) return false;
                    }
                    if (st.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "wire_index", st.wireIndex)) return false;
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- NTC calibration stop ----
    server.on(EP_NTC_CAL_STOP, HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }
            (void)data; (void)len; (void)index; (void)total;

            ntcCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            sendStatusRunning_(request, false);
        }
    );

    // ---- NTC single-point beta calibration ----
    server.on(EP_NTC_BETA_CALIBRATE, HTTP_POST,
        [this](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request,
               uint8_t* data,
               size_t len,
               size_t index,
               size_t total)
        {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            float refC = NAN;
            float refTempC = NAN;
            float refAliasC = NAN;
            float tempC = NAN;
            float targetC = NAN;

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    double v = NAN;
                    if (strcmp(key, "ref_temp_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        refTempC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "ref_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        refC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "ref_alias_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        refAliasC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "temp_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        tempC = static_cast<float>(v);
                        return true;
                    }
                    if (strcmp(key, "target_c") == 0) {
                        if (!readCborDouble_(it, v)) return false;
                        targetC = static_cast<float>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (!parsed) {
                    body.clear();
                    WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                    return;
                }
            }
            body.clear();

            if (isfinite(refTempC)) {
                refC = refTempC;
            } else if (isfinite(refAliasC)) {
                refC = refAliasC;
            } else if (isfinite(tempC)) {
                refC = tempC;
            } else if (isfinite(targetC)) {
                refC = targetC;
            }

            if (!isfinite(refC) || refC <= 0.0f) {
                if (DEVICE && DEVICE->tempSensor) {
                    const float hsC = DEVICE->tempSensor->getHeatsinkTemp();
                    if (isfinite(hsC) && hsC > 0.0f) {
                        refC = hsC;
                    }
                }
            }
            if (!isfinite(refC) || refC <= 0.0f) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_REF_TEMP);
                return;
            }

            if (s_ntcCalTask != nullptr) {
                WiFiCbor::sendError(request, 409, ERR_CALIBRATION_BUSY);
                return;
            }

            if (!DEVTRAN || !DEVICE) {
                WiFiCbor::sendError(request, 503, ERR_DEVICE_MISSING);
                return;
            }
            if (!NTC) {
                WiFiCbor::sendError(request, 503, ERR_NTC_MISSING);
                return;
            }

            DeviceState lastState = DeviceState::Shutdown;
            if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                WiFiCbor::sendError(request, 409, ERR_DEVICE_NOT_IDLE);
                return;
            }

            if (!NTC->calibrateAtTempC(refC)) {
                WiFiCbor::sendError(request, 500, ERR_CALIBRATION_FAILED);
                return;
            }
            NTC->setModel(NtcSensor::Model::Beta, true);
            if (CONF) {
                CONF->PutBool(CALIB_NTC_DONE_KEY, true);
            }
            sendStatusApplied_(request);
        }
    );
}
