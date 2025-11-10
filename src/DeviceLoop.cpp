#include "Device.h"
#include "Utils.h"
#include "RGBLed.h"
#include "Buzzer.h"
#include <vector>
#include <math.h>
#include <string.h> // for memset if ever needed

// === Multi-output heating helpers (file-local; no header changes needed) ====
//
// These helpers implement the same planner logic as your Python tester:
//  - _pop10     : tiny popcount for 10-bit masks
//  - _req       : parallel equivalent resistance for subset mask
//  - _chooseBest: best subset vs target Req with tie-breakers
//  - _buildPlan : covers all allowed wires at least once
//
// They are used only in ADVANCED mode to compute batch/group masks.
// ============================================================================

// tiny popcount for 10-bit masks
static inline uint8_t _pop10(uint16_t m) {
#if defined(__GNUC__)
    return (uint8_t)__builtin_popcount(m);
#else
    uint8_t c = 0;
    while (m) { c += (m & 1); m >>= 1; }
    return c;
#endif
}

// parallel equivalent resistance for a set of wires
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

// choose best subset (≤ maxActive) inside allowedMask w.r.t. target
static uint16_t _chooseBest(uint16_t allowedMask,
                            const float R[10],
                            float target,
                            uint8_t maxActive,
                            bool preferAboveOrEqual,
                            uint16_t recentMask)
{
    float    bestScore = INFINITY;
    uint16_t best      = 0;
    bool     foundAbove = false;
    const uint16_t FULL = (1u << 10);

    for (uint16_t m = 1; m < FULL; ++m) {
        // Must be subset of allowed
        if ((m & ~allowedMask) != 0) continue;

        const uint8_t k = _pop10(m);
        if (k == 0 || k > maxActive) continue;

        const float Req = _req(m, R);
        if (!isfinite(Req)) continue;

        const bool  above = (Req >= target);
        const float err   = fabsf(Req - target);

        if (preferAboveOrEqual) {
            if (above && !foundAbove) {
                // First candidate that is ≥ target:
                // reset selection space to only consider "above" from now.
                foundAbove = true;
                bestScore  = INFINITY;
                best       = 0;
            }
            if (!above && foundAbove) {
                // Once we have ≥ target options, ignore undershoots.
                continue;
            }
        }

        float score = err;
        if (m == recentMask) {
            score += 0.0001f; // mild fairness penalty for repeating last mask
        }

        // Tie-breakers:
        //  1) lower score
        //  2) fewer active wires
        //  3) for equal size: higher Req (safer, less current)
        if (score < bestScore ||
            (score == bestScore && k < _pop10(best)) ||
            (score == bestScore && k == _pop10(best) && Req > _req(best, R)))
        {
            bestScore = score;
            best      = m;
        }
    }

    // If we insisted on ≥ target but got nothing, retry once without that constraint
    if (preferAboveOrEqual && best == 0) {
        return _chooseBest(allowedMask, R, target, maxActive,
                           /*preferAboveOrEqual=*/false, recentMask);
    }

    return best;
}

// build one supercycle "plan": covers every allowed wire at least once
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
            // No multi-group available → pick best single wire to finish coverage
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

// turn a group ON/OFF with LED mirroring
static void _applyMask(Device* self, uint16_t mask, bool on, bool ledFeedback)
{
    for (uint8_t i = 0; i < 10; ++i) {
        if (mask & (1u << i)) {
            if (WIRE) {
                WIRE->setOutput(i + 1, on);
            }
            if (ledFeedback && self->indicator) {
                self->indicator->setLED(i + 1, on);
            }
        }
    }
}

// boolean array -> 10-bit allowed mask
static inline uint16_t _allowedMaskFrom(const bool allowed[10]) {
    uint16_t m = 0;
    for (uint8_t i = 0; i < 10; ++i) {
        if (allowed[i]) m |= (1u << i);
    }
    return m;
}

// ============================================================================
// Helper: run one ON pulse with RTOS-friendly mask control
// ============================================================================
//
// **Modified for dynamic presence detection**
//
// - Uses HeaterManager::setOutputMask(mask).
// - Uses delayWithPowerWatch() for STOP/12V monitoring.
// - Mirrors on indicator LEDs if enabled.
// - On a completed pulse, reads current and calls
//   HeaterManager::updatePresenceFromMask(mask, I)
//   so wire presence (connect / disconnect) is tracked live.
// ============================================================================

static bool _runMaskedPulse(Device* self,
                            uint16_t mask,
                            uint32_t onTimeMs,
                            bool ledFeedback)
{
    if (!WIRE || mask == 0 || onTimeMs == 0) {
        return true; // nothing to do
    }

    // Apply mask atomically.
    WIRE->setOutputMask(mask);

    // Optional LED mirror.
    if (ledFeedback && self->indicator) {
        for (uint8_t i = 0; i < 10; ++i) {
            bool on = (mask & (1u << i)) != 0;
            self->indicator->setLED(i + 1, on);
        }
    }

    // Keep ON for requested time, with power/STOP checks.
    bool ok = self->delayWithPowerWatch(onTimeMs);

    // If we completed the pulse (not aborted), sample current once
    // with the mask still active and update presence.
    if (ok && self->currentSensor) {
        float I = self->currentSensor->readCurrent();
        // This must be implemented in HeaterManager:
        //  - For SEQ: single-bit mask → update that wire.
        //  - For ADV: multi-bit mask → update all bits in that group
        //    consistently based on ratio vs expected.
        WIRE->updatePresenceFromMask(mask, I);
    }

    // Always turn everything OFF after the pulse / abort.
    WIRE->setOutputMask(0);

    if (ledFeedback && self->indicator) {
        self->indicator->clearAll();
    }

    return ok;
}


// ============================================================================
// Loop task management & main state machine
// ============================================================================

void Device::startLoopTask() {
    if (loopTaskHandle == nullptr) {
        DEBUG_PRINTLN("[Device] Starting main loop task on RTOS 🧵");

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
            DEBUG_PRINTLN("[Device] Failed to create DeviceLoopTask ❌");
            loopTaskHandle = nullptr;
        }
    } else {
        DEBUG_PRINTLN("[Device] Loop task is already running ⏳");
    }
}

void Device::loopTaskWrapper(void* param) {
    Device* self = static_cast<Device*>(param);
    self->loopTask();
}

void Device::loopTask() {
    DEBUG_PRINTLN("[Device] 🔁 Device loop task started");
    BUZZ->bip();

    // Safe baseline at task start
    relayControl->turnOff();
    bypassFET->disable();
    stopTemperatureMonitor();
    RGB->setOff();

    for (;;) {
        // ========================= OFF STATE =========================
        if (StateLock()) { currentState = DeviceState::Shutdown; StateUnlock(); }

        // Backward compatibility: remote start shortcut → WAKE+RUN
        if (StartFromremote) {
            StartFromremote = false;
            if (gEvt) xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
        }

        DEBUG_PRINTLN("[Device] State=OFF. Waiting for WAKE (Tap#1 / Web) …");

        if (gEvt) {
            // Wait until WAKE request; auto-clear consumed bit.
            xEventGroupWaitBits(gEvt, EVT_WAKE_REQ, pdTRUE, pdFALSE, portMAX_DELAY);
        }

        // ===================== POWER-UP SEQUENCE =====================
        RGB->setWait();
        BUZZ->bip();
        DEBUG_PRINTLN("[Device] Waiting for 12V input… 🔋");

        while (!digitalRead(DETECT_12V_PIN)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        DEBUG_PRINTLN("[Device] 12V Detected – Enabling input relay ✅");
        relayControl->turnOn();
        RGB->postOverlay(OverlayEvent::RELAY_ON);

        vTaskDelay(pdMS_TO_TICKS(150));

        // Charge capacitor bank to GO_THRESHOLD_RATIO
        TickType_t lastChargePost = 0;
        while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
            TickType_t now = xTaskGetTickCount();
            if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                lastChargePost = now;
            }

            DEBUG_PRINTF("[Device] Charging… Cap: %.2fV / Target: %.2fV ⏳\n",
                         discharger->readCapVoltage(), GO_THRESHOLD_RATIO);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        RGB->postOverlay(OverlayEvent::PWR_THRESH_OK);
        DEBUG_PRINTLN("[Device] Voltage threshold met ✅ Bypassing inrush resistor 🔄");
        bypassFET->enable();
        RGB->postOverlay(OverlayEvent::PWR_BYPASS_ON);

        // Ensure no heaters during idle calibration
        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();

        // Learn idle current baseline (AC+relay+caps only)
        calibrateIdleCurrent();

        // Initial wire presence scan (static)
        if (WIRE && currentSensor) {
            WIRE->probeWirePresence(*currentSensor);
        }

        // Initialize allowed outputs from config + thermal lockouts + presence
        checkAllowedOutputs();
        BUZZ->bipSystemReady();
        RGB->postOverlay(OverlayEvent::WAKE_FLASH);

        // If RUN already requested (e.g., from web), skip IDLE
        bool runRequested = false;
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_RUN_REQ) {
                xEventGroupClearBits(gEvt, EVT_RUN_REQ);
                runRequested = true;
            }
        }

        if (!runRequested) {
            // ======================= IDLE STATE =======================
            if (StateLock()) { currentState = DeviceState::Idle; StateUnlock(); }
            DEBUG_PRINTLN("[Device] State=IDLE. Waiting for RUN (Tap#2) or STOP …");
            RGB->setIdle();

            if (gEvt) {
                EventBits_t got = xEventGroupWaitBits(
                    gEvt,
                    EVT_RUN_REQ | EVT_STOP_REQ,
                    pdTRUE,      // clear bits on exit
                    pdFALSE,
                    portMAX_DELAY
                );

                if (got & EVT_STOP_REQ) {
                    // Back to OFF, full power-down
                    DEBUG_PRINTLN("[Device] STOP in IDLE → OFF");
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                    relayControl->turnOff();
                    bypassFET->disable();
                    RGB->setOff();
                    continue; // restart OFF loop
                }
            }
        }

        // ======================== RUN STATE =========================
        if (StateLock()) { currentState = DeviceState::Running; StateUnlock(); }
        DEBUG_PRINTLN("[Device] State=RUN. Launching main loop ▶️");
        BUZZ->successSound();
        RGB->postOverlay(OverlayEvent::PWR_START);
        RGB->setRun();

        // Execute main heating loop (blocks until STOP/FAULT/NO-WIRE)
        StartLoop();

        // =================== CLEAN SHUTDOWN → OFF ===================
        DEBUG_PRINTLN("[Device] StartLoop returned. Clean shutdown 🛑");
        BUZZ->bipSystemShutdown();
        RGB->postOverlay(OverlayEvent::RELAY_OFF);
        relayControl->turnOff();
        bypassFET->disable();
        RGB->setOff();
        // loop back to OFF state
    }
}

// ============================================================================
// StartLoop(): main heating behavior (sequential / advanced)
// ============================================================================

void Device::StartLoop() {
    if (currentState != DeviceState::Running) {
        return;
    }

    DEBUGGSTART();
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Loop Sequence 🔻 (RTOS/history-based)");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUGGSTOP();

    // Ensure thermal model is initialized and wires are near ambient.
    initWireThermalModelOnce();
    waitForWiresNearAmbient(5.0f, 0);

    // Start continuous current sampling + thermal integration if available.
    if (currentSensor && !currentSensor->isContinuousRunning()) {
        currentSensor->startContinuous(0); // default period from CurrentSensor
    }
    if (thermalTaskHandle == nullptr) {
        startThermalTask();
    }

    // DS18B20 monitoring in background for ambient/safety.
    startTemperatureMonitor();

    // Ensure bypass and relay are correctly set for active operation.
    relayControl->turnOn();
    bypassFET->enable();

    // Initial evaluation of allowed outputs (cfg + thermal + presence).
    checkAllowedOutputs();

    // -------- PowerTracker: start a new heating session --------
    float busV = DEFAULT_DC_VOLTAGE;
    if (CONF) {
        float vdc = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        if (vdc > 0.0f) {
            busV = vdc;
        } else {
            float vset = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, DEFAULT_DESIRED_OUTPUT_VOLTAGE);
            if (vset > 0.0f) busV = vset;
        }
    }

    float idleA = DEFAULT_IDLE_CURR;
    if (CONF) {
        idleA = CONF->GetFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);
    }
    if (idleA < 0.0f) idleA = 0.0f;

    // Start session even if currentSensor is null: will at least count duration/sessions.
    POWER_TRACKER->startSession(busV, idleA);

    const uint16_t onTime      = CONF->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
    const uint16_t offTime     = CONF->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    const bool     ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    DEBUG_PRINTF("[Device] Loop config: onTime=%ums offTime=%ums mode=%s\n",
                 (unsigned)onTime,
                 (unsigned)offTime,
#if (DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_SEQUENTIAL)
                 "SEQUENTIAL"
#else
                 "ADVANCED"
#endif
    );

#if (DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_SEQUENTIAL)

    // =====================================================================
    // SEQUENTIAL MODE:
    //  - Drive ONE allowed output at a time.
    //  - Always pick the coolest eligible wire.
    //  - Dynamic presence:
    //      * Each pulse updates WireInfo.connected via updatePresenceFromMask().
    //      * checkAllowedOutputs() folds presence into allowedOutputs[].
    //      * If no wires remain connected → stop loop.
    //  - PowerTracker:
    //      * update() called regularly to integrate Wh from history.
    // =====================================================================

    DEBUG_PRINTLN("[Device] Loop mode: SEQUENTIAL (coolest-first, history-based)");

    while (currentState == DeviceState::Running) {
        // Global abort checks
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                DEBUG_PRINTLN("[Device] STOP requested → exit SEQ loop");
                currentState = DeviceState::Idle;
                break;
            }
        }

        // Refresh permissions (cfg + thermal lockouts + dynamic presence)
        checkAllowedOutputs();

        // If all wires are now considered missing, abort safely.
        if (WIRE && !WIRE->hasAnyConnected()) {
            DEBUG_PRINTLN("[Device] No connected heater wires remain → aborting SEQ loop.");
            if (RGB) {
                RGB->setFault();
                RGB->postOverlay(OverlayEvent::FAULT_SENSOR_MISSING);
            }
            if (BUZZ) {
                BUZZ->bipFault();
            }
            if (StateLock()) {
                currentState = DeviceState::Error;
                StateUnlock();
            }
            break;
        }

        // Pick coolest allowed wire
        bool    found    = false;
        uint8_t bestIdx  = 0;
        float   bestTemp = 1e9f;

        for (uint8_t i = 0; i < 10; ++i) {
            if (!allowedOutputs[i]) continue;

            float t = WIRE->getWireEstimatedTemp(i + 1);
            if (isnan(t)) {
                // If no estimate yet, assume ambient (safe/cool).
                t = ambientC;
            }
            if (t < bestTemp) {
                bestTemp = t;
                bestIdx  = i;
                found    = true;
            }
        }

        if (!found) {
            // No eligible wires: either thermal-locked or temporarily none.
            // Wait a bit with abort checks; planner will re-evaluate.
            if (!delayWithPowerWatch(100)) {
                if (!is12VPresent()) {
                    handle12VDrop();
                } else if (gEvt) {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    currentState = DeviceState::Idle;
                }
                break;
            }

            // Track energy during idle wait (uses current history)
            if (currentSensor) {
                POWER_TRACKER->update(*currentSensor);
            }
            continue;
        }

        const uint16_t mask = (1u << bestIdx);

        // Run one guarded pulse on this single wire.
        if (!_runMaskedPulse(this, mask, onTime, ledFeedback)) {
            // Aborted by STOP/12V.
            if (!is12VPresent()) {
                handle12VDrop();
            } else {
                currentState = DeviceState::Idle;
            }
            break;
        }

        // Integrate energy for this ON period.
        if (currentSensor) {
            POWER_TRACKER->update(*currentSensor);
        }

        // OFF window between activations.
        if (!delayWithPowerWatch(offTime)) {
            if (!is12VPresent()) {
                handle12VDrop();
            } else if (gEvt) {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                currentState = DeviceState::Idle;
            }
            break;
        }

        // Integrate during OFF / background as well.
        if (currentSensor) {
            POWER_TRACKER->update(*currentSensor);
        }
    }

#else // DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_ADVANCED

    // =====================================================================
    // ADVANCED MODE:
    //  - Multi-output grouped drive using planner.
    //  - Dynamic presence:
    //      * Each group pulse calls updatePresenceFromMask().
    //      * checkAllowedOutputs() uses presence each cycle.
    //      * If no wires remain connected → stop loop.
    //  - PowerTracker:
    //      * update() called regularly to integrate Wh from history.
    // =====================================================================

    DEBUG_PRINTLN("[Device] Loop mode: ADVANCED (planner + history-based thermal)");

    while (currentState == DeviceState::Running) {
        // Abort checks
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                DEBUG_PRINTLN("[Device] STOP requested → exit ADV loop");
                currentState = DeviceState::Idle;
                break;
            }
        }

        // Refresh allowed outputs from cfg + thermal + dynamic presence.
        checkAllowedOutputs();

        // If all wires are gone, abort safely.
        if (WIRE && !WIRE->hasAnyConnected()) {
            DEBUG_PRINTLN("[Device] No connected heater wires remain → aborting ADV loop.");
            if (RGB) {
                RGB->setFault();
                RGB->postOverlay(OverlayEvent::FAULT_SENSOR_MISSING);
            }
            if (BUZZ) {
                BUZZ->bipFault();
            }
            if (StateLock()) {
                currentState = DeviceState::Error;
                StateUnlock();
            }
            break;
        }

        const uint16_t allowedMask = _allowedMaskFrom(allowedOutputs);
        if (allowedMask == 0) {
            // Nothing available right now → short wait, let thermal or presence
            // changes re-open options.
            if (!delayWithPowerWatch(100)) {
                if (!is12VPresent()) {
                    handle12VDrop();
                } else if (gEvt) {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    currentState = DeviceState::Idle;
                }
                break;
            }

            if (currentSensor) {
                POWER_TRACKER->update(*currentSensor);
            }
            continue;
        }

        // Build planner input: base resistances (as before).
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

        const float    targetRes     = CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);
        const uint8_t  maxActive     = MAX_ACTIVE;
        const bool     preferAboveEq = PREF_ABOVE;

        std::vector<uint16_t> plan;
        _buildPlan(plan, allowedMask, R, targetRes, maxActive, preferAboveEq);

        if (plan.empty()) {
            DEBUG_PRINTLN("[Device] [ADV] Planner returned empty plan; waiting...");
            if (!delayWithPowerWatch(200)) {
                if (!is12VPresent()) {
                    handle12VDrop();
                } else if (gEvt) {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    currentState = DeviceState::Idle;
                }
                break;
            }

            if (currentSensor) {
                POWER_TRACKER->update(*currentSensor);
            }
            continue;
        }

        // Execute each group mask as one pulse.
        for (uint16_t mask : plan) {
            if (currentState != DeviceState::Running) {
                break;
            }

            if (!_runMaskedPulse(this, mask, onTime, ledFeedback)) {
                // Aborted by STOP/12V.
                if (!is12VPresent()) {
                    handle12VDrop();
                } else {
                    currentState = DeviceState::Idle;
                }
                break;
            }

            // Integrate after each group pulse.
            if (currentSensor) {
                POWER_TRACKER->update(*currentSensor);
            }

            // OFF window between groups.
            if (!delayWithPowerWatch(offTime)) {
                if (!is12VPresent()) {
                    handle12VDrop();
                } else if (gEvt) {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    currentState = DeviceState::Idle;
                }
                break;
            }

            // Integrate during OFF as well.
            if (currentSensor) {
                POWER_TRACKER->update(*currentSensor);
            }
        }

        // Optional: recharge handling preserved as-is, with tracking.
        if (rechargeMode == RechargeMode::BatchRecharge &&
            currentState == DeviceState::Running)
        {
            TickType_t lastChargePost = 0;
            while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO &&
                   currentState == DeviceState::Running)
            {
                if (!is12VPresent()) {
                    handle12VDrop();
                    break;
                }

                TickType_t now = xTaskGetTickCount();
                if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                    RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                    lastChargePost = now;
                }

                DEBUG_PRINTF("[Device] [ADV] Recharging… Cap: %.2fV / Target: %.2fV\n",
                             discharger->readCapVoltage(), GO_THRESHOLD_RATIO);

                if (!delayWithPowerWatch(200)) {
                    if (!is12VPresent()) {
                        handle12VDrop();
                    } else if (gEvt) {
                        xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                        currentState = DeviceState::Idle;
                    }
                    break;
                }

                if (currentSensor) {
                    POWER_TRACKER->update(*currentSensor);
                }
            }
        }
    }

#endif // DEVICE_LOOP_MODE

    // --- Finalize power tracking session (any exit path) ---
    if (POWER_TRACKER->isSessionActive()) {
        if (currentSensor) {
            // Consume remaining history window
            POWER_TRACKER->update(*currentSensor);
        }

        // Treat non-Error as "successful" session; Error / FAULT as aborted.
        bool success = (currentState != DeviceState::Error);
        POWER_TRACKER->endSession(success);
    }

    // Common exit: ensure everything is off & safe.
    if (WIRE)      WIRE->disableAll();
    if (indicator) indicator->clearAll();
    stopTemperatureMonitor(); // thermalTask continues; it is cheap and safe
}
