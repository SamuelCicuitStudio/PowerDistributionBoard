#include "system/Device.h"
#include "system/Utils.h"
#include "control/RGBLed.h"
#include "control/Buzzer.h"
#include "services/SleepTimer.h"
#include <math.h>

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

static constexpr uint32_t CAL_TIMEOUT_MS = 10000; // max time for pre-loop charge + calibrations

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

    // Predict bus droop/energy using calibrated capacitance (before energizing).
    if (self->discharger && self->relayControl) {
        const float capF = self->getCapBankCapF();
        if (isfinite(capF) && capF > 0.0f) {
            float Gtot = 0.0f;
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                if (!(mask & (1u << i))) continue;
                float R = WIRE->getWireInfo(i + 1).resistanceOhm;
                if (R > 0.01f && isfinite(R)) {
                    Gtot += 1.0f / R;
                }
            }
            const float Rload = (Gtot > 0.0f) ? (1.0f / Gtot) : INFINITY;

            float vSrc = DEFAULT_DC_VOLTAGE;
            float rChg = DEFAULT_CHARGE_RESISTOR_OHMS;
            if (CONF) {
                rChg = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
            }
            if (!isfinite(vSrc) || vSrc <= 0.0f) vSrc = DEFAULT_DC_VOLTAGE;
            if (!isfinite(rChg) || rChg <= 0.0f) rChg = DEFAULT_CHARGE_RESISTOR_OHMS;

            // If input relay is open, model "no source".
            const float rChargeEff = self->relayControl->isOn() ? rChg : INFINITY;

            const float v0 = self->discharger->sampleVoltageNow();
            const float dtS = (float)onTimeMs * 0.001f;
            const float v1 = CapModel::predictVoltage(v0, dtS, capF, Rload, vSrc, rChargeEff);
            const float eJ = CapModel::energyToLoadJ(v0, dtS, capF, Rload, vSrc, rChargeEff);

            DEBUG_PRINTF("[Pulse] pre: mask=0x%03X V0=%.2fV -> V1(pred)=%.2fV  E(pred)=%.2fJ  C=%.6fF\n",
                         (unsigned)mask,
                         (double)v0,
                         (double)v1,
                         (double)eJ,
                         (double)capF);
        }
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

    // If pulse completed (no fault/STOP), log estimated V (from measured I) and per-wire currents.
    if (ok) {
        float Itot = (self->currentSensor ? self->currentSensor->readCurrent() : NAN);
        float Gtot = 0.0f;
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (!(mask & (1u << i))) continue;
            float R = WIRE->getWireInfo(i + 1).resistanceOhm;
            if (R > 0.01f && isfinite(R)) {
                Gtot += 1.0f / R;
            }
        }
        float V_est = (isfinite(Itot) && Gtot > 0.0f) ? (Itot / Gtot) : NAN;
        DEBUG_PRINTF("[Pulse] end: mask=0x%03X Vest=%.2fV I=%.3fA\n",
                     (unsigned)mask,
                     (double)V_est,
                     (double)Itot);
        if (isfinite(V_est) && V_est > 0.0f) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                uint16_t bit = (1u << i);
                if (!(mask & bit)) continue;
                WireInfo wi = WIRE->getWireInfo(i + 1);
                float R = wi.resistanceOhm;
                if (!(R > 0.01f && isfinite(R))) continue;
                float Iw = V_est / R;
                DEBUG_PRINTF("  [Pulse] OUT%u: R=%.2fΩ I=%.3fA\n",
                             (unsigned)(i + 1),
                             (double)R,
                             (double)Iw);
            }
        }
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
    BaseType_t result = xTaskCreate(
        Device::loopTaskWrapper,
        "DeviceLoopTask",
        DEVICE_LOOP_TASK_STACK_SIZE,
        this,
        DEVICE_LOOP_TASK_PRIORITY,
        &loopTaskHandle
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
    DEBUG_PRINTLN("[Device] Device loop task started");
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

        // Legacy remote start -> request WAKE+RUN
        if (StartFromremote) {
            StartFromremote = false;
            if (gEvt) xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
        }

        DEBUG_PRINTLN("[Device] State=OFF. Waiting for WAKE ...");

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
        RGB->clearActivePattern(); // clear any latched error code from previous attempt
        RGB->setWait();
        BUZZ->bip();
        DEBUG_PRINTLN("[Device] Waiting for 12V input...");

        while (!digitalRead(DETECT_12V_PIN)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        DEBUG_PRINTLN("[Device] 12V detected -> enabling relay");
        relayControl->turnOn();
        RGB->postOverlay(OverlayEvent::RELAY_ON);
        vTaskDelay(pdMS_TO_TICKS(150));
        // Ensure outputs are OFF before idling.
        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();

        checkAllowedOutputs();
        BUZZ->bipSystemReady();
        RGB->postOverlay(OverlayEvent::WAKE_FLASH);

        // ======================= IDLE STATE =======================
        setState(DeviceState::Idle);
        DEBUG_PRINTLN("[Device] State=IDLE. Waiting for RUN or STOP");
        RGB->setIdle();

        bool runRequested = false;
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_RUN_REQ) {
                xEventGroupClearBits(gEvt, EVT_RUN_REQ);
                runRequested = true;
            }
        }

        if (!runRequested) {
            if (gEvt) {
                EventBits_t got = xEventGroupWaitBits(
                    gEvt,
                    EVT_RUN_REQ | EVT_STOP_REQ,
                    pdTRUE,
                    pdFALSE,
                    portMAX_DELAY
                );

                if (got & EVT_STOP_REQ) {
                    DEBUG_PRINTLN("[Device] STOP in IDLE -> full OFF");
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                    relayControl->turnOff();
                    if (WIRE)      WIRE->disableAll();
                    if (indicator) indicator->clearAll();
                    RGB->setOff();
                    continue; // back to OFF state
                }

                if (got & EVT_RUN_REQ) {
                    runRequested = true;
                }
            }
        }

        // ===================== RUN PREP (timeouts + calibrations) =====================
        // All calibration and presence probing happen ONLY here, before StartLoop().
        RGB->clearActivePattern(); // clear any latched error code from previous attempt
        RGB->setWait();
        BUZZ->bip();

        const TickType_t prepStart = xTaskGetTickCount();
        auto timedOut = [&]() -> bool {
            return (xTaskGetTickCount() - prepStart) * portTICK_PERIOD_MS >= CAL_TIMEOUT_MS;
        };

        bool abortRun = false;
        ErrorCategory abortCat = ErrorCategory::POWER;
        uint8_t abortCode = 0;

        // Ensure a quiet, known state before any calibration.
        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();

        // 0) Current sensor zero calibration must happen first, with relay OFF (no load).
        relayControl->turnOff();
        vTaskDelay(pdMS_TO_TICKS(40));
        if (currentSensor) {
            currentSensor->calibrateZeroCurrent();
        }
        if (timedOut()) {
            DEBUG_PRINTLN("[Device] Timeout during current sensor zero calibration; aborting start");
            abortRun = true;
            abortCat = ErrorCategory::CALIB;
            abortCode = 1;
        }

        // 1) Enable relay and charge capacitors to GO threshold.
        if (!abortRun) {
            DEBUG_PRINTLN("[Device] RUN prep: enabling relay");
            relayControl->turnOn();
            RGB->postOverlay(OverlayEvent::RELAY_ON);
            vTaskDelay(pdMS_TO_TICKS(150));

            TickType_t lastChargePost = 0;
            while (discharger && discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
                if (timedOut()) {
                    DEBUG_PRINTLN("[Device] Timeout while charging caps to GO threshold; aborting start");
                    abortRun = true;
                    abortCat = ErrorCategory::POWER;
                    abortCode = 2;
                    break;
                }
                TickType_t now = xTaskGetTickCount();
                if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                    RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                    lastChargePost = now;
                }
                DEBUG_PRINTF("[Device] Charging... Cap=%.2fV Target=%.2fV\n",
                             discharger ? discharger->readCapVoltage() : -1.0f,
                             (double)GO_THRESHOLD_RATIO);
                if (!delayWithPowerWatch(200)) {
                    // STOP or 12V loss handled inside delayWithPowerWatch()
                    abortRun = true;
                    if (getState() == DeviceState::Idle) {
                        // Treat STOP as a clean cancel (no error code).
                        abortCode = 0;
                    } else {
                        abortCat = ErrorCategory::POWER;
                        abortCode = 2;
                    }
                    break;
                }
            }
        }

        // 2) Idle current calibration (relay ON, no heaters).
        if (!abortRun) {
            calibrateIdleCurrent();
            if (timedOut()) {
                DEBUG_PRINTLN("[Device] Timeout during idle current calibration; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 1;
            }
        }

        // 3) Capacitor voltage gain calibration (coarse + fine) + presence probing.
        if (!abortRun) {
            const bool ok = calibrateCapVoltageGain();
            if (!ok) {
                DEBUG_PRINTLN("[Device] Cap gain calibration failed; aborting start");
                abortRun  = true;
                abortCat  = ErrorCategory::CALIB;
                abortCode = 2;
            }
            if (timedOut()) {
                DEBUG_PRINTLN("[Device] Timeout during capacitor gain calibration; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 2;
            }
        }

        // 4) Capacitor bank capacitance calibration (timed discharge with relay OFF).
        if (!abortRun) {
            if (!calibrateCapacitance()) {
                DEBUG_PRINTLN("[Device] Capacitance calibration failed; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 3;
            } else if (timedOut()) {
                DEBUG_PRINTLN("[Device] Timeout during capacitance calibration; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 3;
            }
        }

        // 5) Recharge after discharge-based calibration so RUN starts with sane voltage.
        if (!abortRun) {
            TickType_t lastChargePost = 0;
            while (discharger && discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
                if (timedOut()) {
                    DEBUG_PRINTLN("[Device] Timeout while re-charging caps after calibration; aborting start");
                    abortRun = true;
                    abortCat = ErrorCategory::POWER;
                    abortCode = 2;
                    break;
                }
                TickType_t now = xTaskGetTickCount();
                if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                    RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                    lastChargePost = now;
                }
                if (!delayWithPowerWatch(200)) {
                    abortRun = true;
                    if (getState() == DeviceState::Idle) {
                        abortCode = 0; // STOP cancel
                    } else {
                        abortCat = ErrorCategory::POWER;
                        abortCode = 2;
                    }
                    break;
                }
            }
        }

        if (abortRun) {
            if (getState() == DeviceState::Idle && abortCode == 0) {
                DEBUG_PRINTLN("[Device] RUN prep cancelled by STOP -> returning to OFF");
                RGB->postOverlay(OverlayEvent::RELAY_OFF);
                relayControl->turnOff();
                if (WIRE)      WIRE->disableAll();
                if (indicator) indicator->clearAll();
                RGB->setOff();
                continue; // back to OFF state
            } else {
                RGB->setFault();
                if (abortCode) {
                    RGB->showError(abortCat, abortCode);
                }
                DEBUG_PRINTLN("[Device] RUN prep aborted -> returning to OFF");
                RGB->postOverlay(OverlayEvent::RELAY_OFF);
                relayControl->turnOff();
                if (WIRE)      WIRE->disableAll();
                if (indicator) indicator->clearAll();
                RGB->setOff();
                setState(DeviceState::Error);
                continue; // back to OFF state
            }
        }

        // Refresh gating after calibrations (presence + thermal + config).
        checkAllowedOutputs();

        // ======================== RUN STATE =========================
        setState(DeviceState::Running);
        DEBUG_PRINTLN("[Device] State=RUN. Entering StartLoop()");
        BUZZ->successSound();
        RGB->postOverlay(OverlayEvent::PWR_START);
        RGB->setRun();

        StartLoop(); // will block until STOP/FAULT/NO-WIRE

        // =================== CLEAN SHUTDOWN -> OFF ===================
        DEBUG_PRINTLN("[Device] StartLoop finished -> clean shutdown");
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

    manualMode = false; // auto loop enforces model updates

    // Current sensor zero calibration is performed in RUN-prep (before state=RUN).

    // 1) Thermal model ready & wires cooled.
    initWireThermalModelOnce();
    waitForWiresNearAmbient(5.0f, 0);

    // 2) Presence check disabled.

    // 3) Start continuous current & thermal integration (observers only).
    if (currentSensor && !currentSensor->isContinuousRunning()) {
        int hz = DEFAULT_AC_FREQUENCY;
        if (CONF) {
            hz = CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
        }
        if (hz < 50) hz = 50;
        if (hz > 500) hz = 500;
        const uint32_t periodMs = (hz > 0) ? std::max(2, static_cast<int>(lroundf(1000.0f / hz))) : 2;
        currentSensor->startContinuous(periodMs);
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

    float idleA = DEFAULT_IDLE_CURR;
    if (CONF) {
        idleA = CONF->GetFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);
    }
    if (idleA < 0.0f) idleA = 0.0f;

    RunSessionGuard session(this);
    session.begin(busV, idleA);

    loadRuntimeSettings();

    const uint16_t onTime      = CONF->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
    const uint16_t offTime     = CONF->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    const bool     ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    DEBUG_PRINTF("[Device] Loop config: on=%ums off=%ums mode=%s\n",
                 (unsigned)onTime,
                 (unsigned)offTime,
                 (loopModeSetting == LoopMode::Sequential ? "SEQUENTIAL"
                                                          : "ADVANCED")
    );

    const bool sequentialMode = (loopModeSetting == LoopMode::Sequential);

    if (sequentialMode) {

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
                DEBUG_PRINTLN("[Device] STOP -> exit SEQ loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
                break;
            }
        }

        // Refresh allowed.
        checkAllowedOutputs();

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

    } else {

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
                DEBUG_PRINTLN("[Device] STOP -> exit ADV loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
                break;
            }
        }

        checkAllowedOutputs();
        // Log allowed outputs with their configured resistance.
        DEBUG_PRINT("[Device] Allowed outputs:");
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (!allowedOutputs[i]) continue;
            float r = wireConfigStore.getWireResistance(i + 1);
            DEBUG_PRINTF(" OUT%u(%.2f)", (unsigned)(i + 1), (double)r);
        }
        DEBUG_PRINTLN("");

        // Decay usage scores each loop to create a moving window.
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            WireRuntimeState& ws = wireStateModel.wire(i + 1);
            ws.usageScore *= 0.9f; // decay factor
            if (ws.usageScore < 0.0f) ws.usageScore = 0.0f;
        }

        if (!wirePresenceManager.hasAnyConnected(wireStateModel)) {
            DEBUG_PRINTLN("[Device] No connected wires -> abort ADV loop");
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

        const float targetRes = wireConfigStore.getTargetResOhm();
        const uint16_t requestedMask =
            wirePlanner.chooseMask(wireConfigStore, wireStateModel, targetRes);

        WireActuator actuator;
        const uint16_t appliedMask = actuator.applyRequestedMask(
            requestedMask,
            wireConfigStore,
            wireStateModel,
            getState());

        // Debug: log planner decision each loop.
        DEBUG_PRINTF("[Planner] target=%.2f req=0x%03X applied=0x%03X outs:",
                     (double)targetRes,
                     (unsigned)requestedMask,
                     (unsigned)appliedMask);
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (appliedMask & (1u << i)) {
                DEBUG_PRINTF(" %u", (unsigned)(i + 1));
            }
        }
        DEBUG_PRINTLN("");

        if (appliedMask == 0) {
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

        if (!_runMaskedPulse(this, appliedMask, onTime, ledFeedback)) {
            if (!is12VPresent()) handle12VDrop();
            else setState(DeviceState::Idle);
            break;
        }

        // Update usage scores for wires that were just driven.
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (appliedMask & (1u << i)) {
                WireRuntimeState& ws = wireStateModel.wire(i + 1);
                ws.usageScore += 1.0f;
            }
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

    // --- Session finalize & hard-off ---
    const bool success = (getState() != DeviceState::Error);
    session.end(success);

    if (WIRE)      WIRE->disableAll();
    if (indicator) indicator->clearAll();
    stopTemperatureMonitor();
}
