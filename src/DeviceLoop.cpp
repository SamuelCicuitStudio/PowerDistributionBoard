#include "Device.h"
#include "Utils.h"
#include "RGBLed.h"
#include "Buzzer.h"
#include "SleepTimer.h"
#include <vector>
#include <math.h>
#include <string.h>

// ============================================================================
// Helper: tiny popcount for 10-bit masks (planner only, no HW side effects)
// ============================================================================

static inline uint8_t _pop10(uint16_t m) {
#if defined(__GNUC__)
    return (uint8_t)__builtin_popcount(m);
#else
    uint8_t c = 0;
    while (m) { c += (m & 1); m >>= 1; }
    return c;
#endif
}

// ============================================================================
// Helper: parallel Req for subset mask (planner only)
// ============================================================================

static inline float _req(uint16_t mask, const float R[10]) {
    float g = 0.0f;
    for (uint8_t i = 0; i < 10; ++i) {
        if (mask & (1u << i)) {
            const float Ri = R[i];
            if (Ri > 0.01f && isfinite(Ri)) {
                g += 1.0f / Ri;
            }
        }
    }
    if (g <= 0.0f) return INFINITY;
    return 1.0f / g;
}

// ============================================================================
// Helper: choose best subset vs target Req (planner only)
// ============================================================================

static uint16_t _chooseBest(uint16_t allowedMask,
                            const float R[10],
                            float target,
                            uint8_t maxActive,
                            bool preferAboveOrEqual,
                            uint16_t recentMask)
{
    float    bestScore  = INFINITY;
    uint16_t best       = 0;
    bool     foundAbove = false;
    const uint16_t FULL = (1u << 10);

    for (uint16_t m = 1; m < FULL; ++m) {
        if ((m & ~allowedMask) != 0) continue;      // must be subset

        const uint8_t k = _pop10(m);
        if (k == 0 || k > maxActive) continue;

        const float Req = _req(m, R);
        if (!isfinite(Req)) continue;

        const bool  above = (Req >= target);
        const float err   = fabsf(Req - target);

        if (preferAboveOrEqual) {
            if (above && !foundAbove) {
                foundAbove = true;
                bestScore  = INFINITY;
                best       = 0;
            }
            if (!above && foundAbove) {
                continue; // once we have ≥target, ignore undershoots
            }
        }

        float score = err;
        if (m == recentMask) {
            score += 0.0001f; // tiny bias to avoid repeating same mask
        }

        if (score < bestScore ||
            (score == bestScore && k < _pop10(best)) ||
            (score == bestScore && k == _pop10(best) && Req > _req(best, R)))
        {
            bestScore = score;
            best      = m;
        }
    }

    // Fallback if "≥target" constraint was too strict
    if (preferAboveOrEqual && best == 0) {
        return _chooseBest(allowedMask, R, target,
                           maxActive, /*preferAboveOrEqual=*/false, recentMask);
    }

    return best;
}

// ============================================================================
// Helper: build one planner "supercycle" (planner only; no HW)
// ============================================================================

static void _buildPlan(std::vector<uint16_t>& plan,
                       uint16_t allowedMask,
                       const float R[10],
                       float target,
                       uint8_t maxActive,
                       bool preferAboveOrEqual)
{
    plan.clear();
    uint16_t remaining = allowedMask;
    uint16_t last      = 0;

    while (remaining) {
        uint16_t pick = _chooseBest(remaining, R, target,
                                    maxActive, preferAboveOrEqual, last);
        if (pick == 0) {
            // Fallback: pick best single to cover remaining
            float    bestErr = INFINITY;
            uint16_t solo    = 0;
            for (uint8_t i = 0; i < 10; ++i) {
                if (remaining & (1u << i)) {
                    const float Req = _req((1u << i), R);
                    const float err = fabsf(Req - target);
                    if (err < bestErr) {
                        bestErr = err;
                        solo    = (1u << i);
                    }
                }
            }
            pick = solo;
            if (pick == 0) break;
        }

        plan.push_back(pick);
        remaining &= ~pick;
        last = pick;
    }
}

// ============================================================================
// Helper: allowed[] -> bitmask
// ============================================================================

static inline uint16_t _allowedMaskFrom(const bool allowed[10]) {
    uint16_t m = 0;
    for (uint8_t i = 0; i < 10; ++i) {
        if (allowed[i]) m |= (1u << i);
    }
    return m;
}

// ============================================================================
// Helper: single guarded ON pulse for a mask
// ============================================================================
//
// HARD SAFETY RULES:
//  - Only called from StartLoop() while in DeviceState::Running.
//  - Never called from ctor/begin/Idle/power-tracking/thermal code.
//  - Uses HeaterManager::setOutputMask(mask) once, then ALWAYS back to 0.
//  - Uses delayWithPowerWatch() for STOP/12V/OC abort.
//  - On successful pulse, calls updatePresenceFromMask() (logic-only).
//  - Never touches PowerTracker (separation of concerns).
// ============================================================================

static bool _runMaskedPulse(Device* self,
                            uint16_t mask,
                            uint32_t onTimeMs,
                            bool ledFeedback)
{
    if (!self || !WIRE)              return true;
    if (mask == 0 || onTimeMs == 0)  return true;
    if (self->getState() != DeviceState::Running) {
        // Do not energize if not in RUN
        return false;
    }

    // Apply mask atomically.
    WIRE->setOutputMask(mask);

    // Optional LED mirror.
    if (ledFeedback && self->indicator) {
        for (uint8_t i = 0; i < 10; ++i) {
            self->indicator->setLED(i + 1, (mask & (1u << i)) != 0);
        }
    }

    // Keep ON with protection-aware delay.
    bool ok = self->delayWithPowerWatch(onTimeMs);

    // If pulse completed (no fault/STOP), update presence from that pulse.
    if (ok && self->currentSensor) {
        const float I = self->currentSensor->readCurrent();
        // Implementation on HeaterManager side must be non-heating logic only.
        WIRE->updatePresenceFromMask(mask, I);
    }

    // ALWAYS ensure outputs are OFF (success or abort).
    WIRE->setOutputMask(0);
    if (ledFeedback && self->indicator) {
        self->indicator->clearAll();
    }

    return ok;
}

// ============================================================================
// Loop Task Management & State Machine
// ============================================================================

void Device::startLoopTask() {
    if (loopTaskHandle != nullptr) {
        DEBUG_PRINTLN("[Device] Loop task already running");
        return;
    }

    DEBUG_PRINTLN("[Device] Starting main loop task");
    BaseType_t result = xTaskCreatePinnedToCore(
        Device::loopTaskWrapper,
        "DeviceLoopTask",
        DEVICE_LOOP_TASK_STACK_SIZE,
        this,
        DEVICE_LOOP_TASK_PRIORITY,
        &loopTaskHandle,
        DEVICE_LOOP_TASK_CORE
    );

    if (result != pdPASS) {
        DEBUG_PRINTLN("[Device] Failed to create DeviceLoopTask");
        loopTaskHandle = nullptr;
    }
}

void Device::loopTaskWrapper(void* param) {
    Device* self = static_cast<Device*>(param);
    self->loopTask();
}

void Device::loopTask() {
    DEBUG_PRINTLN("[Device] 🔁 Device loop task started");
    BUZZ->bip();

    // Hard baseline: no power path, no heaters, no LEDs.
    relayControl->turnOff();
    stopTemperatureMonitor();
    if (WIRE)      WIRE->disableAll();
    if (indicator) indicator->clearAll();
    RGB->setOff();

    for (;;) {
        // ========================= OFF STATE =========================
        setState(DeviceState::Shutdown);

        // If WiFi is disabled and we're idle, enter deep sleep until button wake.
        bool wifiOn = (WIFI && WIFI->isWifiOn());
        if (!wifiOn) {
            prepareForDeepSleep();
            SLEEP->goToSleep();
            // esp_deep_sleep_start() does not return, but guard just in case.
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Legacy remote start → request WAKE+RUN
        if (StartFromremote) {
            StartFromremote = false;
            if (gEvt) xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
        }

        DEBUG_PRINTLN("[Device] State=OFF. Waiting for WAKE …");

        if (gEvt) {
            xEventGroupWaitBits(
                gEvt,
                EVT_WAKE_REQ,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY
            );
        }

        // ===================== POWER-UP SEQUENCE =====================
        RGB->setWait();
        BUZZ->bip();
        DEBUG_PRINTLN("[Device] Waiting for 12V input…");

        while (!digitalRead(DETECT_12V_PIN)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        DEBUG_PRINTLN("[Device] 12V detected → enabling relay");
        relayControl->turnOn();
        RGB->postOverlay(OverlayEvent::RELAY_ON);
        vTaskDelay(pdMS_TO_TICKS(150));

        // Charge capacitors to GO_THRESHOLD_RATIO.
        TickType_t lastChargePost = 0;
        while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
            TickType_t now = xTaskGetTickCount();
            if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                lastChargePost = now;
            }
            DEBUG_PRINTF("[Device] Charging… Cap=%.2fV Target=%.2fV\n",
                         discharger->readCapVoltage(), GO_THRESHOLD_RATIO);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        DEBUG_PRINTLN("[Device] Threshold met → bypass inrush");
        RGB->postOverlay(OverlayEvent::PWR_BYPASS_ON);

        // Ensure NO heaters during idle calibration.
        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();

        // Idle current calibration (uses only relay+AC; no heaters).
        calibrateIdleCurrent();

        // IMPORTANT:
        //  No probeWirePresence() here.
        //  No mask pulses here.
        //  We start with config+thermal only; presence refined in StartLoop().

        checkAllowedOutputs();
        BUZZ->bipSystemReady();
        RGB->postOverlay(OverlayEvent::WAKE_FLASH);

        // ======================= IDLE STATE =======================
        bool runRequested = false;
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_RUN_REQ) {
                xEventGroupClearBits(gEvt, EVT_RUN_REQ);
                runRequested = true;
            }
        }

        if (!runRequested) {
            setState(DeviceState::Idle);
            DEBUG_PRINTLN("[Device] State=IDLE. Waiting for RUN or STOP");
            RGB->setIdle();

            if (gEvt) {
                EventBits_t got = xEventGroupWaitBits(
                    gEvt,
                    EVT_RUN_REQ | EVT_STOP_REQ,
                    pdTRUE,
                    pdFALSE,
                    portMAX_DELAY
                );

                if (got & EVT_STOP_REQ) {
                    DEBUG_PRINTLN("[Device] STOP in IDLE → full OFF");
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                    relayControl->turnOff();
                    if (WIRE)      WIRE->disableAll();
                    if (indicator) indicator->clearAll();
                    RGB->setOff();
                    continue; // back to OFF state
                }
            }
        }

        // ======================== RUN STATE =========================
        setState(DeviceState::Running);
        DEBUG_PRINTLN("[Device] State=RUN. Entering StartLoop()");
        BUZZ->successSound();
        RGB->postOverlay(OverlayEvent::PWR_START);
        RGB->setRun();

        StartLoop(); // will block until STOP/FAULT/NO-WIRE

        // =================== CLEAN SHUTDOWN → OFF ===================
        DEBUG_PRINTLN("[Device] StartLoop finished → clean shutdown");
        BUZZ->bipSystemShutdown();

        RGB->postOverlay(OverlayEvent::RELAY_OFF);
        relayControl->turnOff();

        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();
        RGB->setOff();

        // loop back to OFF
    }
}

// ============================================================================
// RAII guard for PowerTracker session (no heater side effects)
// ============================================================================

struct RunSessionGuard {
    Device* dev;
    bool    active = false;

    explicit RunSessionGuard(Device* d) : dev(d) {}

    void begin(float busV, float idleA) {
        if (!dev || active) return;
        POWER_TRACKER->startSession(busV, idleA);
        active = true;
    }

    void tick() {
        if (!dev || !active) return;
        if (dev->currentSensor) {
            POWER_TRACKER->update(*dev->currentSensor);
        }
    }

    void end(bool success) {
        if (!dev || !active) return;
        // Final update
        tick();
        POWER_TRACKER->endSession(success);
        active = false;
    }

    ~RunSessionGuard() {
        if (active) {
            // If we exit unexpectedly, mark as failed but cleanly closed.
            end(false);
        }
    }
};

// ============================================================================
// StartLoop(): main heating behavior (sequential / advanced)
// ============================================================================

void Device::StartLoop() {
    if (getState() != DeviceState::Running) {
        return;
    }

    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] StartLoop: entering main heating loop");
    DEBUG_PRINTLN("-----------------------------------------------------------");

    // 1) Thermal model ready & wires cooled.
    initWireThermalModelOnce();
    waitForWiresNearAmbient(5.0f, 0);

    // 2) Auto wire-detect ONLY here, ONLY if all wires are < 150°C.
    if (WIRE && currentSensor) {
        const float maxProbeTempC = 150.0f;
        bool anyTooHot = false;

        for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
            const float t = WIRE->getWireEstimatedTemp(i);
            if (isfinite(t) && t >= maxProbeTempC) {
                anyTooHot = true;
                break;
            }
        }

        if (!anyTooHot) {
            DEBUG_PRINTLN("[Device] Auto wire-detect (RUN, <150°C) ✅");
            WIRE->probeWirePresence(*currentSensor);
        } else {
            DEBUG_PRINTLN("[Device] Skip auto wire-detect (hot wire ≥150°C) 🚫");
        }
    }

    // 3) Start continuous current & thermal integration (observers only).
    if (currentSensor && !currentSensor->isContinuousRunning()) {
        currentSensor->startContinuous(0);
    }
    if (thermalTaskHandle == nullptr) {
        startThermalTask();
    }
    startTemperatureMonitor();

    // 4) Ensure power path is ready for active operation.
    relayControl->turnOn();

    // 5) Initial allowed outputs (cfg + thermal + presence).
    checkAllowedOutputs();

    // 6) Setup PowerTracker session (no control over outputs).
    float busV = DEFAULT_DC_VOLTAGE;
    if (CONF) {
        float vdc = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        if (vdc > 0.0f) {
            busV = vdc;
        } else {
            float vset = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY,
                                        DEFAULT_DESIRED_OUTPUT_VOLTAGE);
            if (vset > 0.0f) busV = vset;
        }
    }

    float idleA = DEFAULT_IDLE_CURR;
    if (CONF) {
        idleA = CONF->GetFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);
    }
    if (idleA < 0.0f) idleA = 0.0f;

    RunSessionGuard session(this);
    session.begin(busV, idleA);

    const uint16_t onTime      = CONF->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
    const uint16_t offTime     = CONF->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    const bool     ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    DEBUG_PRINTF("[Device] Loop config: on=%ums off=%ums mode=%s\n",
                 (unsigned)onTime,
                 (unsigned)offTime,
#if (DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_SEQUENTIAL)
                 "SEQUENTIAL"
#else
                 "ADVANCED"
#endif
    );

#if (DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_SEQUENTIAL)

    // ====================== SEQUENTIAL MODE ======================

    DEBUG_PRINTLN("[Device] Mode: SEQUENTIAL");

    while (getState() == DeviceState::Running) {
        // Abort conditions
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP → exit SEQ loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
                break;
            }
        }

        // Refresh allowed.
        checkAllowedOutputs();

        // If no connected wires → fault & exit.
        if (WIRE && !WIRE->hasAnyConnected()) {
            DEBUG_PRINTLN("[Device] No connected wires → abort SEQ loop");
            if (RGB) {
                RGB->setFault();
                RGB->postOverlay(OverlayEvent::FAULT_SENSOR_MISSING);
            }
            if (BUZZ) BUZZ->bipFault();
            if (StateLock()) {
                setState(DeviceState::Error);
                StateUnlock();
            }
            break;
        }

        // Choose coolest allowed wire.
        bool    found    = false;
        uint8_t bestIdx  = 0;
        float   bestTemp = 1e9f;

        for (uint8_t i = 0; i < 10; ++i) {
            if (!allowedOutputs[i]) continue;
            float t = WIRE->getWireEstimatedTemp(i + 1);
            if (isnan(t)) t = ambientC;
            if (t < bestTemp) {
                bestTemp = t;
                bestIdx  = i;
                found    = true;
            }
        }

        if (!found) {
            // No eligible wires; short wait, still track power.
            if (!delayWithPowerWatch(100)) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Idle);
                }
                break;
            }
            session.tick();
            continue;
        }

        const uint16_t mask = (1u << bestIdx);

        // Single guarded pulse.
        if (!_runMaskedPulse(this, mask, onTime, ledFeedback)) {
            // Aborted by STOP/12V/OC.
            if (!is12VPresent()) handle12VDrop();
            else setState(DeviceState::Idle);
            break;
        }

        session.tick();

        // OFF window.
        if (!delayWithPowerWatch(offTime)) {
            if (!is12VPresent()) handle12VDrop();
            else {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
            }
            break;
        }

        session.tick();
    }

#else  // DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_ADVANCED

    // ====================== ADVANCED MODE ======================

    DEBUG_PRINTLN("[Device] Mode: ADVANCED");

    while (getState() == DeviceState::Running) {
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP → exit ADV loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
                break;
            }
        }

        checkAllowedOutputs();

        if (WIRE && !WIRE->hasAnyConnected()) {
            DEBUG_PRINTLN("[Device] No connected wires → abort ADV loop");
            if (RGB) {
                RGB->setFault();
                RGB->postOverlay(OverlayEvent::FAULT_SENSOR_MISSING);
            }
            if (BUZZ) BUZZ->bipFault();
            if (StateLock()) {
                setState(DeviceState::Error);
                StateUnlock();
            }
            break;
        }

        const uint16_t allowedMask = _allowedMaskFrom(allowedOutputs);
        if (allowedMask == 0) {
            if (!delayWithPowerWatch(100)) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Idle);
                }
                break;
            }
            session.tick();
            continue;
        }

        float R[10] = {
            CONF->GetFloat(R01OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R02OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R03OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R04OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R05OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R06OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R07OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R08OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R09OHM_KEY, DEFAULT_WIRE_RES_OHMS),
            CONF->GetFloat(R10OHM_KEY, DEFAULT_WIRE_RES_OHMS),
        };

        const float   targetRes     = CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);
        const uint8_t maxActive     = MAX_ACTIVE;
        const bool    preferAboveEq = PREF_ABOVE;

        std::vector<uint16_t> plan;
        _buildPlan(plan, allowedMask, R, targetRes, maxActive, preferAboveEq);

        if (plan.empty()) {
            DEBUG_PRINTLN("[Device] [ADV] Planner empty; wait & retry");
            if (!delayWithPowerWatch(200)) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Idle);
                }
                break;
            }
            session.tick();
            continue;
        }

        for (uint16_t mask : plan) {
            if (getState() != DeviceState::Running) break;

            if (!_runMaskedPulse(this, mask, onTime, ledFeedback)) {
                if (!is12VPresent()) handle12VDrop();
                else setState(DeviceState::Idle);
                break;
            }

            session.tick();

            if (!delayWithPowerWatch(offTime)) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Idle);
                }
                break;
            }

            session.tick();
        }

    }

#endif // DEVICE_LOOP_MODE

    // --- Session finalize & hard-off ---
    const bool success = (getState() != DeviceState::Error);
    session.end(success);

    if (WIRE)      WIRE->disableAll();
    if (indicator) indicator->clearAll();
    stopTemperatureMonitor();
}
