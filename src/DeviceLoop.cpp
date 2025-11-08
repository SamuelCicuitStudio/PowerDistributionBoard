#include "Device.h"
#include "Utils.h"
#include "RGBLed.h"
#include "Buzzer.h"
#include <vector>
#include <math.h>

// === Multi-output heating helpers (file-local; no header changes needed) ===

// tiny popcount for 10-bit masks
static inline uint8_t _pop10(uint16_t m) {
#if defined(__GNUC__)
  return (uint8_t)__builtin_popcount(m);
#else
  uint8_t c=0; while (m) { c += (m & 1); m >>= 1; } return c;
#endif
}

// parallel equivalent resistance for a set of wires
static inline float _req(uint16_t mask, const float R[10]) {
  float g = 0.0f;
  for (uint8_t i=0;i<10;++i) if (mask & (1u<<i)) {
    const float Ri = R[i];
    if (Ri > 0.01f && isfinite(Ri)) g += 1.0f / Ri;
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
  float bestScore = INFINITY; uint16_t best = 0; bool foundAbove = false;
  const uint16_t FULL = (1u<<10);

  for (uint16_t m=1; m<FULL; ++m) {
    if ((m & ~allowedMask) != 0) continue;
    const uint8_t k = _pop10(m);
    if (k==0 || k>maxActive) continue;

    const float Req = _req(m, R);
    if (!isfinite(Req)) continue;
    const bool  above = (Req >= target);
    const float err   = fabsf(Req - target);

    if (preferAboveOrEqual) {
      if (above && !foundAbove) { foundAbove = true; bestScore = INFINITY; best = 0; }
      if (!above && foundAbove)  continue;
    }

    float score = err;
    if (m == recentMask) score += 0.0001f;          // mild fairness

    // tie-breakers: fewer channels, then higher Req (safer current)
    if (score < bestScore ||
       (score == bestScore && k < _pop10(best)) ||
       (score == bestScore && k == _pop10(best) && Req > _req(best, R)))
    {
      bestScore = score; best = m;
    }
  }

  // if we insisted on ≥ target but nothing qualifies, allow undershoot once
  if (preferAboveOrEqual && best == 0) {
    return _chooseBest(allowedMask, R, target, maxActive, /*preferAboveOrEqual=*/false, recentMask);
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
  uint16_t last = 0;

  while (remaining) {
    uint16_t pick = _chooseBest(remaining, R, target, maxActive, preferAboveOrEqual, last);
    if (pick == 0) {
      // no multi-group possible → pick best single wire
      float bestErr = INFINITY; uint16_t solo = 0;
      for (uint8_t i=0;i<10;++i) if (remaining & (1u<<i)) {
        const float Req = _req((1u<<i), R);
        const float err = fabsf(Req - target);
        if (err < bestErr) { bestErr = err; solo = (1u<<i); }
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
static void _applyMask(Device* self, uint16_t mask, bool on, bool ledFeedback) {
  for (uint8_t i=0;i<10;++i) if (mask & (1u<<i)) {
    WIRE->setOutput(i+1, on);
    if (ledFeedback) self->indicator->setLED(i+1, on);
  }
}

// utility: boolean array -> 10-bit allowed mask
static inline uint16_t _allowedMaskFrom(const bool allowed[10]) {
  uint16_t m=0; for (uint8_t i=0;i<10;++i) if (allowed[i]) m |= (1u<<i); return m;
}

// ===== Loop task management & main state machine =====

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

    // Safe baseline
    relayControl->turnOff();
    bypassFET->disable();
    stopTemperatureMonitor();

    // We begin OFF at boot
    RGB->setOff();

    for (;;) {
        // ======= OFF =======
        if (StateLock()) { currentState = DeviceState::Shutdown; StateUnlock(); }

        // Fallback compatibility: if legacy code sets StartFromremote, translate it to WAKE+RUN
        if (StartFromremote) {
            StartFromremote = false;
            if (gEvt) xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
        }

        DEBUG_PRINTLN("[Device] State=OFF. Waiting for WAKE request (Tap#1 or Web Start) …");
        // Wait for a WAKE request; clear on exit so we "consume" it
        if (gEvt) {
            xEventGroupWaitBits(gEvt, EVT_WAKE_REQ, pdTRUE, pdFALSE, portMAX_DELAY);
        } else {
            // If event group wasn't created, don't deadlock; just proceed
            DEBUG_PRINTLN("[Device] ⚠️ gEvt is null; proceeding with WAKE");
        }

        // ======= POWER-UP sequence =======
        RGB->setWait();          // amber breathe background
        BUZZ->bip();

        DEBUG_PRINTLN("[Device] Waiting for 12V input… 🔋");
        while (!digitalRead(DETECT_12V_PIN)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        DEBUG_PRINTLN("[Device] 12V Detected – Enabling input relay ✅");
        relayControl->turnOn();
        RGB->postOverlay(OverlayEvent::RELAY_ON);

        // Charge to threshold with throttled overlay
        vTaskDelay(pdMS_TO_TICKS(150));
        TickType_t lastChargePost = 0;
        while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
            const TickType_t now = xTaskGetTickCount();
            if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) { // ≤ 1 Hz
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

        // Make sure NO heater outputs are active during idle calibration
        if (WIRE) {
            WIRE->disableAll();
        }
        if (indicator) {
            indicator->clearAll();
        }

        // Measure baseline current: AC + relay + caps topped, no heaters.
        calibrateIdleCurrent();

        checkAllowedOutputs();
        BUZZ->bipSystemReady();
        RGB->postOverlay(OverlayEvent::WAKE_FLASH);

        // If RUN already requested (Web Start), skip IDLE and go RUN
        bool runRequested = false;
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_RUN_REQ) {
                xEventGroupClearBits(gEvt, EVT_RUN_REQ);
                runRequested = true;
            }
        }

        if (!runRequested) {
            // ======= IDLE =======
            if (StateLock()) { currentState = DeviceState::Idle; StateUnlock(); }
            DEBUG_PRINTLN("[Device] State=IDLE. Waiting for RUN (Tap#2) or STOP …");
            RGB->setIdle();

            if (gEvt) {
                EventBits_t got = xEventGroupWaitBits(
                    gEvt, EVT_RUN_REQ | EVT_STOP_REQ, pdTRUE, pdFALSE, portMAX_DELAY
                );
                if (got & EVT_STOP_REQ) {
                    // Return to OFF
                    DEBUG_PRINTLN("[Device] STOP requested in IDLE → OFF");
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                    relayControl->turnOff();
                    bypassFET->disable();
                    RGB->setOff();
                    continue; // back to OFF loop
                }
                // otherwise RUN requested
            } else {
                // No event group: fall through to RUN
            }
        }

        // ======= RUN =======
        if (StateLock()) { currentState = DeviceState::Running; StateUnlock(); }
        DEBUG_PRINTLN("[Device] State=RUN. Launching main loop ▶️");
        BUZZ->successSound();
        RGB->postOverlay(OverlayEvent::PWR_START);
        RGB->setRun();

        // Start the user loop (will exit when you change currentState)
        StartLoop();

        // ======= CLEAN SHUTDOWN → OFF =======
        DEBUG_PRINTLN("[Device] StartLoop returned. Performing clean shutdown 🛑");
        BUZZ->bipSystemShutdown();
        RGB->postOverlay(OverlayEvent::RELAY_OFF);
        relayControl->turnOff();
        bypassFET->disable();

        RGB->setOff();
        // loop back to OFF and wait again
    }
}

void Device::StartLoop() {
    if (currentState != DeviceState::Running) return;

    DEBUGGSTART();
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Loop Sequence 🔻");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUGGSTOP();

    // Before starting any new heating sequence, ensure wires have cooled
    // close to ambient. This keeps the thermal model consistent across runs.
    //
    // Example: 5°C tolerance, no hard timeout (or set e.g. 60000 for 60s).
    waitForWiresNearAmbient(5.0f /*tolC*/, 0 /*maxWaitMs=0 => no limit*/);

    // Background running cue
    RGB->setRun();

    startTemperatureMonitor();  // Start temperature monitoring in background
    bypassFET->enable();
    checkAllowedOutputs();

    DEBUG_PRINTLN("[Device] Starting Output Activation Cycle 🔁");

    const uint16_t onTime      = CONF->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
    const uint16_t offTime     = CONF->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    const bool     ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

#if (DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_SEQUENTIAL)
    // -------------------------------------------------------------------------
    // SEQUENTIAL LOOP:
    // Always drives ONE allowed output at a time.
    // Now selects the COOLEST eligible wire based on virtual temperature.
    // Power-loss and STOP-safe via delayWithPowerWatch().
    // -------------------------------------------------------------------------
    DEBUG_PRINTLN("[Device] Loop mode: SEQUENTIAL (coolest-wire-first)");
    relayControl->turnOn();
    RGB->postOverlay(OverlayEvent::RELAY_ON);

    while (currentState == DeviceState::Running) {
        // 12V watchdog
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }

        // STOP request check
        if (gEvt) {
            EventBits_t b = xEventGroupGetBits(gEvt);
            if (b & EVT_STOP_REQ) {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                DEBUG_PRINTLN("[Device] STOP requested during RUN → exiting sequential loop");
                currentState = DeviceState::Idle;
                break;
            }
        }

        // Capture any runtime access changes (includes thermal lock state)
        checkAllowedOutputs();

        // Pick the coolest allowed wire based on virtual temperature
        bool    found    = false;
        uint8_t bestIdx  = 0;
        float   bestTemp = 1e9f;

        for (uint8_t i = 0; i < 10; ++i) {
            if (!allowedOutputs[i]) continue;

            float t = WIRE->getWireEstimatedTemp(i + 1);
            if (isnan(t)) {
                // No estimate yet → treat as ambient (safe / "cool")
                t = ambientC;
            }

            if (t < bestTemp) {
                bestTemp = t;
                bestIdx  = i;
                found    = true;
            }
        }

        if (found) {
            const uint8_t idx = bestIdx;

            // Turn this output ON
            heaterManager->setOutput(idx + 1, true);
            if (ledFeedback) indicator->setLED(idx + 1, true);

            // Guarded ON time (12V + STOP aware)
            if (!delayWithPowerWatch(onTime)) {
                // Ensure OFF on abort
                heaterManager->setOutput(idx + 1, false);
                if (ledFeedback) indicator->setLED(idx + 1, false);

                if (!is12VPresent()) {
                    // handle12VDrop() already called inside delayWithPowerWatch
                    break;
                }

                if (gEvt) {
                    EventBits_t b2 = xEventGroupGetBits(gEvt);
                    if (b2 & EVT_STOP_REQ) {
                        xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                        DEBUG_PRINTLN("[Device] STOP requested during RUN → exiting sequential loop");
                        currentState = DeviceState::Idle;
                        break;
                    }
                }
            } else {
                // Normal OFF phase
                heaterManager->setOutput(idx + 1, false);
                if (ledFeedback) indicator->setLED(idx + 1, false);

                if (!delayWithPowerWatch(offTime)) {
                    if (!is12VPresent()) {
                        // Already handled
                        break;
                    }

                    if (gEvt) {
                        EventBits_t b3 = xEventGroupGetBits(gEvt);
                        if (b3 & EVT_STOP_REQ) {
                            xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                            DEBUG_PRINTLN("[Device] STOP requested during RUN → exiting sequential loop");
                            currentState = DeviceState::Idle;
                            break;
                        }
                    }
                }
            }
        } else {
            // No allowed outputs → short idle with power + STOP watch
            if (!delayWithPowerWatch(100)) {
                if (!is12VPresent()) {
                    handle12VDrop();
                } else if (gEvt) {
                    EventBits_t b4 = xEventGroupGetBits(gEvt);
                    if (b4 & EVT_STOP_REQ) {
                        xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                        currentState = DeviceState::Idle;
                    }
                }
                break;
            }
        }
    }

#else  // DEVICE_LOOP_MODE == DEVICE_LOOP_MODE_ADVANCED
    // -------------------------------------------------------------------------
    // ADVANCED MODE:
    // Original batch / group logic retained.
    // Thermal lockout is already enforced via allowedOutputs + thermal model.
    // -------------------------------------------------------------------------
    while (currentState == DeviceState::Running) {
        // 12V watchdog up-front
        if (!is12VPresent()) {
            handle12VDrop();
            break;
        }

        // Handle STOP requests during RUN
        if (gEvt) {
            EventBits_t b = xEventGroupGetBits(gEvt);
            if (b & EVT_STOP_REQ) {
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                DEBUG_PRINTLN("[Device] STOP requested during RUN → exiting loop");
                currentState = DeviceState::Idle;   // causes StartLoop() to return
                break;
            }
        }

        if (rechargeMode == RechargeMode::BatchRecharge) {
            // ------------------ BATCH RECHARGE MODE ------------------
            relayControl->turnOn();
            RGB->postOverlay(OverlayEvent::RELAY_ON);
            if (!delayWithPowerWatch(200)) {
                if (!is12VPresent()) { handle12VDrop(); }
                else {
                    // likely STOP
                    if (gEvt) xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                    currentState = DeviceState::Idle;
                }
                break;
            }

            // ---- Multi-output plan: cover all allowed wires, near target Ω ----
            checkAllowedOutputs(); // pick up any access changes
            const float targetRes = CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);

            // actual per-wire resistances (use your calibrated/entered values)
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

            // knobs (safe defaults; you can surface these in UI later)
            const uint8_t maxActive        = MAX_ACTIVE; // try 2–3
            const bool    preferAboveEqual = PREF_ABOVE;         // avoid undershoot when possible

            std::vector<uint16_t> plan;
            const uint16_t allowedMask = _allowedMaskFrom(allowedOutputs);
            _buildPlan(plan, allowedMask, R, targetRes, maxActive, preferAboveEqual);

            if (plan.empty()) {
                DEBUG_PRINTLN("[Device] [Batch] No allowed outputs in plan; skipping.");
            } else {
                for (uint16_t mask : plan) {
                    if (currentState != DeviceState::Running) break;

                    // group pulse: turn this subset ON together to approximate target Ω
                    _applyMask(this, mask, true,  ledFeedback);
                    if (!delayWithPowerWatch(onTime)) {
                        _applyMask(this, mask, false, ledFeedback); // ensure OFF on abort
                        if (!is12VPresent()) { handle12VDrop(); }
                        else {
                            if (gEvt) xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                            currentState = DeviceState::Idle;
                        }
                        break;
                    }

                    _applyMask(this, mask, false, ledFeedback);
                    if (!delayWithPowerWatch(offTime)) {
                        if (!is12VPresent()) { handle12VDrop(); }
                        else {
                            if (gEvt) xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                            currentState = DeviceState::Idle;
                        }
                        break;
                    }
                }
            }

            // Recharge wait loop (RUN background + throttled charging overlay)
            TickType_t lastChargePost = 0;
            while (currentState == DeviceState::Running &&
                   discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {

                if (!is12VPresent()) {
                    handle12VDrop();
                    break;
                }

                const TickType_t now = xTaskGetTickCount();
                if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                    RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                    lastChargePost = now;
                }
                DEBUG_PRINTF("[Device] [Batch] Recharging... Cap: %.2fV / Target: %.2fV ⏳\n",
                             discharger->readCapVoltage(), GO_THRESHOLD_RATIO);

                // Background stays RUN
                RGB->setRun();

                if (!delayWithPowerWatch(200)) {
                    if (!is12VPresent()) { handle12VDrop(); }
                    else {
                        // likely STOP
                        if (gEvt) xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                        currentState = DeviceState::Idle;
                    }
                    break;
                }
            }
        }

        // (Overcurrent check etc. can sit here as in your original code)
    }
#endif  // DEVICE_LOOP_MODE

    // Background monitors
    stopTemperatureMonitor();

    WIRE->disableAll();
    indicator->clearAll();
}

