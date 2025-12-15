#include "system/Device.h"
#include "system/Utils.h"
#include "control/CpDischg.h"
#include <math.h>
#ifndef THERMAL_TASK_STACK_SIZE
#define THERMAL_TASK_STACK_SIZE 6144
#endif

#ifndef THERMAL_TASK_PRIORITY
#define THERMAL_TASK_PRIORITY 4
#endif

#ifndef THERMAL_TASK_CORE
#define THERMAL_TASK_CORE 1
#endif

// Integration period for thermalTask (ms).
// Should be faster than thermal time constants, slower than ADC sampling.
#ifndef THERMAL_TASK_PERIOD_MS
#define THERMAL_TASK_PERIOD_MS 25  // 40 Hz integration over 200 Hz samples
#endif


// ============================================================================
// 1) Initialize per-wire thermal model
// ============================================================================
//
// Called once before using the virtual temperatures. Uses:
//  - WireInfo from HeaterManager (R0, massKg).
//  - DS18B20 sensors (index 0 & 1) for ambient estimate.
// ============================================================================

void Device::initWireThermalModelOnce() {
    if (thermalInitDone || !tempSensor || !WIRE) {
        return;
    }

    // Ambient estimate: always use the average of physical sensors 0 and 1.
    // Wait briefly for fresh readings instead of falling back to a fixed 25 C.
    auto sampleAmbient = [&]() -> float {
        const uint32_t startMs = millis();
        for (;;) {
            float t0 = tempSensor->getTemperature(0);
            float t1 = tempSensor->getTemperature(1);

            int   count = 0;
            float sum   = 0.0f;
            if (isfinite(t0)) { sum += t0; ++count; }
            if (isfinite(t1)) { sum += t1; ++count; }
            if (count > 0) {
                return sum / static_cast<float>(count);
            }

            if ((millis() - startMs) > 1000) {
                return NAN; // give up after ~1s
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    };

    const float ambientSample = sampleAmbient();
    if (isfinite(ambientSample)) {
        ambientC = ambientSample;
    }

    const uint32_t now = millis();

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireInfo wi = WIRE->getWireInfo(i + 1);
        WireThermalState& ws = wireThermal[i];

        // Cold resistance
        ws.R0 = (wi.resistanceOhm > 0.01f) ? wi.resistanceOhm : 1.0f;

        // Thermal capacity C_th = m * cp
        const float m = (wi.massKg > 0.0f) ? wi.massKg : 0.0001f;
        ws.C_th = m * NICHROME_CP_J_PER_KG;
        if (!isfinite(ws.C_th) || ws.C_th <= 0.0f) {
            ws.C_th = 0.05f;  // safe tiny default
        }

        // First-order time constant tau (can be tuned per design)
        ws.tau = (DEFAULT_TAU_SEC > 0.05f) ? DEFAULT_TAU_SEC : 0.05f;

        ws.T                 = ambientC;
        ws.lastUpdateMs      = now;
        ws.locked            = false;
        ws.cooldownReleaseMs = 0;

        WIRE->setWireEstimatedTemp(i + 1, ws.T);
    }

    lastAmbientUpdateMs = now;

    DEBUG_PRINTF("[Thermal] Model initialized, ambient=%.1fÂ°C\n", ambientC);
    thermalInitDone = true;
}

// ============================================================================
// 2) R(T) for Nichrome wires
// ============================================================================
//
// Simple linear tempco model around ambient:
//      R(T) = R0 * (1 + alpha * (T - ambient))
// with clamping to avoid extreme values.
// ============================================================================

float Device::wireResistanceAtTemp(uint8_t idx, float T) const {
    if (idx >= HeaterManager::kWireCount) {
        return 1e6f; // out-of-range guard
    }

    const WireThermalState& ws = wireThermal[idx];

    const float dT    = T - ambientC;
    float scale = 1.0f + NICHROME_ALPHA * dT;

    // Clamp scale for safety
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 3.0f) scale = 3.0f;

    return ws.R0 * scale;
}

// ============================================================================
// 4) Utility: derive active mask from HeaterManager, ignoring locked wires
// ============================================================================

uint16_t Device::getActiveMaskFromHeater() const {
    uint16_t m = 0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (WIRE->getOutputState(i + 1) && !wireThermal[i].locked) {
            m |= (1u << i);
        }
    }
    return m;
}

// ============================================================================
// 5) Ambient tracking from DS18B20 sensors
// ============================================================================
//
// Keeps ambientC slowly tracking real measurements.
// Called from applyHeatingFromCapture() / waitForWiresNearAmbient().
// ============================================================================

void Device::updateAmbientFromSensors(bool force) {
    if (!tempSensor) return;

    const uint32_t now = millis();
    if (!force && (now - lastAmbientUpdateMs) < AMBIENT_UPDATE_INTERVAL_MS) {
        return; // not yet
    }
    lastAmbientUpdateMs = now;

    float t0 = tempSensor->getTemperature(0);
    float t1 = tempSensor->getTemperature(1);

    bool v0 = !isnan(t0);
    bool v1 = !isnan(t1);

    if (!v0 && !v1) {
        // No new data; keep current ambientC
        return;
    }

    float newAmb;
    if (v0 && v1) {
        newAmb = 0.5f * (t0 + t1);
    } else if (v0) {
        newAmb = t0;
    } else {
        newAmb = t1;
    }

    if (!thermalInitDone) {
        ambientC = newAmb;
    } else {
        // Clamp unrealistic jumps, then low-pass filter to avoid chatter.
        float delta = newAmb - ambientC;
        const float maxStep = AMBIENT_MAX_STEP_C;
        if (delta > maxStep) delta = maxStep;
        if (delta < -maxStep) delta = -maxStep;
        const float alpha = 0.15f;
        ambientC = ambientC + alpha * delta;
    }
}

// ============================================================================
// 6) Wait until all wires are near ambient (used before new loop starts)
// ============================================================================

void Device::waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs) {
    if (!thermalInitDone) {
        // Will be initialized on first use; nothing to wait for yet.
        return;
    }

    if (tolC < 0.5f) {
        tolC = 0.5f; // avoid unrealistic strictness
    }

    const uint32_t start = millis();
    DEBUG_PRINTF("[Thermal] Waiting for wires within %.1fÂ°C of ambient...\n", tolC);

    while (true) {
        updateAmbientFromSensors(false);

        bool allOk = true;
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            float d = fabsf(wireThermal[i].T - ambientC);
            if (d > tolC) {
                allOk = false;
                break;
            }
        }

        if (allOk) {
            DEBUG_PRINTF("[Thermal] All wires near ambient (%.1fÂ°C). Ready.\n", ambientC);
            break;
        }

        // Abort if power lost
        if (!is12VPresent()) {
            DEBUG_PRINTLN("[Thermal] 12V lost while waiting for cool-down.");
            handle12VDrop();
            break;
        }

        // Respect STOP during wait
        if (gEvt) {
            EventBits_t b = xEventGroupGetBits(gEvt);
            if (b & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Thermal] STOP during cool-down wait.");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                break;
            }
        }

        if (maxWaitMs > 0 && (millis() - start) >= maxWaitMs) {
            DEBUG_PRINTLN("[Thermal] Cool-down wait timeout; proceeding best-effort.");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void Device::startThermalTask() {
    if (thermalTaskHandle != nullptr) {
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        Device::thermalTaskWrapper,
        "ThermalTask",
        THERMAL_TASK_STACK_SIZE,
        this,
        THERMAL_TASK_PRIORITY,
        &thermalTaskHandle,
        THERMAL_TASK_CORE
    );

    if (ok != pdPASS) {
        DEBUG_PRINTLN("[Thermal] Failed to create ThermalTask ");
        thermalTaskHandle = nullptr;
    } else {
        DEBUG_PRINTLN("[Thermal] ThermalTask started ");
    }
}

void Device::thermalTaskWrapper(void* param) {
    Device* self = static_cast<Device*>(param);
    self->thermalTask();
}

void Device::thermalTask() {
    initWireThermalModelOnce();

    // Load idle current baseline if available.
    idleCurrentA = CONF->GetFloat(IDLE_CURR_KEY, 0.0f);
    if (idleCurrentA < 0.0f) idleCurrentA = 0.0f;

    const TickType_t period = pdMS_TO_TICKS(THERMAL_TASK_PERIOD_MS);

    while (true) {
        // Integrate using whatever new history is available.
        updateWireThermalFromHistory();

        // Run at a fixed, modest rate.
        vTaskDelay(period);
    }
}

void Device::updateWireThermalFromHistory() {
    if (!WIRE) {
        return;
    }
    if (manualMode) {
        return; // skip thermal integration in manual mode
    }
    if (!thermalInitDone) {
        initWireThermalModelOnce();
    }

    // Refresh ambient slowly.
    updateAmbientFromSensors(false);

    // Hard physical over-temp guard (real sensors, not just virtual model).
    if (tempSensor) {
        auto overPhysical = [&](float t) {
            return isfinite(t) && t >= PHYSICAL_HARD_MAX_C;
        };
        float t0 = tempSensor->getBoardTemp(0);
        float t1 = tempSensor->getBoardTemp(1);
        float th = tempSensor->getHeatsinkTemp();
        if (overPhysical(t0) || overPhysical(t1) || overPhysical(th)) {
            DEBUG_PRINTLN("[Thermal] Physical sensor over-temp detected -> forcing Error");
            WIRE->disableAll();
            setState(DeviceState::Error);
            if (BUZZ) BUZZ->bipFault();
            return;
        }
    }

    // Buffers for incremental reads (keep small to limit stack use).
    CurrentSensor::Sample       curBuf[32];
    BusSampler::Sample          busBuf[32];
    CpDischg::Sample            voltBuf[32];
    HeaterManager::OutputEvent  outBuf[32];

    uint32_t newCurSeq  = currentHistorySeq;
    uint32_t newVoltSeq = voltageHistorySeq;
    uint32_t newBusSeq  = busHistorySeq;
    uint32_t newOutSeq  = outputHistorySeq;

    size_t nCur = 0;
    size_t nVolt = 0;

    const bool useCapModel = isfinite(capBankCapF) && capBankCapF > 0.0f;

    // Prefer synchronized bus sampler (V+I) if available
    if (busSampler) {
        size_t nBus = busSampler->getHistorySince(
            busHistorySeq, busBuf, (size_t)32, newBusSeq);
        if (nBus > 0) {
            // Current samples (used by current-only model and watchdog)
            nCur = nBus;
            for (size_t i = 0; i < nBus; ++i) {
                curBuf[i].timestampMs = busBuf[i].timestampMs;
                curBuf[i].currentA    = busBuf[i].currentA;
            }

            // Voltage samples (used by capacitor model)
            nVolt = nBus;
            for (size_t i = 0; i < nBus; ++i) {
                voltBuf[i].timestampMs = busBuf[i].timestampMs;
                voltBuf[i].voltageV    = busBuf[i].voltageV;
            }
        }
    }

    if (useCapModel && nVolt == 0 && discharger) {
        // Fallback: use CpDischg history if BusSampler isn't available.
        nVolt = discharger->getHistorySince(
            voltageHistorySeq, voltBuf, (size_t)32, newVoltSeq);
    }

    if (!useCapModel && nCur == 0 && currentSensor) {
        nCur = currentSensor->getHistorySince(
            currentHistorySeq, curBuf, (size_t)32, newCurSeq);
    }

    const size_t nOut = WIRE->getOutputHistorySince(
        outputHistorySeq, outBuf, (size_t)32, newOutSeq);

    // Update last-sample watchdog when we have fresh current.
    if (nCur > 0) {
        lastCurrentSampleMs = curBuf[nCur - 1].timestampMs;
    }

    auto checkCurrentWatchdog = [&](uint16_t activeMask) -> bool {
        const uint32_t nowMs = millis();
        if (currentState != DeviceState::Running) return false;
        if (activeMask == 0) return false; // nothing heating
        if (lastCurrentSampleMs == 0) return false; // not yet primed
        #if   SAMPLINGSTALL
        if ((nowMs - lastCurrentSampleMs) > NO_CURRENT_SAMPLE_TIMEOUT_MS) {
            DEBUG_PRINTLN("[Thermal] Current sampling stalled -> forcing Error");
            WIRE->disableAll();
            setState(DeviceState::Error);
            if (BUZZ) BUZZ->bipFault();
            return true;
        }
        #endif
        return false;
    };

    // Delegate integration to WireThermalModel:
    //  - If capBankCapF is calibrated: use capacitor+recharge model (pulse-based).
    //  - Otherwise: fall back to current-only integration.
    if (useCapModel) {
        float vSrc = DEFAULT_DC_VOLTAGE;
        float rChg = DEFAULT_CHARGE_RESISTOR_OHMS;
        if (CONF) {
            vSrc = CONF->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);
            rChg = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
        }
        if (!isfinite(vSrc) || vSrc <= 0.0f) vSrc = DEFAULT_DC_VOLTAGE;
        if (!isfinite(rChg) || rChg <= 0.0f) rChg = DEFAULT_CHARGE_RESISTOR_OHMS;

        // If relay is open, model "no source".
        const float rChargeEff = (relayControl && relayControl->isOn()) ? rChg : INFINITY;

        wireThermalModel.integrateCapModel(voltBuf, nVolt,
                                           outBuf, nOut,
                                           capBankCapF, vSrc, rChargeEff,
                                           ambientC,
                                           wireStateModel,
                                           *WIRE);

        if (nVolt) {
            if (busSampler) busHistorySeq = newBusSeq;
            else            voltageHistorySeq = newVoltSeq;
        }
        outputHistorySeq = newOutSeq;
        lastHeaterMask   = wireStateModel.getLastMask();

        // Optional current-sampling watchdog (still useful when heating is active).
        if (checkCurrentWatchdog(lastHeaterMask)) {
            return;
        }
    } else {
        if (nCur > 0 || nOut > 0) {
            wireThermalModel.integrateCurrentOnly(curBuf, nCur,
                                                  outBuf, nOut,
                                                  ambientC,
                                                  wireStateModel,
                                                  *WIRE);

            if (nCur)  currentHistorySeq = newCurSeq;
            if (busSampler) busHistorySeq = newBusSeq;
            outputHistorySeq  = newOutSeq;
            lastHeaterMask    = wireStateModel.getLastMask();

            if (checkCurrentWatchdog(lastHeaterMask)) {
                return;
            }
        }
    }
}
