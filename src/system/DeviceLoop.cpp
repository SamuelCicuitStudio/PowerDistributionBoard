#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>
#include <Buzzer.hpp>
#include <SleepTimer.hpp>
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
            double Gtot = 0.0;
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                if (!(mask & (1u << i))) continue;
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

            const double v0 = self->discharger->sampleVoltageNow();
            const double dtS = onTimeMs * 0.001;
            const double v1 = CapModel::predictVoltage(v0, dtS, capF, Rload, vSrc, rChargeEff);
            const double eJ = CapModel::energyToLoadJ(v0, dtS, capF, Rload, vSrc, rChargeEff);

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

    float pulseVSum = 0.0f;
    uint8_t pulseVSamples = 0;
    BusSampler* sampler = BUS_SAMPLER;

    auto recordPulseSample = [&](float v) {
        if (!isfinite(v)) return;
        pulseVSum += v;
        pulseVSamples++;
        if (sampler) {
            const float i = WIRE->estimateCurrentFromVoltage(v, mask);
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
        const float Itot = (WIRE ? WIRE->estimateCurrentFromVoltage(V_bus, mask) : NAN);
        DEBUG_PRINTF("[Pulse] end: mask=0x%03X Vbus=%.2fV Iest=%.3fA\n",
                     (unsigned)mask,
                     (double)V_bus,
                     (double)Itot);
        if (isfinite(V_bus) && V_bus > 0.0f) {
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                uint16_t bit = (1u << i);
                if (!(mask & bit)) continue;
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
// StartLoop(): main heating behavior (energy-based sequential)
// ============================================================================

void Device::StartLoop() {
    if (getState() != DeviceState::Running) {
        return;
    }

    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] StartLoop: entering main heating loop");
    DEBUG_PRINTLN("-----------------------------------------------------------");

    manualMode = false; // auto loop enforces model updates

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
        (waitPurpose == EnergyRunPurpose::ModelCal) ||
        (waitPurpose == EnergyRunPurpose::NtcCal);
    if (shouldWait) {
        const char* waitReason =
            (waitPurpose == EnergyRunPurpose::ModelCal) ? "model_cal" :
            (waitPurpose == EnergyRunPurpose::NtcCal)   ? "ntc_cal"   :
                                                        "run";
        waitForWiresNearAmbient(5.0f, 0, waitReason);
    } else {
        setAmbientWaitStatus(false, 0.0f, "none");
    }

    // 2) Presence check disabled.

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

    float idleA = DEFAULT_IDLE_CURR;
    if (CONF) {
        idleA = CONF->GetFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);
    }
    if (idleA < 0.0f) idleA = 0.0f;

    RunSessionGuard session(this);
    session.begin(busV, idleA);

    loadRuntimeSettings();

    const bool ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    DEBUG_PRINTLN("[Device] Mode: ENERGY");

    // ====================== ENERGY MODE ======================
    //
    // Energy packets serialized inside a fixed frame:
    //  - Each allowed wire gets one packet per frame.
    //  - Packet size is normalized by resistance and adjusted by temperature error.
    //  - Global boost phase breaks plateaus, then a hold phase maintains Tset.
    // ========================================================

    int frameI       = CONF ? CONF->GetInt(MIX_FRAME_MS_KEY, DEFAULT_MIX_FRAME_MS) : DEFAULT_MIX_FRAME_MS;
    int refOnI       = CONF ? CONF->GetInt(MIX_REF_ON_MS_KEY, DEFAULT_MIX_REF_ON_MS) : DEFAULT_MIX_REF_ON_MS;
    float refRes     = CONF ? CONF->GetFloat(MIX_REF_RES_OHM_KEY, DEFAULT_MIX_REF_RES_OHM) : DEFAULT_MIX_REF_RES_OHM;
    float boostK     = CONF ? CONF->GetFloat(MIX_BOOST_K_KEY, DEFAULT_MIX_BOOST_K) : DEFAULT_MIX_BOOST_K;
    int boostMsI     = CONF ? CONF->GetInt(MIX_BOOST_MS_KEY, DEFAULT_MIX_BOOST_MS) : DEFAULT_MIX_BOOST_MS;
    float preDeltaC  = CONF ? CONF->GetFloat(MIX_PRE_DELTA_C_KEY, DEFAULT_MIX_PRE_DELTA_C) : DEFAULT_MIX_PRE_DELTA_C;
    int holdUpdateI  = CONF ? CONF->GetInt(MIX_HOLD_UPDATE_MS_KEY, DEFAULT_MIX_HOLD_UPDATE_MS) : DEFAULT_MIX_HOLD_UPDATE_MS;
    float holdGain   = CONF ? CONF->GetFloat(MIX_HOLD_GAIN_KEY, DEFAULT_MIX_HOLD_GAIN) : DEFAULT_MIX_HOLD_GAIN;
    int minOnI       = CONF ? CONF->GetInt(MIX_MIN_ON_MS_KEY, DEFAULT_MIX_MIN_ON_MS) : DEFAULT_MIX_MIN_ON_MS;
    int maxOnI       = CONF ? CONF->GetInt(MIX_MAX_ON_MS_KEY, DEFAULT_MIX_MAX_ON_MS) : DEFAULT_MIX_MAX_ON_MS;
    int maxAvgI      = CONF ? CONF->GetInt(MIX_MAX_AVG_MS_KEY, DEFAULT_MIX_MAX_AVG_MS) : DEFAULT_MIX_MAX_AVG_MS;

    if (frameI < 10) frameI = 10;
    if (frameI > 300) frameI = 300;
    if (refOnI < 1) refOnI = 1;
    if (refOnI > frameI) refOnI = frameI;
    if (!isfinite(refRes) || refRes <= 0.0f) refRes = DEFAULT_MIX_REF_RES_OHM;
    if (!isfinite(boostK) || boostK <= 0.0f) boostK = DEFAULT_MIX_BOOST_K;
    if (boostK > 5.0f) boostK = 5.0f;
    if (boostMsI < 0) boostMsI = 0;
    if (boostMsI > 600000) boostMsI = 600000;
    if (!isfinite(preDeltaC) || preDeltaC < 0.0f) preDeltaC = DEFAULT_MIX_PRE_DELTA_C;
    if (preDeltaC > 30.0f) preDeltaC = 30.0f;
    if (holdUpdateI < 200) holdUpdateI = 200;
    if (holdUpdateI > 5000) holdUpdateI = 5000;
    if (!isfinite(holdGain) || holdGain < 0.0f) holdGain = DEFAULT_MIX_HOLD_GAIN;
    if (holdGain > 5.0f) holdGain = 5.0f;
    if (minOnI < 0) minOnI = 0;
    if (minOnI > frameI) minOnI = frameI;
    if (maxOnI < 1) maxOnI = 1;
    if (maxOnI > 1000) maxOnI = 1000;
    if (maxAvgI < 0) maxAvgI = 0;
    if (maxAvgI > 1000) maxAvgI = 1000;

    const float frameMs = static_cast<float>(frameI);
    const float refOnMs = static_cast<float>(refOnI);
    const uint32_t boostMs = static_cast<uint32_t>(boostMsI);
    const uint32_t holdUpdateMs = static_cast<uint32_t>(holdUpdateI);
    const float minOnMs = static_cast<float>(minOnI);
    const float maxOnMs = static_cast<float>(maxOnI);

    float targetC = WIRE_T_MAX_C;
    float defaultTargetC = WIRE_T_MAX_C;
    EnergyRunPurpose targetPurpose = EnergyRunPurpose::None;
    uint8_t targetWire = 0;
    if (CONF) {
        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                 DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(v) && v > 0.0f) defaultTargetC = v;
    }
    targetC = defaultTargetC;
    if (controlMtx &&
        xSemaphoreTake(controlMtx, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (wireTargetStatus.active && isfinite(wireTargetStatus.targetC)) {
            targetC = wireTargetStatus.targetC;
            targetPurpose = wireTargetStatus.purpose;
            targetWire = wireTargetStatus.activeWire;
        } else if (floorControlStatus.active &&
                   isfinite(floorControlStatus.wireTargetC)) {
            targetC = floorControlStatus.wireTargetC;
        }
        xSemaphoreGive(controlMtx);
    }
    if (targetC > WIRE_T_MAX_C) targetC = WIRE_T_MAX_C;
    if (targetC < 0.0f) targetC = 0.0f;
    const float boostExitC = targetC - preDeltaC;

    bool targetedRun = false;
    if (controlMtx &&
        xSemaphoreTake(controlMtx, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        targetedRun = wireTargetStatus.active &&
                      (wireTargetStatus.purpose != EnergyRunPurpose::None);
        xSemaphoreGive(controlMtx);
    } else {
        targetedRun = wireTargetStatus.active &&
                      (wireTargetStatus.purpose != EnergyRunPurpose::None);
    }

    float holdMs[HeaterManager::kWireCount] = {0.0f};
    bool  holdInit = false;
    uint32_t lastHoldUpdate = 0;
    uint8_t rotateOffset = 0;
    const uint32_t boostDeadline = millis() + boostMs;

    while (getState() == DeviceState::Running) {
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP -> exit MIXED loop");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setLastStopReason("Stop requested");
                setState(DeviceState::Shutdown);
                break;
            }
        }

        if (targetedRun) {
            bool active = false;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                active = wireTargetStatus.active;
                xSemaphoreGive(controlMtx);
            } else {
                active = wireTargetStatus.active;
            }
            if (!active) {
                DEBUG_PRINTLN("[Device] Targeted run stopped -> exit MIXED loop");
                setLastStopReason("Targeted run stopped");
                setState(DeviceState::Shutdown);
                break;
            }
        }

        checkAllowedOutputs();

        uint8_t allowedIdx[HeaterManager::kWireCount];
        float   baseMs[HeaterManager::kWireCount] = {0.0f};
        float   tempC[HeaterManager::kWireCount] = {0.0f};
        size_t  allowedCount = 0;
        bool    anyAtTarget = false;

        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (!allowedOutputs[i]) continue;
            WireInfo wi = WIRE->getWireInfo(i + 1);
            if (!wi.connected) continue;
            float t = WIRE->getWireEstimatedTemp(i + 1);
            if (!isfinite(t)) continue;
            tempC[i] = t;
            if (t >= boostExitC) {
                anyAtTarget = true;
            }
            float r = wi.resistanceOhm;
            if (!isfinite(r) || r <= 0.0f) r = refRes;
            float base = refOnMs * (r / refRes);
            if (!isfinite(base) || base <= 0.0f) base = refOnMs;
            baseMs[i] = base;
            allowedIdx[allowedCount++] = i;
        }

        if (targetedRun && isfinite(targetC) && targetC > 0.0f)
        {
            EnergyRunPurpose runPurpose = targetPurpose;
            uint8_t runWire = targetWire;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                if (wireTargetStatus.active) {
                    runPurpose = wireTargetStatus.purpose;
                    runWire = wireTargetStatus.activeWire;
                }
                xSemaphoreGive(controlMtx);
            }

            if (runPurpose == EnergyRunPurpose::ModelCal) {
                bool reached = false;
                if (runWire >= 1 && runWire <= HeaterManager::kWireCount) {
                    const float t = tempC[runWire - 1];
                    reached = isfinite(t) && t >= targetC;
                } else {
                    for (size_t i = 0; i < allowedCount; ++i) {
                        const uint8_t idx = allowedIdx[i];
                        const float t = tempC[idx];
                        if (isfinite(t) && t >= targetC) {
                            reached = true;
                            break;
                        }
                    }
                }
                if (reached) {
                    stopWireTargetTest();
                    break;
                }
            }
        }

        if (allowedCount == 0) {
            if (!delayWithPowerWatch(100)) {
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

        const uint32_t nowMs = millis();
        const bool boostActive = (boostMs > 0) &&
                                 ((int32_t)(boostDeadline - nowMs) > 0) &&
                                 (!anyAtTarget);

        if (!boostActive) {
            if (!holdInit || (nowMs - lastHoldUpdate) >= holdUpdateMs) {
                lastHoldUpdate = nowMs;
                holdInit = true;
                for (size_t i = 0; i < allowedCount; ++i) {
                    const uint8_t idx = allowedIdx[i];
                    const float err = targetC - tempC[idx];
                    float t = baseMs[idx] + (holdGain * err);
                    if (!isfinite(t)) t = baseMs[idx];
                    holdMs[idx] = t;
                }
            }
        }

        float packetMs[HeaterManager::kWireCount] = {0.0f};
        float sumMs = 0.0f;
        float minTotal = minOnMs * static_cast<float>(allowedCount);
        const float maxAvgPerFrame = (maxAvgI > 0)
                                         ? (static_cast<float>(maxAvgI) * frameMs / 1000.0f)
                                         : maxOnMs;
        const float hardMax = (maxAvgPerFrame < maxOnMs) ? maxAvgPerFrame : maxOnMs;

        for (size_t i = 0; i < allowedCount; ++i) {
            const uint8_t idx = allowedIdx[i];
            float t = boostActive ? (baseMs[idx] * boostK) : holdMs[idx];
            if (!isfinite(t)) t = baseMs[idx];
            if (t < minOnMs) t = minOnMs;
            if (t > hardMax) t = hardMax;
            if (t > frameMs) t = frameMs;
            packetMs[idx] = t;
            sumMs += t;
        }

        if (minTotal > frameMs) {
            const float each = frameMs / static_cast<float>(allowedCount);
            for (size_t i = 0; i < allowedCount; ++i) {
                packetMs[allowedIdx[i]] = each;
            }
        } else if (sumMs > frameMs) {
            float extraSum = 0.0f;
            for (size_t i = 0; i < allowedCount; ++i) {
                const uint8_t idx = allowedIdx[i];
                float extra = packetMs[idx] - minOnMs;
                if (extra < 0.0f) extra = 0.0f;
                extraSum += extra;
            }
            const float avail = frameMs - minTotal;
            const float scale = (extraSum > 0.0f) ? (avail / extraSum) : 0.0f;
            for (size_t i = 0; i < allowedCount; ++i) {
                const uint8_t idx = allowedIdx[i];
                float extra = packetMs[idx] - minOnMs;
                if (extra < 0.0f) extra = 0.0f;
                packetMs[idx] = minOnMs + (extra * scale);
            }
        }

        const uint32_t frameStartMs = millis();
        bool abortMixed = false;

        if (allowedCount > 0) {
            rotateOffset = static_cast<uint8_t>((rotateOffset + 1) % allowedCount);
        }

        for (size_t oi = 0; oi < allowedCount; ++oi) {
            const uint8_t idx = allowedIdx[(oi + rotateOffset) % allowedCount];
            const uint32_t pulseMs = static_cast<uint32_t>(lroundf(packetMs[idx]));
            if (pulseMs == 0) continue;

            const uint16_t mask = (1u << idx);
            bool testActive = false;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                testActive = wireTargetStatus.active;
                xSemaphoreGive(controlMtx);
            }
            if (testActive) {
                updateWireTestStatus(static_cast<uint8_t>(idx + 1),
                                     pulseMs,
                                     static_cast<uint32_t>(frameMs));
            }
            if (!_runMaskedPulse(this, mask, pulseMs, ledFeedback)) {
                if (!is12VPresent()) handle12VDrop();
                else setState(DeviceState::Shutdown);
                abortMixed = true;
                break;
            }
            session.tick();
        }

        if (abortMixed) break;

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
