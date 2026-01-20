#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>
#include <Buzzer.hpp>
#include <NtcSensor.hpp>
#include <SleepTimer.hpp>
#include <WireSafetyPolicy.hpp>
#include <WireActuator.hpp>
#include <WireScheduler.hpp>
#include <math.h>
#include <stdio.h>

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

static constexpr uint32_t PREP_CAL_TIMEOUT_MS = 10000;    // per-step timeout for calibrations
static constexpr uint32_t PREP_CHARGE_TIMEOUT_MS = 15000; // per-step timeout for cap charging
static constexpr uint32_t PREP_CHARGE_SOAK_MS = 4000;     // fixed cap soak before RUN
static constexpr uint32_t WAIT_12V_TIMEOUT_MS = 10000;

// ============================================================================
// Helper: single guarded ON pulse for a mask
// ============================================================================
//
// HARD SAFETY RULES:
//  - Only called from StartLoop() while in DeviceState::Running.
//  - Never called from ctor/begin/Idle/power-tracking/thermal code.
//  - Uses WireActuator to apply the mask once, then ALWAYS back to 0.
//  - Uses delayWithPowerWatch() for STOP/12V/OC abort.
//  - Never touches PowerTracker (separation of concerns).
// ============================================================================

struct PulseStats {
    float    busVoltageStart = NAN;
    float    busVoltage = NAN;
    float    currentA   = NAN;
    uint16_t appliedMask = 0;
};

static bool _runMaskedPulse(Device* self,
                            uint16_t mask,
                            uint32_t onTimeMs,
                            bool ledFeedback,
                            PulseStats* stats = nullptr)
{
    if (!self || !WIRE)              return true;
    if (mask == 0 || onTimeMs == 0)  return true;
    if (self->getState() != DeviceState::Running) {
        // Do not energize if not in RUN
        return false;
    }

    if (stats) {
        stats->busVoltageStart = NAN;
        stats->busVoltage = NAN;
        stats->currentA = NAN;
        stats->appliedMask = 0;
    }

    WireSafetyPolicy safety;
    WireActuator actuator;
    WireConfigStore& cfg = self->getWireConfigStore();
    WireStateModel& state = self->getWireStateModel();

    const uint16_t safeMask =
        safety.filterMask(mask, cfg, state, self->getState(), false);
    if (safeMask == 0) {
        return true;
    }

    double vStart = NAN;
    if (self->discharger) {
        vStart = self->discharger->sampleVoltageNow();
    }
    if (stats) {
        stats->busVoltageStart = static_cast<float>(vStart);
    }

    // Predict bus droop/energy using calibrated capacitance (before energizing).
    if (self->discharger && self->relayControl) {
        const float capF = self->getCapBankCapF();
        if (isfinite(capF) && capF > 0.0f) {
            double Gtot = 0.0;
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                if (!(safeMask & (1u << i))) continue;
                float R = WIRE->getWireInfo(i + 1).resistanceOhm;
                if (R > 0.01f && isfinite(R)) {
                    Gtot += 1.0 / R;
                }
            }
            const double Rload = (Gtot > 0.0) ? (1.0 / Gtot) : INFINITY;

            double vSrc = DEFAULT_DC_VOLTAGE;
            double rChg = DEFAULT_CHARGE_RESISTOR_OHMS;
            if (CONF) {
                rChg = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
            }
            if (!isfinite(vSrc) || vSrc <= 0.0) vSrc = DEFAULT_DC_VOLTAGE;
            if (!isfinite(rChg) || rChg <= 0.0) rChg = DEFAULT_CHARGE_RESISTOR_OHMS;

            // If input relay is open, model "no source".
            const double rChargeEff = self->relayControl->isOn() ? rChg : INFINITY;

            const double v0 = isfinite(vStart) ? vStart : self->discharger->sampleVoltageNow();
            if (stats) {
                stats->busVoltageStart = static_cast<float>(v0);
            }
            const double dtS = onTimeMs * 0.001;
            const double v1 = CapModel::predictVoltage(v0, dtS, capF, Rload, vSrc, rChargeEff);
            const double eJ = CapModel::energyToLoadJ(v0, dtS, capF, Rload, vSrc, rChargeEff);

            DEBUG_PRINTF("[Pulse] pre: mask=0x%03X V0=%.2fV -> V1(pred)=%.2fV  E(pred)=%.2fJ  C=%.6fF\n",
                         (unsigned)safeMask,
                         (double)v0,
                         (double)v1,
                         (double)eJ,
                         (double)capF);
        }
    }

    // Apply mask atomically via safety/actuator.
    uint16_t appliedMask =
        actuator.applyRequestedMask(safeMask, *WIRE, cfg, state, safety,
                                    self->getState(), false);
    if (stats) {
        stats->appliedMask = appliedMask;
    }
    if (appliedMask == 0) {
        return true;
    }

    // Optional LED mirror.
    if (ledFeedback && self->indicator) {
        for (uint8_t i = 0; i < 10; ++i) {
            self->indicator->setLED(i + 1, (appliedMask & (1u << i)) != 0);
        }
    }

    float pulseVSum = 0.0f;
    uint8_t pulseVSamples = 0;
    BusSampler* sampler = BUS_SAMPLER;

    int currentSource = DEFAULT_CURRENT_SOURCE;
    if (CONF) {
        currentSource = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
    }
    if (currentSource != CURRENT_SRC_ACS) {
        currentSource = CURRENT_SRC_ESTIMATE;
    }

    auto samplePulseCurrent = [&](float v) -> float {
        if (currentSource == CURRENT_SRC_ACS && self->currentSensor) {
            const float i = self->currentSensor->readCurrent();
            if (isfinite(i)) {
                return i;
            }
        }
        return WIRE->estimateCurrentFromVoltage(v, appliedMask);
    };

    auto recordPulseSample = [&](float v) {
        if (!isfinite(v)) return;
        pulseVSum += v;
        pulseVSamples++;
        if (sampler) {
            const float i = samplePulseCurrent(v);
            sampler->recordSample(millis(), v, i);
        }
    };

    uint8_t midSamples = 0;
    if (onTimeMs >= 60)  midSamples = 1;
    if (onTimeMs >= 180) midSamples = 2;
    if (onTimeMs >= 300) midSamples = 3;

    bool ok = true;
    if (midSamples == 0) {
        ok = self->delayWithPowerWatch(onTimeMs);
    } else {
        const uint32_t segmentMs = onTimeMs / (midSamples + 1);
        uint32_t remainingMs = onTimeMs;
        for (uint8_t s = 0; s < midSamples; ++s) {
            if (segmentMs > 0) {
                if (!self->delayWithPowerWatch(segmentMs)) {
                    ok = false;
                    break;
                }
                if (remainingMs > segmentMs) remainingMs -= segmentMs;
                else remainingMs = 0;
            }
            if (self->discharger) {
                recordPulseSample(self->discharger->sampleVoltageNow());
            }
        }
        if (ok && remainingMs > 0) {
            ok = self->delayWithPowerWatch(remainingMs);
        }
    }

    if (ok && self->discharger) {
        recordPulseSample(self->discharger->sampleVoltageNow());
    }

    // If pulse completed (no fault/STOP), log estimated V and per-wire currents.
    if (ok) {
        float V_bus = NAN;
        if (pulseVSamples > 0) {
            V_bus = pulseVSum / static_cast<float>(pulseVSamples);
        } else if (self->discharger) {
            V_bus = self->discharger->sampleVoltageNow();
        }
        const float Itot = (WIRE ? samplePulseCurrent(V_bus) : NAN);
        if (stats) {
            stats->busVoltage = V_bus;
            stats->currentA = Itot;
        }
        DEBUG_PRINTF("[Pulse] end: mask=0x%03X Vbus=%.2fV Iest=%.3fA\n",
                     (unsigned)appliedMask,
                     (double)V_bus,
                     (double)Itot);
        if (isfinite(V_bus) && V_bus > 0.0f) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                uint16_t bit = (1u << i);
                if (!(appliedMask & bit)) continue;
                WireInfo wi = WIRE->getWireInfo(i + 1);
                float R = wi.resistanceOhm;
                if (!(R > 0.01f && isfinite(R))) continue;
                float Iw = V_bus / R;
                DEBUG_PRINTF("  [Pulse] OUT%u: R=%.2fIc I=%.3fA\n",
                             (unsigned)(i + 1),
                             (double)R,
                             (double)Iw);
            }
        }
    }

    // ALWAYS ensure outputs are OFF (success or abort).
    actuator.applyRequestedMask(0, *WIRE, cfg, state, safety,
                                self->getState(), false);
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

        const uint32_t wait12vStart = millis();
        bool wait12vTimedOut = false;
        while (!digitalRead(DETECT_12V_PIN)) {
            if ((millis() - wait12vStart) >= WAIT_12V_TIMEOUT_MS) {
                wait12vTimedOut = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (wait12vTimedOut) {
            DEBUG_PRINTLN("[Device] 12V not detected within timeout");
            setLastErrorReason("12V not detected within 10s of start");
            RGB->setFault();
            RGB->showError(ErrorCategory::POWER, 2);
            BUZZ->bipFault();
            if (WIRE)      WIRE->disableAll();
            if (indicator) indicator->clearAll();
            relayControl->turnOff();
            setState(DeviceState::Error);
            continue; // back to OFF state
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
                    setLastStopReason("Stop requested");
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

        auto stepTimedOut = [&](TickType_t stepStart, uint32_t timeoutMs) -> bool {
            return (xTaskGetTickCount() - stepStart) * portTICK_PERIOD_MS >= timeoutMs;
        };

        bool abortRun = false;
        ErrorCategory abortCat = ErrorCategory::POWER;
        uint8_t abortCode = 0;

        // Ensure a quiet, known state before any calibration.
        if (WIRE)      WIRE->disableAll();
        if (indicator) indicator->clearAll();

        TickType_t stepStart = 0;

        // 1) Enable relay and charge capacitors to GO threshold.
        if (!abortRun) {
            DEBUG_PRINTLN("[Device] RUN prep: enabling relay");
            TickType_t chargeStart = xTaskGetTickCount();
            relayControl->turnOn();
            RGB->postOverlay(OverlayEvent::RELAY_ON);
            vTaskDelay(pdMS_TO_TICKS(150));

            TickType_t lastChargePost = 0;
            while (discharger && discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
                if (stepTimedOut(chargeStart, PREP_CHARGE_TIMEOUT_MS)) {
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
                    if (getState() == DeviceState::Shutdown) {
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

        // 2) Capacitor bank capacitance calibration (timed discharge with relay OFF).
        if (!abortRun) {
            stepStart = xTaskGetTickCount();
            if (!calibrateCapacitance()) {
                DEBUG_PRINTLN("[Device] Capacitance calibration failed; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 3;
            } else if (stepTimedOut(stepStart, PREP_CAL_TIMEOUT_MS)) {
                DEBUG_PRINTLN("[Device] Timeout during capacitance calibration; aborting start");
                abortRun = true;
                abortCat = ErrorCategory::CALIB;
                abortCode = 3;
            }
        }

        // 3) Recharge after discharge-based calibration so RUN starts with sane voltage.
        if (!abortRun) {
            TickType_t chargeStart = xTaskGetTickCount();
            TickType_t lastChargePost = 0;
            while (discharger && discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
                if (stepTimedOut(chargeStart, PREP_CHARGE_TIMEOUT_MS)) {
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
                    if (getState() == DeviceState::Shutdown) {
                        abortCode = 0; // STOP cancel
                    } else {
                        abortCat = ErrorCategory::POWER;
                        abortCode = 2;
                    }
                    break;
                }
            }
        }

        // 4) Final cap soak before RUN (fixed pre-charge dwell).
        if (!abortRun) {
            DEBUG_PRINTLN("[Device] RUN prep: cap soak 4s");
            if (!delayWithPowerWatch(PREP_CHARGE_SOAK_MS)) {
                abortRun = true;
                if (getState() == DeviceState::Shutdown) {
                    abortCode = 0; // STOP cancel
                } else {
                    abortCat = ErrorCategory::POWER;
                    abortCode = 2;
                }
            }
        }

        if (abortRun) {
            if (getState() == DeviceState::Shutdown && abortCode == 0) {
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
                char reason[96] = {0};
                if (abortCode) {
                    snprintf(reason, sizeof(reason),
                             "Run prep aborted (cat=%u code=%u)",
                             static_cast<unsigned>(abortCat),
                             static_cast<unsigned>(abortCode));
                    setLastErrorReason(reason);
                } else {
                    setLastErrorReason("Run preparation aborted");
                }
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
        if (getState() != DeviceState::Error) {
            setState(DeviceState::Shutdown);
        }

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
        POWER_TRACKER->update();
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
// StartLoop(): main heating behavior (fast warm-up + equilibrium)
// ============================================================================

void Device::StartLoop() {
    if (getState() != DeviceState::Running) {
        return;
    }

    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] StartLoop: entering main heating loop");
    DEBUG_PRINTLN("-----------------------------------------------------------");

    // Current sensor is not used for power calculations during RUN.

    // 1) Thermal model ready & wires cooled (skip for wire test).
    initWireThermalModelOnce();
    EnergyRunPurpose waitPurpose = EnergyRunPurpose::None;
    if (controlMtx &&
        xSemaphoreTake(controlMtx, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        waitPurpose = wireTargetStatus.active ? wireTargetStatus.purpose
                                              : EnergyRunPurpose::None;
        xSemaphoreGive(controlMtx);
    } else {
        waitPurpose = wireTargetStatus.active ? wireTargetStatus.purpose
                                              : EnergyRunPurpose::None;
    }
    const bool shouldWait =
        (waitPurpose == EnergyRunPurpose::None) ||
        (waitPurpose == EnergyRunPurpose::NtcCal) ||
        (waitPurpose == EnergyRunPurpose::FloorCal);
    const bool coolConfirmed = shouldWait ? consumeWiresCoolConfirmation() : false;
    if (shouldWait && !coolConfirmed) {
        const char* waitReason =
            (waitPurpose == EnergyRunPurpose::ModelCal) ? "model_cal" :
            (waitPurpose == EnergyRunPurpose::NtcCal)   ? "ntc_cal"   :
            (waitPurpose == EnergyRunPurpose::FloorCal) ? "floor_cal" :
                                                        "run";
        if (WIRE) {
            const uint32_t nowMs = millis();
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                wireThermalModel.applyExternalWireTemp(
                    static_cast<uint8_t>(i + 1),
                    WIRE_T_MAX_C,
                    nowMs,
                    wireStateModel,
                    *WIRE);
            }
        }
        waitForWiresNearAmbient(5.0f, 0, waitReason);
    } else {
        setAmbientWaitStatus(false, 0.0f, coolConfirmed ? "confirmed" : "none");
    }

    // 2) Presence runtime checks (reset counters at run start).
    wirePresenceManager.resetFailures();

    // 3) Start thermal integration (observers only).
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

    float idleA = 0.0f;

    RunSessionGuard session(this);
    session.begin(busV, idleA);

    loadRuntimeSettings();

    const bool ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    DEBUG_PRINTLN("[Device] Mode: ENERGY");

    // ====================== ENERGY MODE ======================
    //
    // Energy packets serialized inside a fixed frame:
    //  - Total ON time derives from control error (floor NTC or wire target).
    //  - WireScheduler distributes ON time across eligible wires.
    // ========================================================

    int frameI   = 120;
    float holdGain = 0.6f;
    int minOnI   = 60;
    int maxOnI   = 900;
    int maxAvgI  = 1200;

    if (frameI < 10) frameI = 10;
    if (frameI > 300) frameI = 300;
    if (holdGain < 0.0f) holdGain = 0.0f;
    if (holdGain > 5.0f) holdGain = 5.0f;
    if (minOnI < 0) minOnI = 0;
    if (minOnI > frameI) minOnI = frameI;
    if (maxOnI < 1) maxOnI = 1;
    if (maxOnI > 1000) maxOnI = 1000;
    if (maxAvgI < 0) maxAvgI = 0;
    if (maxAvgI > 1000) maxAvgI = 1000;

    const float frameMs = static_cast<float>(frameI);
    const float minOnMs = static_cast<float>(minOnI);
    const float maxOnMs = static_cast<float>(maxOnI);
    const float maxAvgPerFrame = (maxAvgI > 0)
                                     ? (static_cast<float>(maxAvgI) * frameMs / 1000.0f)
                                     : frameMs;

    float wireMaxC = WIRE_T_MAX_C;
    if (CONF) {
        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                 DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(v) && v > 0.0f) wireMaxC = v;
    }
    if (wireMaxC > WIRE_T_MAX_C) wireMaxC = WIRE_T_MAX_C;
    if (wireMaxC < 0.0f) wireMaxC = 0.0f;

    float floorSwitchMarginC = DEFAULT_FLOOR_SWITCH_MARGIN_C;
    if (CONF) {
        float v = CONF->GetFloat(FLOOR_SWITCH_MARGIN_C_KEY,
                                 DEFAULT_FLOOR_SWITCH_MARGIN_C);
        if (isfinite(v) && v > 0.0f) floorSwitchMarginC = v;
    }
    if (!isfinite(floorSwitchMarginC) || floorSwitchMarginC <= 0.0f) {
        floorSwitchMarginC = DEFAULT_FLOOR_SWITCH_MARGIN_C;
    }

    double floorTau = DEFAULT_FLOOR_MODEL_TAU;
    double floorK = DEFAULT_FLOOR_MODEL_K;
    double floorC = DEFAULT_FLOOR_MODEL_C;
    if (CONF) {
        floorTau = CONF->GetDouble(FLOOR_MODEL_TAU_KEY, DEFAULT_FLOOR_MODEL_TAU);
        floorK = CONF->GetDouble(FLOOR_MODEL_K_KEY, DEFAULT_FLOOR_MODEL_K);
        floorC = CONF->GetDouble(FLOOR_MODEL_C_KEY, DEFAULT_FLOOR_MODEL_C);
    }
    if (!isfinite(floorK) || floorK <= 0.0) {
        floorK = DEFAULT_FLOOR_MODEL_K;
    }
    if (!isfinite(floorC) || floorC < 0.0) {
        floorC = DEFAULT_FLOOR_MODEL_C;
    }
    if (!isfinite(floorTau) || floorTau <= 0.0) {
        if (isfinite(floorK) && floorK > 0.0 && isfinite(floorC) && floorC > 0.0) {
            floorTau = floorC / floorK;
        }
    }
    const bool floorModelValid = isfinite(floorTau) && floorTau > 0.0 &&
                                 isfinite(floorK) && floorK > 0.0;

    WireScheduler scheduler;
    WirePacket packets[HeaterManager::kWireCount]{};
    auto sumOnOverR = [&](const WirePacket* list, size_t count) -> double {
        if (!list || count == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            const WirePacket& pkt = list[i];
            if (pkt.onMs == 0 || pkt.mask == 0) continue;
            for (uint8_t b = 0; b < HeaterManager::kWireCount; ++b) {
                if (!(pkt.mask & (1u << b))) continue;
                float r = wireConfigStore.getWireResistance(b + 1);
                if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;
                sum += static_cast<double>(pkt.onMs) / static_cast<double>(r);
            }
        }
        return sum;
    };

    auto predictFloorNext = [&](double sumOnOverR,
                                float busV,
                                float roomC,
                                float tNowC) -> double {
        if (!(sumOnOverR > 0.0)) return NAN;
        if (!isfinite(busV) || busV <= 0.0f) return NAN;
        if (!isfinite(roomC) || !isfinite(tNowC)) return NAN;
        if (!(floorTau > 0.0) || !(floorK > 0.0)) return NAN;
        const double dtS = static_cast<double>(frameMs) * 0.001;
        if (dtS <= 0.0) return NAN;
        const double decay = exp(-dtS / floorTau);
        const double pAvg =
            (static_cast<double>(busV) * static_cast<double>(busV) * sumOnOverR) /
            static_cast<double>(frameMs);
        const double tRoom = static_cast<double>(roomC);
        const double tNow = static_cast<double>(tNowC);
        return tRoom + (tNow - tRoom) * decay + (pAvg / floorK) * (1.0 - decay);
    };
    bool targetedMode = false;
    {
        const WireTargetStatus wt = getWireTargetStatus();
        targetedMode = wt.active && (wt.purpose != EnergyRunPurpose::None);
    }

    while (getState() == DeviceState::Running) {
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP -> exit ENERGY loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setLastStopReason("Stop requested");
                setState(DeviceState::Shutdown);
                break;
            }
        }

        checkAllowedOutputs();

        const WireTargetStatus wt = getWireTargetStatus();
        if (targetedMode && !wt.active) {
            DEBUG_PRINTLN("[Device] Targeted run stopped -> exit loop");
            setLastStopReason("Targeted run stopped");
            setState(DeviceState::Shutdown);
            break;
        }
        if (targetedMode &&
            wt.active &&
            wt.purpose == EnergyRunPurpose::ModelCal) {
            const uint8_t activeWire = wt.activeWire;
            if (activeWire > 0) {
                const WireRuntimeState& ws = wireStateModel.wire(activeWire);
                if (!ws.present) {
                    stopWireTargetTest();
                    setLastStopReason("Wire not present");
                    break;
                }
            }
        }

        const FloorControlStatus fc = getFloorControlStatus();
        float targetC = NAN;
        float controlTempC = NAN;
        EnergyRunPurpose runPurpose = wt.purpose;
        float fixedDutyFrac = 1.0f;

        if (targetedMode) {
            targetC = wt.targetC;
            if (NTC) {
                controlTempC = NTC->getLastTempC();
            }
            if (isfinite(targetC) && targetC > wireMaxC) targetC = wireMaxC;
            if (runPurpose == EnergyRunPurpose::FloorCal) {
                fixedDutyFrac = wt.dutyFrac;
                if (!isfinite(fixedDutyFrac) || fixedDutyFrac <= 0.0f) {
                    fixedDutyFrac = 1.0f;
                }
                if (fixedDutyFrac > 1.0f) fixedDutyFrac = 1.0f;
            }
        } else {
            if (fc.active && isfinite(fc.targetC)) {
                targetC = fc.targetC;
            }
            if (NTC) {
                controlTempC = NTC->getLastTempC();
            }
        }

        if (targetedMode &&
            (runPurpose == EnergyRunPurpose::ModelCal ||
             runPurpose == EnergyRunPurpose::FloorCal) &&
            isfinite(targetC) &&
            isfinite(controlTempC) &&
            controlTempC >= targetC)
        {
            stopWireTargetTest();
            break;
        }

        if (!isfinite(targetC) || targetC <= 0.0f) {
            if (targetedMode) {
                setLastStopReason("Target temp invalid");
            } else {
                setLastStopReason("Floor target unset");
            }
            setState(DeviceState::Shutdown);
            break;
        }

        if (!isfinite(controlTempC)) {
            setLastErrorReason(targetedMode ? "NTC invalid"
                                            : "Floor NTC invalid");
            setState(DeviceState::Error);
            break;
        }

        float errorC = targetC - controlTempC;
        if (errorC < 0.0f) errorC = 0.0f;

        const float guardC = targetC - floorSwitchMarginC;
        float roomC = NAN;
        float busV = NAN;
        if (!targetedMode && floorModelValid) {
            roomC = ambientC;
            if (!isfinite(roomC) && tempSensor) {
                roomC = tempSensor->getHeatsinkTemp();
            }
            if (!isfinite(roomC)) {
                roomC = controlTempC;
            }
            if (discharger) {
                busV = discharger->sampleVoltageNow();
            }
        }

        const bool fixedDuty =
            targetedMode && (runPurpose == EnergyRunPurpose::ModelCal ||
                             runPurpose == EnergyRunPurpose::FloorCal);
        bool boostActive = fixedDuty || (errorC > floorSwitchMarginC);

        if (!fixedDuty && boostActive && !targetedMode && floorModelValid &&
            isfinite(guardC) && isfinite(roomC) && isfinite(busV) && busV > 0.0f) {
            float boostBudgetMs = frameMs;
            if (maxAvgPerFrame < boostBudgetMs) boostBudgetMs = maxAvgPerFrame;
            if (boostBudgetMs > 0.0f) {
                const uint16_t boostOnMs =
                    static_cast<uint16_t>(lroundf(boostBudgetMs));
                if (boostOnMs > 0) {
                    WirePacket probePackets[HeaterManager::kWireCount]{};
                    const size_t probeCount = scheduler.buildSchedule(
                        wireConfigStore,
                        wireStateModel,
                        static_cast<uint16_t>(frameI),
                        boostOnMs,
                        wireMaxC,
                        static_cast<uint16_t>(minOnMs),
                        static_cast<uint16_t>(maxOnMs),
                        probePackets,
                        HeaterManager::kWireCount);
                    const double sumOnR = sumOnOverR(probePackets, probeCount);
                    const double nextT = predictFloorNext(sumOnR, busV, roomC, controlTempC);
                    if (isfinite(nextT) && nextT > static_cast<double>(guardC)) {
                        boostActive = false;
                    }
                }
            }
        }

        float demandMs = 0.0f;
        if (errorC > 0.0f) {
            if (fixedDuty) {
                demandMs = frameMs * fixedDutyFrac;
            } else if (boostActive) {
                demandMs = frameMs;
            } else {
                const float denom =
                    (floorSwitchMarginC > 0.1f) ? floorSwitchMarginC : 0.1f;
                float frac = errorC / denom;
                if (frac > 1.0f) frac = 1.0f;
                demandMs = frameMs * frac * holdGain;
            }
        }

        if (demandMs > maxAvgPerFrame) demandMs = maxAvgPerFrame;
        if (demandMs > frameMs) demandMs = frameMs;
        if (demandMs < 0.0f) demandMs = 0.0f;

        uint16_t totalOnMs = static_cast<uint16_t>(lroundf(demandMs));
        size_t packetCount = 0;
        if (totalOnMs > 0) {
            packetCount = scheduler.buildSchedule(
                wireConfigStore,
                wireStateModel,
                static_cast<uint16_t>(frameI),
                totalOnMs,
                wireMaxC,
                static_cast<uint16_t>(minOnMs),
                static_cast<uint16_t>(maxOnMs),
                packets,
                HeaterManager::kWireCount);
        }

        if (!targetedMode && floorModelValid && packetCount > 0 &&
            isfinite(guardC) && isfinite(roomC) && isfinite(busV) && busV > 0.0f) {
            const double sumOnR = sumOnOverR(packets, packetCount);
            const double nextT = predictFloorNext(sumOnR, busV, roomC, controlTempC);
            if (isfinite(nextT) && nextT > static_cast<double>(guardC)) {
                const double dtS = static_cast<double>(frameMs) * 0.001;
                const double decay = (dtS > 0.0 && floorTau > 0.0)
                                        ? exp(-dtS / floorTau)
                                        : 0.0;
                const double denom = 1.0 - decay;
                if (denom > 0.0) {
                    const double tRoom = static_cast<double>(roomC);
                    const double tNow = static_cast<double>(controlTempC);
                    double pReq = floorK * ((static_cast<double>(guardC) - tRoom) -
                                           (tNow - tRoom) * decay) / denom;
                    if (pReq < 0.0) pReq = 0.0;
                    const double pAvg =
                        (static_cast<double>(busV) * static_cast<double>(busV) * sumOnR) /
                        static_cast<double>(frameMs);
                    const double perMs = pAvg / static_cast<double>(totalOnMs);
                    if (perMs > 0.0) {
                        uint16_t newTotal =
                            static_cast<uint16_t>(floor(pReq / perMs));
                        if (newTotal < totalOnMs) {
                            totalOnMs = newTotal;
                            if (totalOnMs > static_cast<uint16_t>(frameI)) {
                                totalOnMs = static_cast<uint16_t>(frameI);
                            }
                            packetCount = 0;
                            if (totalOnMs > 0) {
                                packetCount = scheduler.buildSchedule(
                                    wireConfigStore,
                                    wireStateModel,
                                    static_cast<uint16_t>(frameI),
                                    totalOnMs,
                                    wireMaxC,
                                    static_cast<uint16_t>(minOnMs),
                                    static_cast<uint16_t>(maxOnMs),
                                    packets,
                                    HeaterManager::kWireCount);
                            }
                        }
                    }
                }
            }
        }

        if (packetCount == 0) {
            if (!delayWithPowerWatch(static_cast<uint32_t>(frameMs))) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Shutdown);
                }
                break;
            }
            session.tick();
            continue;
        }

        const uint32_t frameStartMs = millis();
        bool abortMixed = false;
        bool reevalAllowed = false;

        for (size_t oi = 0; oi < packetCount; ++oi) {
            const WirePacket& pkt = packets[oi];
            if (pkt.onMs == 0 || pkt.mask == 0) continue;

            if (targetedMode) {
                uint8_t wireIndex = 0;
                for (uint8_t b = 0; b < HeaterManager::kWireCount; ++b) {
                    if (pkt.mask & (1u << b)) {
                        wireIndex = static_cast<uint8_t>(b + 1);
                        break;
                    }
                }
                if (wireIndex > 0) {
                    updateWireTestStatus(wireIndex,
                                         pkt.onMs,
                                         static_cast<uint32_t>(frameMs));
                }
            }

            PulseStats pulseStats{};
            if (!_runMaskedPulse(this, pkt.mask, pkt.onMs, ledFeedback, &pulseStats)) {
                if (!is12VPresent()) handle12VDrop();
                else setState(DeviceState::Shutdown);
                abortMixed = true;
                break;
            }
            if (pulseStats.appliedMask != 0) {
                const bool changed =
                    wirePresenceManager.updatePresenceFromMask(
                        *WIRE,
                        wireStateModel,
                        pulseStats.appliedMask,
                        pulseStats.busVoltageStart,
                        pulseStats.busVoltage);
                if (changed) {
                    checkAllowedOutputs();
                    if (!wirePresenceManager.hasAnyConnected(wireStateModel)) {
                        setLastStopReason("No wires present");
                        setState(DeviceState::Shutdown);
                        abortMixed = true;
                        break;
                    }
                    reevalAllowed = true;
                    break;
                }
            }
            session.tick();
        }

        if (abortMixed) break;
        if (reevalAllowed) {
            continue;
        }

        const uint32_t elapsed = millis() - frameStartMs;
        if (elapsed < static_cast<uint32_t>(frameMs)) {
            if (!delayWithPowerWatch(static_cast<uint32_t>(frameMs) - elapsed)) {
                if (!is12VPresent()) handle12VDrop();
                else {
                    xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    setState(DeviceState::Shutdown);
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
