#include "Device.h"
#include "Utils.h"
#include <math.h>

// ---- 1) Initialize model once ------------------------------------------------

void Device::initWireThermalModelOnce() {
    if (thermalInitDone || !tempSensor || !WIRE) return;

    // Ambient from DS18B20 (sensor 0 & 1), fallback 25°C
    float t0 = tempSensor->getTemperature(0);
    float t1 = tempSensor->getTemperature(1);

    if (isnan(t0) && isnan(t1)) {
        ambientC = 25.0f;
    } else if (isnan(t0)) {
        ambientC = t1;
    } else if (isnan(t1)) {
        ambientC = t0;
    } else {
        ambientC = 0.5f * (t0 + t1);
    }

    const uint32_t now = millis();

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireInfo wi = WIRE->getWireInfo(i + 1);
        WireThermalState &ws = wireThermal[i];

        ws.R0 = (wi.resistanceOhm > 0.01f) ? wi.resistanceOhm : 1.0f;

        // Mass from HeaterManager (already computed), fallback small
        const float m = (wi.massKg > 0.0f) ? wi.massKg : 0.0001f;
        ws.C_th = m * NICHROME_CP_J_PER_KG;
        if (!isfinite(ws.C_th) || ws.C_th <= 0.0f) {
            ws.C_th = 0.05f; // safe tiny default
        }

        // One tunable tau (can be made per-wire later)
        ws.tau = (DEFAULT_TAU_SEC > 0.05f) ? DEFAULT_TAU_SEC : 0.05f;

        ws.T                 = ambientC;
        ws.lastUpdateMs      = now;
        ws.locked            = false;
        ws.cooldownReleaseMs = 0;

        WIRE->setWireEstimatedTemp(i + 1, ws.T);
    }

    DEBUG_PRINTF("[Thermal] Model initialized, ambient=%.1f°C\n", ambientC);
    thermalInitDone = true;
}

// ---- 2) R(T) for Nichrome ----------------------------------------------------

float Device::wireResistanceAtTemp(uint8_t idx, float T) const {
    if (idx >= HeaterManager::kWireCount) return 1e6f;
    const WireThermalState &ws = wireThermal[idx];

    // Linear tempco around ambient
    const float dT    = T - ambientC;
    float scale = 1.0f + NICHROME_ALPHA * dT;

    // Clamp to avoid insane values
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 3.0f) scale = 3.0f;

    return ws.R0 * scale;
}

// ---- 3) Helper: pure cooling of all wires -----------------------------------

void Device::updateWireCoolingAll() {
    if (!thermalInitDone) return;

    const uint32_t now = millis();

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireThermalState &ws = wireThermal[i];
        if (ws.lastUpdateMs == 0 || ws.tau <= 0.0f) {
            ws.lastUpdateMs = now;
            continue;
        }

        float dt = (now - ws.lastUpdateMs) / 1000.0f;
        if (dt <= 0.0f) {
            ws.lastUpdateMs = now;
            continue;
        }

        // RC cooling toward ambient
        float dTcool = -(ws.T - ambientC) * (dt / ws.tau);
        ws.T += dTcool;

        if (ws.T < ambientC) ws.T = ambientC;

        // Locked wires: check for re-enable
        if (ws.locked &&
            ws.T <= WIRE_T_REENABLE_C &&
            now >= ws.cooldownReleaseMs) {
            ws.locked = false;
            DEBUG_PRINTF("[Thermal] Wire %u re-enabled at %.1f°C\n", i + 1, ws.T);
        }

        ws.lastUpdateMs = now;
        WIRE->setWireEstimatedTemp(i + 1, ws.T);
    }
}

// ---- 4) Active mask from HeaterManager (ignores locked) ---------------------

uint16_t Device::getActiveMaskFromHeater() const {
    uint16_t m = 0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (WIRE->getOutputState(i + 1) && !wireThermal[i].locked) {
            m |= (1u << i);
        }
    }
    return m;
}

// ---- 5) Core thermal update: current-driven model ---------------------------
//
// Call this periodically (we hook it from LedUpdateTask).
// Uses:
//  - Total current from ACS781 (CurrentSensor)
//  - Which wires are ON
//  - Nichrome R(T) + thermal inertia
// Applies:
//  - Per-wire heating
//  - Cooling
//  - 150°C lock + 140°C hysteresis
//
void Device::applyHeatingFromCapture() {
    if (!currentSensor || !WIRE) return;
    if (!thermalInitDone) {
        initWireThermalModelOnce();
    } else {
        // Periodically refresh ambient from DS18B20 #0/#1
        updateAmbientFromSensors(false);
    }
    const uint32_t now = millis();

    // --- Read current and subtract idle baseline ---
    // Adjust this to your CurrentSensor API if needed.
    float I_meas = currentSensor->readCurrent();        // [A] total measured
    float I_idle = CONF->GetFloat(IDLE_CURR_KEY,DEFAULT_IDLE_CURR);   // baseline when no wires are on
    float I_net  = I_meas - I_idle;
    if (I_net < 0.0f) I_net = 0.0f;

    // --- Build active mask (ignore locked wires, and force locked OFF) ---
    uint16_t activeMask = 0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireThermalState &ws = wireThermal[i];

        // Safety: ensure locked wires are OFF physically.
        if (ws.locked && WIRE->getOutputState(i + 1)) {
            WIRE->setOutput(i + 1, false);
            if (indicator) indicator->setLED(i + 1, false);
        }

        if (WIRE->getOutputState(i + 1) && !ws.locked) {
            activeMask |= (1u << i);
        }
    }

    // If no active wires or no net current → only cooling
    if (activeMask == 0 || I_net <= 0.0005f) {
        updateWireCoolingAll();
        return;
    }

    // --- Pre-pass: cooling + compute R(T) + total conductance ---
    float R_T[HeaterManager::kWireCount] = {0};
    float G_tot = 0.0f;

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireThermalState &ws = wireThermal[i];

        if (ws.lastUpdateMs == 0) {
            ws.lastUpdateMs = now;
        }

        float dt = (now - ws.lastUpdateMs) / 1000.0f;
        if (dt < 0.0f) dt = 0.0f;

        // Cooling towards ambient (applies to ALL wires)
        if (ws.tau > 0.0f && dt > 0.0f) {
            float dTcool = -(ws.T - ambientC) * (dt / ws.tau);
            ws.T += dTcool;
        }
        if (ws.T < ambientC) ws.T = ambientC;

        // For active, non-locked wires: compute R(T) and contribution to G_tot
        if ((activeMask & (1u << i)) && !ws.locked) {
            float R = wireResistanceAtTemp(i, ws.T);
            if (R < 0.01f) R = 0.01f;
            R_T[i] = R;
            G_tot += 1.0f / R;
        }
    }

    if (G_tot <= 0.0f) {
        // Should not happen if activeMask != 0, but guard anyway
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            wireThermal[i].lastUpdateMs = now;
            WIRE->setWireEstimatedTemp(i + 1, wireThermal[i].T);
        }
        return;
    }

    // --- Solve supply voltage from total current ---
    const float V_est = I_net / G_tot;  // V across all active wires

    // --- Heating pass: distribute power per active wire ---
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireThermalState &ws = wireThermal[i];

        float dt = (now - ws.lastUpdateMs) / 1000.0f;
        if (dt <= 0.0f) dt = 0.0f;

        // Only active + not locked wires get heating
        if ((activeMask & (1u << i)) && !ws.locked) {
            float R = R_T[i];
            if (R < 0.01f) R = 0.01f;

            float I_i = V_est / R;           // A
            if (I_i < 0.0f) I_i = 0.0f;

            float P_i = I_i * I_i * R;       // W
            if (ws.C_th > 0.0f && dt > 0.0f) {
                float dTheat = (P_i * dt) / ws.C_th;
                ws.T += dTheat;
            }
        }

        // --- Safety: clamp, lock, unlock ---
        if (!ws.locked && ws.T >= WIRE_T_MAX_C) {
            ws.T = WIRE_T_MAX_C;
            ws.locked = true;
            ws.cooldownReleaseMs = now + LOCK_MIN_COOLDOWN_MS;

            // Force this wire OFF for safety
            WIRE->setOutput(i + 1, false);
            if (indicator) indicator->setLED(i + 1, false);

            DEBUG_PRINTF("[Thermal] Wire %u LOCKED at %.1f°C\n", i + 1, ws.T);
        }

        if (ws.locked &&
            ws.T <= WIRE_T_REENABLE_C &&
            now >= ws.cooldownReleaseMs) {
            ws.locked = false;
            DEBUG_PRINTF("[Thermal] Wire %u re-enabled at %.1f°C\n", i + 1, ws.T);
        }

        ws.lastUpdateMs = now;
        WIRE->setWireEstimatedTemp(i + 1, ws.T);
    }

    // Total power consistency (optional debug)
    // float P_tot = V_est * I_net;
    // DEBUG_PRINTF("[Thermal] I_net=%.3fA V_est=%.2fV P_tot=%.2fW\n", I_net, V_est, P_tot);
}
void Device::updateAmbientFromSensors(bool force) {
    if (!tempSensor) return;

    const uint32_t now = millis();
    const uint32_t AMBIENT_UPDATE_INTERVAL_MS = 2000; // every 2s is enough

    if (!force && (now - lastAmbientUpdateMs) < AMBIENT_UPDATE_INTERVAL_MS) {
        return;
    }
    lastAmbientUpdateMs = now;

    float t0 = tempSensor->getTemperature(0);
    float t1 = tempSensor->getTemperature(1);

    bool v0 = !isnan(t0);
    bool v1 = !isnan(t1);

    if (!v0 && !v1) {
        // No fresh readings; keep existing ambientC
        return;
    }

    float newAmb;
    if (v0 && v1) {
        newAmb = 0.5f * (t0 + t1);
    } else if (v0) {
        newAmb = t0;
    } else { // v1
        newAmb = t1;
    }

    if (!thermalInitDone) {
        // During first init we already set ambientC, but keep logic consistent
        ambientC = newAmb;
    } else {
        // Smooth changes to avoid jumps (simple low-pass filter)
        const float alpha = 0.15f; // 0..1; higher = faster tracking
        ambientC = ambientC + alpha * (newAmb - ambientC);
    }

    // (No need to renormalize wire temps here; they converge via cooling.)
}
void Device::waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs) {
    if (!thermalInitDone) {
        // Nothing meaningful yet; init will align them to ambient on first run.
        return;
    }

    if (tolC < 0.5f) {
        tolC = 0.5f; // avoid over-strict / noise sensitivity
    }

    const uint32_t start = millis();
    DEBUG_PRINTF("[Thermal] Waiting for wires to cool within %.1f°C of ambient...\n", tolC);

    while (true) {
        // Keep ambient aligned with sensors while we wait
        updateAmbientFromSensors(false);

        bool allOk = true;
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            float T = wireThermal[i].T;
            float d = fabsf(T - ambientC);
            if (d > tolC) {
                allOk = false;
                break;
            }
        }

        if (allOk) {
            DEBUG_PRINTF("[Thermal] All wires near ambient (%.1f°C). Ready to restart heating.\n", ambientC);
            break;
        }

        // Safety: if input power drops during wait, bail out
        if (!is12VPresent()) {
            DEBUG_PRINTLN("[Thermal] 12V lost during cool-down wait.");
            handle12VDrop();
            break;
        }

        // If STOP requested again while we are waiting, respect it
        if (gEvt) {
            EventBits_t b = xEventGroupGetBits(gEvt);
            if (b & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Thermal] STOP requested during cool-down wait.");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                break;
            }
        }

        // Optional safety timeout: proceed best-effort if taking too long
        if (maxWaitMs > 0 && (millis() - start) >= maxWaitMs) {
            DEBUG_PRINTLN("[Thermal] Cool-down wait timeout, proceeding with best-effort temperatures.");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
