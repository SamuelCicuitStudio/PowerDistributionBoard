#include <WiFiRoutesShared.hpp>

namespace {
constexpr uint32_t kModelCalPollMs = 500;
constexpr uint32_t kModelCalTimeoutMs = 30 * 60 * 1000;
constexpr uint32_t kModelCalSteadyMsDefault = 60000;
constexpr uint32_t kFloorCalPollMs = 500;
constexpr uint32_t kFloorCalAmbientMsDefault = 5 * 60 * 1000;
constexpr uint32_t kFloorCalHeatMsDefault = 30 * 60 * 1000;
constexpr uint32_t kFloorCalTimeoutMsDefault = 60 * 60 * 1000;
constexpr uint32_t kFloorCalSteadyMsDefault = 120000;
constexpr uint32_t kFloorCalCoolMsDefault = 10 * 60 * 1000;
constexpr float kFloorCalStableSlopeCPerMin = 0.05f;

struct ModelCalTaskArgs {
    float    targetC   = NAN;
    uint8_t  wireIndex = 1;
    float    dutyFrac  = 1.0f;
    uint32_t timeoutMs = kModelCalTimeoutMs;
    uint32_t startMs   = 0;
    uint32_t calibStartMs = 0;
};

struct FloorCalTaskArgs {
    float    targetC     = NAN;
    uint8_t  wireIndex   = 1;
    uint32_t ambientMs   = kFloorCalAmbientMsDefault;
    uint32_t heatMs      = kFloorCalHeatMsDefault;
    uint32_t timeoutMs   = kFloorCalTimeoutMsDefault;
    uint32_t coolMs      = kFloorCalCoolMsDefault;
    float    dutyFrac    = 0.5f;
    uint32_t calibStartMs = 0;
};

static TaskHandle_t s_modelCalTask = nullptr;
static bool s_modelCalAbort = false;
static bool s_modelCalFinalize = false;
static float s_modelCalProgressPct = NAN;
static uint8_t s_modelCalProgressWire = 0;
static uint32_t s_modelCalResultMs = 0;
static uint32_t s_modelCalResultEpoch = 0;
static uint8_t s_modelCalResultWire = 0;
static double s_modelCalResultTau = NAN;
static double s_modelCalResultK = NAN;
static double s_modelCalResultC = NAN;
static TaskHandle_t s_floorCalTask = nullptr;
static bool s_floorCalAbort = false;
constexpr float kModelCalProgressHeatingMax = 90.0f;
constexpr float kModelCalCooldownTempC = 30.0f;

static void updateWireCalibStage(uint8_t wireIndex, int stage) {
    if (!CONF || wireIndex < 1 || wireIndex > HeaterManager::kWireCount) return;
    CONF->PutInt(kWireCalibStageKeys[wireIndex - 1], stage);
    if (RTC) {
        CONF->PutInt(kWireCalibTsKeys[wireIndex - 1],
                     static_cast<int>(RTC->getUnixTime()));
    }
}

static void updateWireCalibRunning(uint8_t wireIndex, bool running) {
    if (!CONF || wireIndex < 1 || wireIndex > HeaterManager::kWireCount) return;
    CONF->PutBool(kWireCalibRunKeys[wireIndex - 1], running);
    if (RTC) {
        CONF->PutInt(kWireCalibTsKeys[wireIndex - 1],
                     static_cast<int>(RTC->getUnixTime()));
    }
}

static bool modelCalAbortRequested() {
    return s_modelCalAbort;
}

static bool modelCalFinalizeRequested() {
    return s_modelCalFinalize;
}

static void setModelCalProgress(uint8_t wireIndex, float pct) {
    s_modelCalProgressWire = wireIndex;
    s_modelCalProgressPct = pct;
}

static bool getModelCalProgress(float& pctOut, uint8_t& wireIndexOut) {
    if (!isfinite(s_modelCalProgressPct)) return false;
    pctOut = s_modelCalProgressPct;
    wireIndexOut = s_modelCalProgressWire;
    return true;
}

static void modelCalRequestAbort() {
    s_modelCalAbort = true;
    s_modelCalProgressPct = NAN;
    s_modelCalProgressWire = 0;
    s_modelCalResultMs = 0;
    s_modelCalResultEpoch = 0;
    s_modelCalResultWire = 0;
    s_modelCalResultTau = NAN;
    s_modelCalResultK = NAN;
    s_modelCalResultC = NAN;
}

static void modelCalRequestFinalize() {
    s_modelCalFinalize = true;
}

static bool computeWireModelFromSamples(uint32_t heatStartMs,
                                        uint32_t heatStopMs,
                                        uint8_t wireIndex,
                                        float dutyFrac,
                                        float ambientRefC,
                                        double& outTau,
                                        double& outK,
                                        double& outC,
                                        float& outAmb,
                                        float& outInf,
                                        float& outPowerW,
                                        const char*& outErr);

static bool floorCalAbortRequested() {
    return s_floorCalAbort;
}

static void floorCalRequestAbort() {
    s_floorCalAbort = true;
}

static bool computeFloorModelFromSamples(uint32_t ambientEndMs,
                                         uint32_t heatStartMs,
                                         uint32_t heatStopMs,
                                         uint8_t wireIndex,
                                         float dutyFrac,
                                         double& outTau,
                                         double& outK,
                                         double& outC,
                                         float& outRoomAmb,
                                         float& outFloorInf,
                                         float& outPowerW,
                                         const char*& outErr);

static void modelCalTask(void* param);
static void floorCalTask(void* param);

static void modelCalTask(void* param) {
    ModelCalTaskArgs args{};
    if (param) {
        args = *static_cast<ModelCalTaskArgs*>(param);
        delete static_cast<ModelCalTaskArgs*>(param);
    }

    const uint32_t startMs = args.startMs ? args.startMs : millis();
    const uint32_t calibStartMs = args.calibStartMs ? args.calibStartMs : startMs;
    bool failed = false;
    const char* failReason = nullptr;
    bool heating = true;
    bool heatStartLogged = false;
    bool heatStopLogged = false;
    bool coolDoneLogged = false;
    bool finalizeLogged = false;
    float baseTempC = NAN;
    float ambientRefC = NAN;
    uint32_t heatStartAbs = 0;
    uint32_t heatStopAbs = 0;
    bool heatStartLocked = false;

    DEBUG_PRINTF("[WiFi] Model calibration start: wire=%u target=%.2f duty=%.2f\n",
                 static_cast<unsigned>(args.wireIndex),
                 static_cast<double>(args.targetC),
                 static_cast<double>(args.dutyFrac));

    if (DEVICE && DEVICE->tempSensor) {
        const float tB0 = DEVICE->tempSensor->getBoardTemp(0);
        const float tB1 = DEVICE->tempSensor->getBoardTemp(1);
        const float tHs = DEVICE->tempSensor->getHeatsinkTemp();
        float sum = 0.0f;
        uint8_t count = 0;
        if (isfinite(tB0)) { sum += tB0; count++; }
        if (isfinite(tB1)) { sum += tB1; count++; }
        if (isfinite(tHs)) { sum += tHs; count++; }
        if (count > 0) {
            ambientRefC = sum / static_cast<float>(count);
            baseTempC = ambientRefC;
        }
    }

    updateWireCalibRunning(args.wireIndex, true);
    updateWireCalibStage(args.wireIndex, 1);
    setModelCalProgress(args.wireIndex, 0.0f);
    s_modelCalResultMs = 0;
    s_modelCalResultEpoch = 0;
    s_modelCalResultWire = 0;
    s_modelCalResultTau = NAN;
    s_modelCalResultK = NAN;
    s_modelCalResultC = NAN;

    while (true) {
        const uint32_t nowMs = millis();
        const uint32_t elapsedMs = (nowMs >= startMs) ? (nowMs - startMs) : 0;

        if (modelCalFinalizeRequested()) {
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            if (heatStopAbs == 0) {
                heatStopAbs = nowMs;
                updateWireCalibStage(args.wireIndex, 3);
            }
            if (!finalizeLogged) {
                DEBUG_PRINTF("[WiFi] Model calibration finalize requested: wire=%u\n",
                             static_cast<unsigned>(args.wireIndex));
                finalizeLogged = true;
            }
            setModelCalProgress(args.wireIndex, kModelCalProgressHeatingMax);
            heating = false;
            break;
        }

        if (modelCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }

        if (elapsedMs >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        if (!DEVICE || !DEVTRAN || !NTC) {
            failed = true;
            failReason = ERR_DEVICE_MISSING;
            break;
        }

        Device::WireTargetStatus st{};
        const bool statusOk = DEVTRAN->getWireTargetStatus(st);
        const bool statusActive =
            statusOk && st.active && st.purpose == Device::EnergyRunPurpose::ModelCal;

        if (statusActive && !heatStartLocked) {
            heatStartAbs = (st.updatedMs > 0) ? st.updatedMs : nowMs;
            heatStartLocked = true;
            updateWireCalibStage(args.wireIndex, 2);
            if (!heatStartLogged) {
                DEBUG_PRINTF("[WiFi] Model calibration heating started: wire=%u\n",
                             static_cast<unsigned>(args.wireIndex));
                heatStartLogged = true;
            }
        }

        NTC->update();
        const float ntcTemp = NTC->getLastTempC();
        const float modelTemp = statusOk ? st.activeTempC : NAN;
        const float tempNow = isfinite(ntcTemp) ? ntcTemp : modelTemp;

        if (!isfinite(baseTempC) && isfinite(tempNow)) {
            baseTempC = tempNow;
        }

        if (heating && isfinite(tempNow) && isfinite(args.targetC) &&
            tempNow >= args.targetC) {
            heating = false;
            if (statusActive) {
                DEVTRAN->stopWireTargetTest();
            }
            if (heatStopAbs == 0) {
                heatStopAbs = millis();
            }
            updateWireCalibStage(args.wireIndex, 3);
            if (!heatStopLogged) {
                DEBUG_PRINTF("[WiFi] Model calibration target reached: wire=%u temp=%.2f target=%.2f\n",
                             static_cast<unsigned>(args.wireIndex),
                             static_cast<double>(tempNow),
                             static_cast<double>(args.targetC));
                heatStopLogged = true;
            }
            setModelCalProgress(args.wireIndex, kModelCalProgressHeatingMax);
        }

        if (!statusActive) {
            if (heating) {
                if (!heatStartLocked) {
                    failed = true;
                    failReason = ERR_ENERGY_STOPPED;
                    break;
                }
                heating = false;
                if (heatStopAbs == 0) {
                    heatStopAbs = nowMs;
                    updateWireCalibStage(args.wireIndex, 3);
                }
                if (!heatStopLogged) {
                    DEBUG_PRINTF("[WiFi] Model calibration heating stopped: wire=%u (energy stopped)\n",
                                 static_cast<unsigned>(args.wireIndex));
                    heatStopLogged = true;
                }
            }
            if (isfinite(tempNow) && tempNow <= kModelCalCooldownTempC) {
                if (!coolDoneLogged) {
                    DEBUG_PRINTF("[WiFi] Model calibration cooldown reached: wire=%u temp=%.2f\n",
                                 static_cast<unsigned>(args.wireIndex),
                                 static_cast<double>(tempNow));
                    coolDoneLogged = true;
                }
                break;
            }
            if (!isfinite(tempNow)) {
                failed = true;
                failReason = ERR_SENSOR_MISSING;
                break;
            }
        } else if (!heating) {
            if (isfinite(tempNow) && tempNow <= kModelCalCooldownTempC) {
                if (!coolDoneLogged) {
                    DEBUG_PRINTF("[WiFi] Model calibration cooldown reached: wire=%u temp=%.2f\n",
                                 static_cast<unsigned>(args.wireIndex),
                                 static_cast<double>(tempNow));
                    coolDoneLogged = true;
                }
                break;
            }
        }

        if (heating) {
            if (isfinite(tempNow) && isfinite(baseTempC) && isfinite(args.targetC)) {
                const float denom = args.targetC - baseTempC;
                if (denom > 0.25f) {
                    float ratio = (tempNow - baseTempC) / denom;
                    if (ratio < 0.0f) ratio = 0.0f;
                    if (ratio > 1.0f) ratio = 1.0f;
                    setModelCalProgress(args.wireIndex, ratio * kModelCalProgressHeatingMax);
                }
            }
        } else {
            setModelCalProgress(args.wireIndex, kModelCalProgressHeatingMax);
        }

        vTaskDelay(pdMS_TO_TICKS(kModelCalPollMs));
    }

    if (DEVTRAN) {
        DEVTRAN->stopWireTargetTest();
    }

    if (CALREC) {
        if (failed) {
            CALREC->stop();
        } else {
            CALREC->stopAndSave(5000);
        }
    }

    if (heatStartAbs == 0) {
        heatStartAbs = (calibStartMs > 0) ? calibStartMs : startMs;
    }
    if (heatStopAbs == 0) heatStopAbs = millis();
    if (heatStartAbs == 0) heatStartAbs = heatStopAbs;

    const uint32_t heatStartMs =
        (heatStartAbs >= calibStartMs) ? (heatStartAbs - calibStartMs) : 0;
    const uint32_t heatStopMs =
        (heatStopAbs >= calibStartMs) ? (heatStopAbs - calibStartMs) : heatStartMs;

    if (!failed) {
        double tau = NAN;
        double kLoss = NAN;
        double capC = NAN;
        float ambC = NAN;
        float infC = NAN;
        float powerW = NAN;
        const char* calcErr = nullptr;

        if (!computeWireModelFromSamples(heatStartMs,
                                         heatStopMs,
                                         args.wireIndex,
                                         args.dutyFrac,
                                         ambientRefC,
                                         tau,
                                         kLoss,
                                         capC,
                                         ambC,
                                         infC,
                                         powerW,
                                         calcErr)) {
            failed = true;
            failReason = calcErr ? calcErr : ERR_FAILED;
            DEBUG_PRINTF("[WiFi] Model calibration compute failed: %s\n",
                         failReason ? failReason : ERR_FAILED);
            s_modelCalResultTau = NAN;
            s_modelCalResultK = NAN;
            s_modelCalResultC = NAN;
        } else if (CONF) {
            DEBUG_PRINTF("[WiFi] Model calibration result: wire=%u tau=%.4f k=%.4f c=%.4f amb=%.2f inf=%.2f p=%.2f\n",
                         static_cast<unsigned>(args.wireIndex),
                         static_cast<double>(tau),
                         static_cast<double>(kLoss),
                         static_cast<double>(capC),
                         static_cast<double>(ambC),
                         static_cast<double>(infC),
                         static_cast<double>(powerW));
            s_modelCalResultMs = millis();
            s_modelCalResultEpoch = RTC ? static_cast<uint32_t>(RTC->getUnixTime()) : 0;
            s_modelCalResultWire = args.wireIndex;
            s_modelCalResultTau = tau;
            s_modelCalResultK = kLoss;
            s_modelCalResultC = capC;
            if (args.wireIndex >= 1 && args.wireIndex <= HeaterManager::kWireCount) {
                const uint8_t idx = static_cast<uint8_t>(args.wireIndex - 1);
                CONF->PutDouble(kWireModelTauKeys[idx], tau);
                CONF->PutDouble(kWireModelKKeys[idx], kLoss);
                CONF->PutDouble(kWireModelCKeys[idx], capC);
                CONF->PutBool(kWireCalibDoneKeys[idx], true);
                updateWireCalibStage(args.wireIndex, 4);
            }
            if (DEVICE) {
                DEVICE->getWireThermalModel()
                    .setWireThermalParams(args.wireIndex, tau, kLoss, capC);
            }
        }
    }

    if (failed) {
        DEBUG_PRINTF("[WiFi] Model calibration failed: %s\n",
                     failReason ? failReason : ERR_FAILED);
        s_modelCalProgressPct = NAN;
        s_modelCalProgressWire = 0;
    } else {
        setModelCalProgress(args.wireIndex, 100.0f);
    }

    updateWireCalibRunning(args.wireIndex, false);
    s_modelCalFinalize = false;

    s_modelCalTask = nullptr;
    vTaskDelete(nullptr);
}

static bool computeWireModelFromSamples(uint32_t heatStartMs,
                                        uint32_t heatStopMs,
                                        uint8_t wireIndex,
                                        float dutyFrac,
                                        float ambientRefC,
                                        double& outTau,
                                        double& outK,
                                        double& outC,
                                        float& outAmb,
                                        float& outInf,
                                        float& outPowerW,
                                        const char*& outErr) {
    outErr = ERR_FAILED;
    if (!CALREC) {
        outErr = ERR_START_FAILED;
        return false;
    }
    if (!WIRE) {
        outErr = ERR_WIRE_SUBSYSTEM_MISSING;
        return false;
    }
    const uint16_t total = CALREC->getSampleCount();
    if (total < 4) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (heatStopMs <= heatStartMs) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    float r = WIRE->getWireResistance(wireIndex);
    if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;

    float duty = dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 1.0f;
    if (duty > 1.0f) duty = 1.0f;

    const uint32_t heatWindowMs = heatStopMs - heatStartMs;
    uint32_t steadyWindowMs = kModelCalSteadyMsDefault;
    if (steadyWindowMs > heatWindowMs) steadyWindowMs = heatWindowMs;
    const uint32_t steadyStartMs =
        (heatStopMs > steadyWindowMs) ? (heatStopMs - steadyWindowMs) : heatStartMs;

    uint32_t ambientWindowMs = kModelCalSteadyMsDefault;
    if (ambientWindowMs > heatStartMs) ambientWindowMs = heatStartMs;
    const uint32_t ambientStartMs =
        (heatStartMs > ambientWindowMs) ? (heatStartMs - ambientWindowMs) : 0;

    double ambSum = 0.0;
    uint32_t ambCount = 0;
    const bool useAmbientOverride = isfinite(ambientRefC);
    double infSum = 0.0;
    uint32_t infCount = 0;
    double powerSum = 0.0;
    uint32_t powerCount = 0;

    CalibrationRecorder::Sample buf[32];
    uint16_t copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs >= ambientStartMs && s.tMs <= heatStartMs &&
                isfinite(s.tempC)) {
                ambSum += s.tempC;
                ambCount++;
            }
            if (s.tMs >= heatStartMs && s.tMs <= heatStopMs &&
                isfinite(s.voltageV)) {
                const double v = static_cast<double>(s.voltageV);
                double p = NAN;
                if (isfinite(s.currentA)) {
                    double iCur = static_cast<double>(s.currentA);
                    if (iCur < 0.0) iCur = 0.0;
                    p = v * iCur;
                }
                if (!isfinite(p)) {
                    p = (v * v) / static_cast<double>(r);
                    p *= static_cast<double>(duty);
                }
                powerSum += p;
                powerCount++;
            }
            if (s.tMs >= steadyStartMs && s.tMs <= heatStopMs &&
                isfinite(s.tempC)) {
                infSum += s.tempC;
                infCount++;
            }
        }
        copied += got;
    }

    const double ambAvg = (ambCount > 0) ? (ambSum / static_cast<double>(ambCount)) : NAN;
    if (!useAmbientOverride && ambCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (infCount < 3 || powerCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }

    outAmb = useAmbientOverride ? ambientRefC : static_cast<float>(ambAvg);
    outInf = static_cast<float>(infSum / static_cast<double>(infCount));
    outPowerW = static_cast<float>(powerSum / static_cast<double>(powerCount));
    if (!isfinite(outAmb) || !isfinite(outInf) || !isfinite(outPowerW)) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }

    double deltaT = static_cast<double>(outInf - outAmb);
    if ((!isfinite(deltaT) || deltaT <= 0.05) && isfinite(ambAvg)) {
        outAmb = static_cast<float>(ambAvg);
        deltaT = static_cast<double>(outInf - outAmb);
    }
    if (!isfinite(deltaT) || deltaT <= 0.05) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }
    if (outPowerW <= 0.0f) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    const double t63Target = static_cast<double>(outAmb) + 0.632 * deltaT;
    double t63 = NAN;
    double sumT = 0.0;
    double sumY = 0.0;
    double sumTT = 0.0;
    double sumTY = 0.0;
    uint32_t fitCount = 0;

    copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs < heatStartMs || s.tMs > heatStopMs) continue;
            if (!isfinite(s.tempC)) continue;
            const double temp = static_cast<double>(s.tempC);
            const double tSec = static_cast<double>(s.tMs - heatStartMs) * 0.001;
            if (!isfinite(t63) && temp >= t63Target && tSec > 0.0) {
                t63 = tSec;
            }
            const double frac = (temp - static_cast<double>(outAmb)) / deltaT;
            if (frac > 0.02 && frac < 0.98) {
                const double y = log(1.0 - frac);
                if (isfinite(y)) {
                    sumT += tSec;
                    sumY += y;
                    sumTT += tSec * tSec;
                    sumTY += tSec * y;
                    fitCount++;
                }
            }
        }
        copied += got;
    }

    double tau = NAN;
    if (isfinite(t63) && t63 > 0.0) {
        tau = t63;
    } else if (fitCount >= 3) {
        const double denom = static_cast<double>(fitCount) * sumTT - sumT * sumT;
        if (fabs(denom) > 1e-6) {
            const double slope =
                (static_cast<double>(fitCount) * sumTY - sumT * sumY) / denom;
            if (isfinite(slope) && slope < 0.0) {
                tau = -1.0 / slope;
            }
        }
    }

    if (!isfinite(tau) || tau <= 0.0) {
        outErr = ERR_FIT_FAILED;
        return false;
    }

    const double kLoss = static_cast<double>(outPowerW) / deltaT;
    const double capC = kLoss * tau;
    if (!isfinite(kLoss) || kLoss <= 0.0 || !isfinite(capC) || capC <= 0.0) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    outTau = tau;
    outK = kLoss;
    outC = capC;
    outErr = nullptr;
    return true;
}

static bool computeFloorModelFromSamples(uint32_t ambientEndMs,
                                         uint32_t heatStartMs,
                                         uint32_t heatStopMs,
                                         uint8_t wireIndex,
                                         float dutyFrac,
                                         double& outTau,
                                         double& outK,
                                         double& outC,
                                         float& outRoomAmb,
                                         float& outFloorInf,
                                         float& outPowerW,
                                         const char*& outErr) {
    outErr = ERR_FAILED;
    if (!CALREC) {
        outErr = ERR_START_FAILED;
        return false;
    }
    if (!WIRE) {
        outErr = ERR_WIRE_SUBSYSTEM_MISSING;
        return false;
    }
    const uint16_t total = CALREC->getSampleCount();
    if (total < 4) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (ambientEndMs == 0) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }
    if (heatStopMs <= heatStartMs) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    float r = WIRE->getWireResistance(wireIndex);
    if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;

    float duty = dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 1.0f;
    if (duty > 1.0f) duty = 1.0f;

    const uint32_t heatWindowMs = heatStopMs - heatStartMs;
    uint32_t steadyWindowMs = kFloorCalSteadyMsDefault;
    if (steadyWindowMs > heatWindowMs) steadyWindowMs = heatWindowMs;
    const uint32_t steadyStartMs =
        (heatStopMs > steadyWindowMs) ? (heatStopMs - steadyWindowMs) : heatStartMs;
    uint32_t ambientWindowMs = kFloorCalSteadyMsDefault;
    if (ambientWindowMs > ambientEndMs) ambientWindowMs = ambientEndMs;
    const uint32_t ambientStartMs =
        (ambientEndMs > ambientWindowMs) ? (ambientEndMs - ambientWindowMs) : 0;

    double roomSum = 0.0;
    uint32_t roomCount = 0;
    double floorInfSum = 0.0;
    uint32_t floorInfCount = 0;
    double powerSum = 0.0;
    uint32_t powerCount = 0;

    CalibrationRecorder::Sample buf[32];
    uint16_t copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs >= ambientStartMs && s.tMs <= ambientEndMs &&
                isfinite(s.roomTempC)) {
                roomSum += s.roomTempC;
                roomCount++;
            }
            if (s.tMs >= heatStartMs && s.tMs <= heatStopMs &&
                isfinite(s.voltageV)) {
                const double v = static_cast<double>(s.voltageV);
                double p = NAN;
                if (isfinite(s.currentA)) {
                    double i = static_cast<double>(s.currentA);
                    if (i < 0.0) i = 0.0;
                    p = v * i;
                }
                if (!isfinite(p)) {
                    p = (v * v) / static_cast<double>(r);
                    p *= static_cast<double>(duty);
                }
                powerSum += p;
                powerCount++;
            }
            if (s.tMs >= steadyStartMs && s.tMs <= heatStopMs &&
                isfinite(s.tempC)) {
                floorInfSum += s.tempC;
                floorInfCount++;
            }
        }
        copied += got;
    }

    if (roomCount < 3) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }
    if (floorInfCount < 3 || powerCount < 3) {
        outErr = ERR_NOT_ENOUGH_SAMPLES;
        return false;
    }

    outRoomAmb = static_cast<float>(roomSum / static_cast<double>(roomCount));
    outFloorInf = static_cast<float>(floorInfSum / static_cast<double>(floorInfCount));
    outPowerW = static_cast<float>(powerSum / static_cast<double>(powerCount));
    if (!isfinite(outRoomAmb) || !isfinite(outFloorInf) || !isfinite(outPowerW)) {
        outErr = ERR_SENSOR_MISSING;
        return false;
    }

    const double deltaT = static_cast<double>(outFloorInf - outRoomAmb);
    if (!isfinite(deltaT) || deltaT <= 0.05) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }
    if (outPowerW <= 0.0f) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    const double t63Target = static_cast<double>(outRoomAmb) + 0.632 * deltaT;
    double t63 = NAN;
    double sumT = 0.0;
    double sumY = 0.0;
    double sumTT = 0.0;
    double sumTY = 0.0;
    uint32_t fitCount = 0;

    copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = CALREC->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            const CalibrationRecorder::Sample& s = buf[i];
            if (s.tMs < heatStartMs || s.tMs > heatStopMs) continue;
            if (!isfinite(s.tempC)) continue;
            const double temp = static_cast<double>(s.tempC);
            const double tSec = static_cast<double>(s.tMs - heatStartMs) * 0.001;
            if (!isfinite(t63) && temp >= t63Target && tSec > 0.0) {
                t63 = tSec;
            }
            const double frac = (temp - static_cast<double>(outRoomAmb)) / deltaT;
            if (frac > 0.02 && frac < 0.98) {
                const double y = log(1.0 - frac);
                if (isfinite(y)) {
                    sumT += tSec;
                    sumY += y;
                    sumTT += tSec * tSec;
                    sumTY += tSec * y;
                    fitCount++;
                }
            }
        }
        copied += got;
    }

    double tau = NAN;
    if (isfinite(t63) && t63 > 0.0) {
        tau = t63;
    } else if (fitCount >= 3) {
        const double denom = static_cast<double>(fitCount) * sumTT - sumT * sumT;
        if (fabs(denom) > 1e-6) {
            const double slope =
                (static_cast<double>(fitCount) * sumTY - sumT * sumY) / denom;
            if (isfinite(slope) && slope < 0.0) {
                tau = -1.0 / slope;
            }
        }
    }

    if (!isfinite(tau) || tau <= 0.0) {
        outErr = ERR_FIT_FAILED;
        return false;
    }

    const double kLoss = static_cast<double>(outPowerW) / deltaT;
    const double capC = kLoss * tau;
    if (!isfinite(kLoss) || kLoss <= 0.0 || !isfinite(capC) || capC <= 0.0) {
        outErr = ERR_INVALID_TARGET;
        return false;
    }

    outTau = tau;
    outK = kLoss;
    outC = capC;
    outErr = nullptr;
    return true;
}

static void floorCalTask(void* param) {
    FloorCalTaskArgs args{};
    if (param) {
        args = *static_cast<FloorCalTaskArgs*>(param);
        delete static_cast<FloorCalTaskArgs*>(param);
    }

    const uint32_t calibStartMs = args.calibStartMs ? args.calibStartMs : millis();
    const uint32_t startMs = millis();
    bool failed = false;
    const char* failReason = nullptr;
    uint32_t heatStartAbs = 0;
    uint32_t heatStopAbs = 0;
    float duty = args.dutyFrac;
    if (!isfinite(duty) || duty <= 0.0f) duty = 0.5f;
    if (duty > 1.0f) duty = 1.0f;
    bool heatStartLocked = false;

    if (CONF) {
        CONF->PutBool(CALIB_FLOOR_RUNNING_KEY, true);
        CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 1);
        if (RTC) {
            CONF->PutInt(CALIB_FLOOR_TS_KEY,
                         static_cast<int>(RTC->getUnixTime()));
        }
    }

    uint32_t ambientEndAbs = 0;
    uint32_t windowStartMs = 0;
    float floorStartC = NAN;
    float roomStartC = NAN;
    uint32_t lastValidMs = 0;

    while (true) {
        if (floorCalAbortRequested()) {
            failed = true;
            failReason = ERR_STOPPED;
            break;
        }
        const uint32_t nowMs = millis();
        if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
            failed = true;
            failReason = ERR_TIMEOUT;
            break;
        }

        float floorC = NAN;
        if (NTC) {
            NTC->update();
            floorC = NTC->getLastTempC();
        }
        float roomC = NAN;
        if (DEVICE && DEVICE->tempSensor) {
            roomC = DEVICE->tempSensor->getHeatsinkTemp();
        }

        if (!isfinite(floorC) || !isfinite(roomC)) {
            if (lastValidMs == 0) lastValidMs = nowMs;
            if ((nowMs - startMs) >= args.ambientMs &&
                (nowMs - lastValidMs) > 30000) {
                failed = true;
                failReason = ERR_SENSOR_MISSING;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
            continue;
        }

        lastValidMs = nowMs;
        if (windowStartMs == 0) {
            windowStartMs = nowMs;
            floorStartC = floorC;
            roomStartC = roomC;
        }

        const uint32_t windowElapsed = nowMs - windowStartMs;
        if (windowElapsed >= kFloorCalSteadyMsDefault) {
            const double minutes = static_cast<double>(windowElapsed) / 60000.0;
            const double slopeFloor =
                (static_cast<double>(floorC) - static_cast<double>(floorStartC)) / minutes;
            const double slopeRoom =
                (static_cast<double>(roomC) - static_cast<double>(roomStartC)) / minutes;
            const bool stable =
                fabs(slopeFloor) <= kFloorCalStableSlopeCPerMin &&
                fabs(slopeRoom) <= kFloorCalStableSlopeCPerMin;
            if (stable && (nowMs - startMs) >= args.ambientMs) {
                ambientEndAbs = nowMs;
                break;
            }
            windowStartMs = nowMs;
            floorStartC = floorC;
            roomStartC = roomC;
        }

        vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
    }

    if (!failed && ambientEndAbs == 0) {
        failed = true;
        failReason = ERR_TIMEOUT;
    }

    if (!failed && DEVTRAN) {
        if (!DEVTRAN->startEnergyCalibration(args.targetC,
                                             args.wireIndex,
                                             Device::EnergyRunPurpose::FloorCal,
                                             duty)) {
            failed = true;
            failReason = ERR_START_FAILED;
        } else {
            heatStartAbs = millis();
            if (CONF) {
                CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 2);
                if (RTC) {
                    CONF->PutInt(CALIB_FLOOR_TS_KEY,
                                 static_cast<int>(RTC->getUnixTime()));
                }
            }
        }
    }

    if (!failed) {
        while (true) {
            if (floorCalAbortRequested()) {
                failed = true;
                failReason = ERR_STOPPED;
                break;
            }
            const uint32_t nowMs = millis();
            if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
                failed = true;
                failReason = ERR_TIMEOUT;
                break;
            }
            Device::WireTargetStatus st{};
            if (!DEVTRAN || !DEVTRAN->getWireTargetStatus(st)) {
                failed = true;
                failReason = ERR_STATUS_UNAVAILABLE;
                break;
            }
            float floorC = NAN;
            if (NTC) {
                NTC->update();
                floorC = NTC->getLastTempC();
            } else {
                failed = true;
                failReason = ERR_NTC_MISSING;
                break;
            }

            const bool active =
                st.active && st.purpose == Device::EnergyRunPurpose::FloorCal;
            if (!active) {
                bool acceptStop = false;
                if (isfinite(floorC) && isfinite(args.targetC) &&
                    floorC >= args.targetC) {
                    acceptStop = true;
                }
                if (!acceptStop && heatStartAbs > 0 &&
                    (nowMs - heatStartAbs) >= args.heatMs) {
                    acceptStop = true;
                }
                if (acceptStop) {
                    if (DEVTRAN) {
                        DEVTRAN->stopWireTargetTest();
                    }
                    heatStopAbs = millis();
                    break;
                }
                failed = true;
                failReason = ERR_ENERGY_STOPPED;
                break;
            }
            if (!heatStartLocked && st.packetMs > 0 && st.updatedMs > 0) {
                heatStartAbs = st.updatedMs;
                heatStartLocked = true;
            }

            bool stopHeat = false;
            if (isfinite(args.targetC) && isfinite(floorC) &&
                floorC >= args.targetC) {
                stopHeat = true;
            }
            if (!stopHeat && heatStartAbs > 0 &&
                (nowMs - heatStartAbs) >= args.heatMs) {
                stopHeat = true;
            }
            if (stopHeat) {
                if (DEVTRAN) {
                    DEVTRAN->stopWireTargetTest();
                }
                heatStopAbs = millis();
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
        }
    }

    if (DEVTRAN) {
        DEVTRAN->stopWireTargetTest();
    }
    if (!heatStopAbs) heatStopAbs = millis();
    if (heatStartAbs == 0) heatStartAbs = heatStopAbs;

    if (!failed && args.coolMs > 0) {
        if (CONF) {
            CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 3);
            if (RTC) {
                CONF->PutInt(CALIB_FLOOR_TS_KEY,
                             static_cast<int>(RTC->getUnixTime()));
            }
        }
        const uint32_t coolStartMs = millis();
        while (true) {
            if (floorCalAbortRequested()) {
                failed = true;
                failReason = ERR_STOPPED;
                break;
            }
            const uint32_t nowMs = millis();
            if (args.timeoutMs > 0 && (nowMs - startMs) >= args.timeoutMs) {
                failed = true;
                failReason = ERR_TIMEOUT;
                break;
            }
            if ((nowMs - coolStartMs) >= args.coolMs) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kFloorCalPollMs));
        }
    }

    if (CALREC) {
        if (failed) {
            CALREC->stop();
        } else {
            CALREC->stopAndSave(5000);
        }
    }

    const uint32_t heatStartMs =
        (heatStartAbs >= calibStartMs) ? (heatStartAbs - calibStartMs) : 0;
    const uint32_t heatStopMs =
        (heatStopAbs >= calibStartMs) ? (heatStopAbs - calibStartMs) : heatStartMs;
    const uint32_t ambientEndMs =
        (ambientEndAbs >= calibStartMs) ? (ambientEndAbs - calibStartMs) : 0;

    if (!failed) {
        double tau = NAN;
        double kLoss = NAN;
        double capC = NAN;
        float roomAmb = NAN;
        float floorInf = NAN;
        float powerW = NAN;
        const char* calcErr = nullptr;

        if (!computeFloorModelFromSamples(ambientEndMs,
                                          heatStartMs,
                                          heatStopMs,
                                          args.wireIndex,
                                          duty,
                                          tau,
                                          kLoss,
                                          capC,
                                          roomAmb,
                                          floorInf,
                                          powerW,
                                          calcErr)) {
            failed = true;
            failReason = calcErr ? calcErr : ERR_FAILED;
        } else if (CONF) {
            CONF->PutDouble(FLOOR_MODEL_TAU_KEY, tau);
            CONF->PutDouble(FLOOR_MODEL_K_KEY, kLoss);
            CONF->PutDouble(FLOOR_MODEL_C_KEY, capC);
            CONF->PutBool(CALIB_FLOOR_DONE_KEY, true);
            CONF->PutInt(CALIB_FLOOR_STAGE_KEY, 4);
            if (RTC) {
                CONF->PutInt(CALIB_FLOOR_TS_KEY,
                             static_cast<int>(RTC->getUnixTime()));
            }
        }
    }

    if (failed) {
        DEBUG_PRINTF("[WiFi] Floor calibration failed: %s\n",
                     failReason ? failReason : ERR_FAILED);
    }

    if (CONF) {
        CONF->PutBool(CALIB_FLOOR_RUNNING_KEY, false);
    }

    s_floorCalTask = nullptr;
    vTaskDelete(nullptr);
}

static bool normalizeHistoryPath(const String& rawName,
                                 String& fullName,
                                 String& baseName,
                                 uint32_t* epochOut) {
    String name = rawName;
    name.trim();
    if (name.isEmpty() || name.indexOf("..") >= 0) return false;

    const int slash = name.lastIndexOf('/');
    baseName = (slash >= 0) ? name.substring(slash + 1) : name;

    const size_t extLen = strlen(CALIB_HISTORY_EXT);

    if (baseName.length() <= extLen || !baseName.endsWith(CALIB_HISTORY_EXT)) return false;
    String epochStr = baseName.substring(0, baseName.length() - extLen);
    if (epochStr.isEmpty()) return false;
    for (size_t i = 0; i < epochStr.length(); ++i) {
        if (!isDigit(epochStr[i])) return false;
    }

    String dir = (slash >= 0) ? name.substring(0, slash) : "";
    if (!dir.isEmpty()) {
        String dirTrimmed = dir;
        dirTrimmed.trim();
        if (dirTrimmed != CALIB_HISTORY_DIR &&
            dirTrimmed != String(CALIB_HISTORY_DIR).substring(1)) {
            return false;
        }
    }

    if (epochOut) {
        *epochOut = static_cast<uint32_t>(epochStr.toInt());
    }

    if (name.startsWith("/")) {
        fullName = name;
    } else if (slash >= 0) {
        fullName = "/" + name;
    } else {
        fullName = String(CALIB_HISTORY_DIR) + "/" + baseName;
    }

    return true;
}
}

bool modelCalIsRunning_() {
    return s_modelCalTask != nullptr;
}

bool floorCalIsRunning_() {
    return s_floorCalTask != nullptr;
}

void WiFiManager::registerCalibrationRoutes_() {
    // ---- Calibration recorder status ----
    server.on(EP_CALIB_STATUS, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            const CalibrationRecorder::Meta meta = CALREC->getMeta();
            const char* modeStr =
                (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                (meta.mode == CalibrationRecorder::Mode::Floor) ? MODE_FLOOR :
                MODE_NONE;

            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, 384, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeKvBool(map, "running", meta.running)) return false;
                    if (!WiFiCbor::encodeKvText(map, "mode", modeStr)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "count", meta.count)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "capacity", meta.capacity)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "interval_ms", meta.intervalMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "start_ms", meta.startMs)) return false;
                    if (meta.startEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "start_epoch", meta.startEpoch)) return false;
                    }
                    if (!WiFiCbor::encodeKvBool(map, "saved", meta.saved)) return false;
                    if (!WiFiCbor::encodeKvUInt(map, "saved_ms", meta.savedMs)) return false;
                    if (meta.savedEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "saved_epoch", meta.savedEpoch)) return false;
                    }
                    if (isfinite(meta.targetTempC)) {
                        if (!WiFiCbor::encodeKvFloat(map, "target_c", meta.targetTempC)) return false;
                    }
                    if (meta.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "wire_index", meta.wireIndex)) return false;
                    }
                    float progressPct = NAN;
                    uint8_t progressWire = 0;
                    if (getModelCalProgress(progressPct, progressWire)) {
                        if (!WiFiCbor::encodeKvFloat(map, "progress_pct", progressPct)) return false;
                        if (progressWire > 0) {
                            if (!WiFiCbor::encodeKvUInt(map, "progress_wire", progressWire)) {
                                return false;
                            }
                        }
                    }
                    if (s_modelCalResultMs > 0 || s_modelCalResultEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(map, "result_ms",
                                                    static_cast<uint64_t>(s_modelCalResultMs))) {
                            return false;
                        }
                        if (s_modelCalResultEpoch > 0) {
                            if (!WiFiCbor::encodeKvUInt(map, "result_epoch",
                                                        static_cast<uint64_t>(s_modelCalResultEpoch))) {
                                return false;
                            }
                        }
                        if (s_modelCalResultWire > 0) {
                            if (!WiFiCbor::encodeKvUInt(map, "result_wire",
                                                        static_cast<uint64_t>(s_modelCalResultWire))) {
                                return false;
                            }
                        }
                        if (isfinite(s_modelCalResultTau)) {
                            if (!WiFiCbor::encodeKvFloat(map, "result_tau", s_modelCalResultTau)) {
                                return false;
                            }
                        }
                        if (isfinite(s_modelCalResultK)) {
                            if (!WiFiCbor::encodeKvFloat(map, "result_k", s_modelCalResultK)) {
                                return false;
                            }
                        }
                        if (isfinite(s_modelCalResultC)) {
                            if (!WiFiCbor::encodeKvFloat(map, "result_c", s_modelCalResultC)) {
                                return false;
                            }
                        }
                    }
                    return true;
                })) {
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Calibration recorder start ----
    server.on(EP_CALIB_START, HTTP_POST,
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

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            String modeStr;
            uint32_t intervalMs = CalibrationRecorder::kDefaultIntervalMs;
            uint16_t maxSamples = CalibrationRecorder::kDefaultMaxSamples;
            uint32_t floorAmbientMs = 0;
            uint32_t floorHeatMs = 0;
            uint32_t floorTimeoutMs = 0;
            uint32_t floorCoolMs = 0;
            float floorDuty = NAN;
            float modelDuty = NAN;
            float targetC = NAN;
            uint32_t epoch = 0;
            uint8_t wireIndex = getNtcGateIndexFromConfig();
            double dutyVal = NAN;
            double dutyPctVal = NAN;

            const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                if (strcmp(key, "mode") == 0) {
                    String tmp;
                    if (!readCborText_(it, tmp)) return false;
                    tmp.toLowerCase();
                    modeStr = tmp;
                    return true;
                }
                if (strcmp(key, "interval_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    intervalMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "max_samples") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    maxSamples = static_cast<uint16_t>(v);
                    return true;
                }
                if (strcmp(key, "ambient_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorAmbientMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "heat_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorHeatMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "timeout_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorTimeoutMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "cool_ms") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    floorCoolMs = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "duty") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    dutyVal = v;
                    return true;
                }
                if (strcmp(key, "duty_pct") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    dutyPctVal = v;
                    return true;
                }
                if (strcmp(key, "target_c") == 0) {
                    double v = NAN;
                    if (!readCborDouble_(it, v)) return false;
                    targetC = static_cast<float>(v);
                    return true;
                }
                if (strcmp(key, "epoch") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    epoch = static_cast<uint32_t>(v);
                    return true;
                }
                if (strcmp(key, "wire_index") == 0) {
                    uint64_t v = 0;
                    if (!readCborUInt64_(it, v)) return false;
                    wireIndex = static_cast<uint8_t>(v);
                    return true;
                }
                return skipCborValue_(it);
            });
            body.clear();
            if (!parsed) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_CBOR);
                return;
            }

            auto sendCalibError = [&](int status,
                                      const char* error,
                                      const String& detail,
                                      const char* state) {
                const char* detailStr = detail.length() ? detail.c_str() : nullptr;
                WiFiCbor::sendError(request, status, error, detailStr, state);
            };

            CalibrationRecorder::Mode mode = CalibrationRecorder::Mode::None;
            if (modeStr == MODE_NTC) mode = CalibrationRecorder::Mode::Ntc;
            else if (modeStr == MODE_MODEL) mode = CalibrationRecorder::Mode::Model;
            else if (modeStr == MODE_FLOOR) mode = CalibrationRecorder::Mode::Floor;

            if (mode == CalibrationRecorder::Mode::None) {
                sendCalibError(400, ERR_INVALID_MODE, "", nullptr);
                return;
            }
            if (!BUS_SAMPLER) {
                sendCalibError(503, ERR_BUS_SAMPLER_MISSING, "", nullptr);
                return;
            }
            if (CALREC && CALREC->isRunning()) {
                sendCalibError(409, ERR_ALREADY_RUNNING, "", nullptr);
                return;
            }
            if (ntcCalIsRunning_() ||
                s_modelCalTask != nullptr ||
                s_floorCalTask != nullptr) {
                sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                return;
            }

            if (epoch > 0 && RTC) {
                RTC->setUnixTime(epoch);
            }
            const uint8_t ntcGate = getNtcGateIndexFromConfig();
            if (mode == CalibrationRecorder::Mode::Ntc) {
                wireIndex = ntcGate;
            }
            if (CONF) {
                CONF->PutInt(SETUP_WIRE_INDEX_KEY, static_cast<int>(wireIndex));
            }

            if (mode == CalibrationRecorder::Mode::Model) {
                if (isfinite(dutyVal)) {
                    modelDuty = static_cast<float>(dutyVal);
                } else if (isfinite(dutyPctVal)) {
                    modelDuty = static_cast<float>(dutyPctVal * 0.01f);
                }
                if (!isfinite(modelDuty) || modelDuty <= 0.0f) {
                    modelDuty = 1.0f;
                }
                if (modelDuty > 1.0f) modelDuty = 1.0f;
                if (modelDuty < 0.05f) modelDuty = 0.05f;
            }

            if (mode == CalibrationRecorder::Mode::Floor) {
                if (floorAmbientMs == 0) floorAmbientMs = kFloorCalAmbientMsDefault;
                if (floorHeatMs == 0) floorHeatMs = kFloorCalHeatMsDefault;
                if (floorTimeoutMs == 0) floorTimeoutMs = kFloorCalTimeoutMsDefault;
                if (floorCoolMs == 0) floorCoolMs = kFloorCalCoolMsDefault;
                if (isfinite(dutyVal)) {
                    floorDuty = static_cast<float>(dutyVal);
                } else if (isfinite(dutyPctVal)) {
                    floorDuty = static_cast<float>(dutyPctVal * 0.01f);
                }

                if (floorAmbientMs < 10000) floorAmbientMs = 10000;
                if (floorHeatMs < 10000) floorHeatMs = 10000;
                if (floorCoolMs > (30u * 60u * 1000u)) {
                    floorCoolMs = 30u * 60u * 1000u;
                }
                const uint32_t totalFloorMs = floorAmbientMs + floorHeatMs + floorCoolMs;
                if (floorTimeoutMs < totalFloorMs) {
                    floorTimeoutMs = totalFloorMs + 60000;
                }
                if (floorTimeoutMs > (2u * 60u * 60u * 1000u)) {
                    floorTimeoutMs = 2u * 60u * 60u * 1000u;
                }
                if (!isfinite(floorDuty) || floorDuty <= 0.0f) {
                    floorDuty = 0.5f;
                }
                if (floorDuty > 1.0f) floorDuty = 1.0f;
                if (floorDuty < 0.05f) floorDuty = 0.05f;

                if (!isfinite(targetC) || targetC <= 0.0f) {
                    float floorMax = DEFAULT_FLOOR_MAX_C;
                    if (CONF) {
                        floorMax = CONF->GetFloat(FLOOR_MAX_C_KEY,
                                                  DEFAULT_FLOOR_MAX_C);
                    }
                    if (!isfinite(floorMax) || floorMax <= 0.0f) {
                        floorMax = DEFAULT_FLOOR_MAX_C;
                    }
                    targetC = floorMax;
                }

                if (intervalMs < 50) intervalMs = 50;
                if (intervalMs > 5000) intervalMs = 5000;
                if (maxSamples == 0) maxSamples = CalibrationRecorder::kDefaultMaxSamples;
                if (maxSamples > CalibrationRecorder::kAbsoluteMaxSamples) {
                    maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                }

                const uint32_t totalMs = floorAmbientMs + floorHeatMs + floorCoolMs;
                if (intervalMs > 0 && totalMs > 0) {
                    uint32_t required = (totalMs / intervalMs) + 4;
                    if (required > maxSamples) {
                        if (required <= CalibrationRecorder::kAbsoluteMaxSamples) {
                            maxSamples = static_cast<uint16_t>(required);
                        } else {
                            const uint32_t minInterval =
                                (totalMs / (CalibrationRecorder::kAbsoluteMaxSamples - 1)) + 1;
                            if (intervalMs < minInterval) intervalMs = minInterval;
                            if (intervalMs > 5000) intervalMs = 5000;
                            maxSamples = CalibrationRecorder::kAbsoluteMaxSamples;
                        }
                    }
                }
            }

            const bool ok = CALREC->start(mode, intervalMs, maxSamples, targetC, wireIndex);
            if (!ok) {
                sendCalibError(500, ERR_START_FAILED, "", nullptr);
                return;
            }

            if (mode == CalibrationRecorder::Mode::Model) {
                float runTargetC = targetC;
                if (!isfinite(runTargetC) || runTargetC <= 0.0f) {
                    float fallback = 150.0f;
                    if (CONF) {
                        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                                 DEFAULT_NICHROME_FINAL_TEMP_C);
                        if (isfinite(v) && v > 0.0f) fallback = v;
                    }
                    runTargetC = fallback;
                }

                if (!DEVTRAN) {
                    CALREC->stop();
                    sendCalibError(503, ERR_DEVICE_TRANSPORT_MISSING, "", nullptr);
                    return;
                }
                DeviceState lastState = DeviceState::Shutdown;
                if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                    CALREC->stop();
                    String detail = (lastState == DeviceState::Shutdown) ? "wake_timeout" : "";
                    sendCalibError(409, ERR_DEVICE_NOT_IDLE, detail, stateName(lastState));
                    return;
                }
                if (!WIRE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_WIRE_SUBSYSTEM_MISSING, "", nullptr);
                    return;
                }
                if (CONF && DEVICE) {
                    if (!DEVICE->getWireConfigStore().getAccessFlag(wireIndex)) {
                        CALREC->stop();
                        sendCalibError(403, ERR_WIRE_ACCESS_BLOCKED,
                                       String("wire=") + String(wireIndex), nullptr);
                        return;
                    }
                }
                if (!DEVTRAN->startEnergyCalibration(runTargetC,
                                                     wireIndex,
                                                     Device::EnergyRunPurpose::ModelCal,
                                                     modelDuty)) {
                    CALREC->stop();
                    sendCalibError(500, ERR_ENERGY_START_FAILED, "", nullptr);
                    return;
                }
                if (s_modelCalTask != nullptr) {
                    DEVTRAN->stopWireTargetTest();
                    CALREC->stop();
                    sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                    return;
                }
                s_modelCalAbort = false;
                ModelCalTaskArgs* args = new ModelCalTaskArgs();
                if (!args) {
                    DEVTRAN->stopWireTargetTest();
                    CALREC->stop();
                    sendCalibError(500, ERR_ALLOC_FAILED, "", nullptr);
                    return;
                }
                args->targetC = runTargetC;
                args->wireIndex = wireIndex;
                args->dutyFrac = modelDuty;
                args->timeoutMs = kModelCalTimeoutMs;
                args->startMs = millis();
                args->calibStartMs = CALREC->getMeta().startMs;

                BaseType_t okTask = xTaskCreate(
                    modelCalTask,
                    "ModelCal",
                    4096,
                    args,
                    2,
                    &s_modelCalTask
                );
                if (okTask != pdPASS) {
                    delete args;
                    s_modelCalTask = nullptr;
                    DEVTRAN->stopWireTargetTest();
                    CALREC->stop();
                    sendCalibError(500, ERR_TASK_FAILED, "", nullptr);
                    return;
                }
            }
            else if (mode == CalibrationRecorder::Mode::Floor) {
                if (!DEVTRAN || !DEVICE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_DEVICE_MISSING, "", nullptr);
                    return;
                }
                if (!NTC || !DEVICE->tempSensor) {
                    CALREC->stop();
                    sendCalibError(503, ERR_SENSOR_MISSING, "", nullptr);
                    return;
                }
                DeviceState lastState = DeviceState::Shutdown;
                if (!waitForIdle(DEVTRAN, kCalibWakeTimeoutMs, lastState)) {
                    CALREC->stop();
                    String detail = (lastState == DeviceState::Shutdown) ? "wake_timeout" : "";
                    sendCalibError(409, ERR_DEVICE_NOT_IDLE, detail, stateName(lastState));
                    return;
                }
                if (!WIRE) {
                    CALREC->stop();
                    sendCalibError(503, ERR_WIRE_SUBSYSTEM_MISSING, "", nullptr);
                    return;
                }
                if (CONF && DEVICE) {
                    if (!DEVICE->getWireConfigStore().getAccessFlag(wireIndex)) {
                        CALREC->stop();
                        sendCalibError(403, ERR_WIRE_ACCESS_BLOCKED,
                                       String("wire=") + String(wireIndex), nullptr);
                        return;
                    }
                }
                auto wi = WIRE->getWireInfo(wireIndex);
                if (!wi.connected) {
                    CALREC->stop();
                    sendCalibError(400, ERR_WIRE_NOT_CONNECTED,
                                   String("wire=") + String(wireIndex), nullptr);
                    return;
                }
                if (s_floorCalTask != nullptr) {
                    CALREC->stop();
                    sendCalibError(409, ERR_CALIBRATION_BUSY, "", nullptr);
                    return;
                }

                s_floorCalAbort = false;
                FloorCalTaskArgs* args = new FloorCalTaskArgs();
                if (!args) {
                    CALREC->stop();
                    sendCalibError(500, ERR_ALLOC_FAILED, "", nullptr);
                    return;
                }
                args->targetC = targetC;
                args->wireIndex = wireIndex;
                args->ambientMs = floorAmbientMs ? floorAmbientMs : kFloorCalAmbientMsDefault;
                args->heatMs = floorHeatMs ? floorHeatMs : kFloorCalHeatMsDefault;
                args->timeoutMs = floorTimeoutMs ? floorTimeoutMs : kFloorCalTimeoutMsDefault;
                args->coolMs = floorCoolMs ? floorCoolMs : kFloorCalCoolMsDefault;
                args->dutyFrac = floorDuty;
                args->calibStartMs = CALREC->getMeta().startMs;

                BaseType_t okTask = xTaskCreate(
                    floorCalTask,
                    "FloorCal",
                    4096,
                    args,
                    2,
                    &s_floorCalTask
                );
                if (okTask != pdPASS) {
                    delete args;
                    s_floorCalTask = nullptr;
                    CALREC->stop();
                    sendCalibError(500, ERR_TASK_FAILED, "", nullptr);
                    return;
                }
            }

            sendStatusRunning_(request, true);
        }
    );

    // ---- Calibration recorder stop ----
    server.on(EP_CALIB_STOP, HTTP_POST,
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

            static std::vector<uint8_t> body;
            if (index == 0) body.clear();
            body.insert(body.end(), data, data + len);
            if (index + len != total) return;

            if (!body.empty()) {
                uint32_t epoch = 0;
                const bool parsed = parseCborMap_(body, [&](const char* key, CborValue* it) {
                    if (strcmp(key, "epoch") == 0) {
                        uint64_t v = 0;
                        if (!readCborUInt64_(it, v)) return false;
                        epoch = static_cast<uint32_t>(v);
                        return true;
                    }
                    return skipCborValue_(it);
                });
                if (parsed && epoch > 0 && RTC) {
                    RTC->setUnixTime(epoch);
                }
            }
            body.clear();

            const bool saved = CALREC->stopAndSave();
            const CalibrationRecorder::Meta meta = CALREC ? CALREC->getMeta()
                                                          : CalibrationRecorder::Meta{};
            if (meta.mode == CalibrationRecorder::Mode::Model && modelCalIsRunning_()) {
                modelCalRequestFinalize();
            } else {
                modelCalRequestAbort();
            }
            floorCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }
            sendStatusRunningSaved_(request, saved);
        }
    );

    // ---- Calibration recorder clear ----
    server.on(EP_CALIB_CLEAR, HTTP_POST,
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
            CALREC->clear();
            modelCalRequestAbort();
            floorCalRequestAbort();
            if (DEVTRAN) {
                DEVTRAN->stopWireTargetTest();
            }

            bool removed = false;
            size_t removedCount = 0;
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(CALIB_MODEL_CBOR_FILE)) {
                    removed = SPIFFS.remove(CALIB_MODEL_CBOR_FILE);
                }
                auto removeFromDir = [&](File dir) {
                    File file = dir.openNextFile();
                    while (file) {
                        const bool isDir = file.isDirectory();
                        String rawName = file.name();
                        file.close();
                        if (!isDir) {
                            String fullName;
                            String baseName;
                            if (normalizeHistoryPath(rawName, fullName, baseName, nullptr)) {
                                if (SPIFFS.remove(fullName)) {
                                    ++removedCount;
                                }
                            }
                        }
                        file = dir.openNextFile();
                    }
                };

                File historyDir = SPIFFS.open(CALIB_HISTORY_DIR);
                if (historyDir && historyDir.isDirectory()) {
                    removeFromDir(historyDir);
                }

                File root = SPIFFS.open("/");
                if (root && root.isDirectory()) {
                    removeFromDir(root);
                }
            }

            sendStatusClearedFile_(request, removed, removedCount);
        }
    );

    // ---- Calibration recorder data (paged) ----
    server.on(EP_CALIB_DATA, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            uint16_t offset = 0;
            uint16_t count = 0;
            if (request->hasParam("offset")) {
                offset = request->getParam("offset")->value().toInt();
            }
            if (request->hasParam("count")) {
                count = request->getParam("count")->value().toInt();
            }
            if (count == 0) count = 200;
            if (count > 200) count = 200;

            const CalibrationRecorder::Meta meta = CALREC->getMeta();
            const uint16_t total = meta.count;

            const size_t capacity = 4096 + (size_t)count * 160;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "meta")) return false;
                    CborEncoder metaMap;
                    if (cbor_encoder_create_map(map, &metaMap, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    const char* modeStr =
                        (meta.mode == CalibrationRecorder::Mode::Ntc)   ? MODE_NTC :
                        (meta.mode == CalibrationRecorder::Mode::Model) ? MODE_MODEL :
                        (meta.mode == CalibrationRecorder::Mode::Floor) ? MODE_FLOOR :
                        MODE_NONE;
                    if (!WiFiCbor::encodeKvText(&metaMap, "mode", modeStr)) return false;
                    if (!WiFiCbor::encodeKvBool(&metaMap, "running", meta.running)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "count", total)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "capacity", meta.capacity)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "interval_ms", meta.intervalMs)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "start_ms", meta.startMs)) return false;
                    if (meta.startEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "start_epoch", meta.startEpoch)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvBool(&metaMap, "saved", meta.saved)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "saved_ms", meta.savedMs)) return false;
                    if (meta.savedEpoch > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "saved_epoch", meta.savedEpoch)) {
                            return false;
                        }
                    }
                    if (isfinite(meta.targetTempC)) {
                        if (!WiFiCbor::encodeKvFloat(&metaMap, "target_c", meta.targetTempC)) {
                            return false;
                        }
                    }
                    if (meta.wireIndex > 0) {
                        if (!WiFiCbor::encodeKvUInt(&metaMap, "wire_index", meta.wireIndex)) {
                            return false;
                        }
                    }
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "offset", offset)) return false;
                    if (!WiFiCbor::encodeKvUInt(&metaMap, "limit", count)) return false;
                    if (cbor_encoder_close_container(map, &metaMap) != CborNoError) {
                        return false;
                    }

                    if (!WiFiCbor::encodeText(map, "samples")) return false;
                    CborEncoder samples;
                    if (cbor_encoder_create_array(map, &samples, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }

                    CalibrationRecorder::Sample buf[32];
                    uint16_t copied = 0;
                    while (copied < count) {
                        const size_t chunk = (count - copied) < 32 ? (count - copied) : 32;
                        const size_t got = CALREC->copySamples(offset + copied, buf, chunk);
                        if (got == 0) break;
                        for (size_t i = 0; i < got; ++i) {
                            CborEncoder row;
                            if (cbor_encoder_create_map(&samples, &row, CborIndefiniteLength) != CborNoError) {
                                return false;
                            }
                            if (!WiFiCbor::encodeKvUInt(&row, "t_ms", buf[i].tMs)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "v", buf[i].voltageV)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "i", buf[i].currentA)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "temp_c", buf[i].tempC)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "room_c", buf[i].roomTempC)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "ntc_v", buf[i].ntcVolts)) return false;
                            if (!WiFiCbor::encodeKvFloat(&row, "ntc_ohm", buf[i].ntcOhm)) return false;
                            if (!WiFiCbor::encodeKvInt(&row, "ntc_adc", buf[i].ntcAdc)) return false;
                            if (!WiFiCbor::encodeKvBool(&row, "ntc_ok", buf[i].ntcValid)) return false;
                            if (!WiFiCbor::encodeKvBool(&row, "pressed", buf[i].pressed)) return false;
                            if (cbor_encoder_close_container(&samples, &row) != CborNoError) {
                                return false;
                            }
                        }
                        copied += got;
                    }

                    return cbor_encoder_close_container(map, &samples) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Calibration recorder file (CBOR) ----
    server.on(EP_CALIB_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!SPIFFS.begin(false) || !SPIFFS.exists(CALIB_MODEL_CBOR_FILE)) {
                WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
                return;
            }
            request->send(SPIFFS, CALIB_MODEL_CBOR_FILE, CT_APP_CBOR);
        }
    );

    // ---- Calibration history list (CBOR) ----
    server.on(EP_CALIB_HISTORY_LIST, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            std::vector<String> names;
            std::vector<uint32_t> epochs;

            if (SPIFFS.begin(false)) {
                auto addItem = [&](const String& rawName) {
                    String fullName;
                    String baseName;
                    uint32_t epoch = 0;
                    if (!normalizeHistoryPath(rawName, fullName, baseName, &epoch)) return;
                    for (const auto& existing : names) {
                        if (fullName == existing) return;
                    }
                    names.push_back(fullName);
                    epochs.push_back(epoch);
                };

                File historyDir = SPIFFS.open(CALIB_HISTORY_DIR);
                if (historyDir && historyDir.isDirectory()) {
                    File file = historyDir.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            addItem(file.name());
                        }
                        file.close();
                        file = historyDir.openNextFile();
                    }
                }

                File root = SPIFFS.open("/");
                if (root && root.isDirectory()) {
                    File file = root.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            addItem(file.name());
                        }
                        file.close();
                        file = root.openNextFile();
                    }
                }
            }

            const size_t capacity = 256 + names.size() * 128;
            std::vector<uint8_t> payload;
            if (!WiFiCbor::buildMapPayload(payload, capacity, [&](CborEncoder* map) {
                    if (!WiFiCbor::encodeText(map, "items")) return false;
                    CborEncoder items;
                    if (cbor_encoder_create_array(map, &items, CborIndefiniteLength) != CborNoError) {
                        return false;
                    }
                    for (size_t i = 0; i < names.size(); ++i) {
                        CborEncoder row;
                        if (cbor_encoder_create_map(&items, &row, CborIndefiniteLength) != CborNoError) {
                            return false;
                        }
                        if (!WiFiCbor::encodeKvText(&row, "name", names[i])) return false;
                        if (epochs[i] > 0) {
                            if (!WiFiCbor::encodeKvUInt(&row, "start_epoch", epochs[i])) return false;
                        }
                        if (cbor_encoder_close_container(&items, &row) != CborNoError) {
                            return false;
                        }
                    }
                    return cbor_encoder_close_container(map, &items) == CborNoError;
                })) {
                request->send(500, CT_TEXT_PLAIN, WiFiLang::getPlainError());
                return;
            }
            WiFiCbor::sendPayload(request, 200, payload);
        }
    );

    // ---- Calibration history file (CBOR) ----
    server.on(EP_CALIB_HISTORY_FILE, HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            if (!isAuthenticated(request)) return;
            if (lock()) { lastActivityMillis = millis(); unlock(); }

            if (!request->hasParam("name")) {
                WiFiCbor::sendError(request, 400, ERR_MISSING_NAME);
                return;
            }
            String name = request->getParam("name")->value();
            String fullName;
            String baseName;
            if (!normalizeHistoryPath(name, fullName, baseName, nullptr)) {
                WiFiCbor::sendError(request, 400, ERR_INVALID_NAME);
                return;
            }
            if (SPIFFS.begin(false)) {
                if (SPIFFS.exists(fullName)) {
                    request->send(SPIFFS, fullName, CT_APP_CBOR);
                    return;
                }
                String legacyPath = "/" + baseName;
                if (legacyPath != fullName && SPIFFS.exists(legacyPath)) {
                    request->send(SPIFFS, legacyPath, CT_APP_CBOR);
                    return;
                }
            }
            WiFiCbor::sendError(request, 404, ERR_NOT_FOUND);
        }
    );
}
