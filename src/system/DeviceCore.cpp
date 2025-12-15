#include "system/Device.h"
#include "system/Utils.h"
#include "control/RGBLed.h"    // keep
#include "control/Buzzer.h"    // BUZZ macro
#include <math.h>

// Single, shared instances (linked once)
SemaphoreHandle_t gStateMtx = nullptr;
EventGroupHandle_t gEvt     = nullptr;

// Map of output keys (0-indexed for outputs 1 to 10)
const char* outputKeys[10] = {
    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
    OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
};

// ===== Singleton storage & accessors =====
Device* Device::instance = nullptr;

void Device::Init(TempSensor* temp,CurrentSensor* current,Relay* relay,CpDischg* discharger,Indicator* ledIndicator)
{
    if (!instance) {
        instance = new Device(temp, current, relay, discharger, ledIndicator);
    }
}

Device* Device::Get() {
    return instance; // nullptr until Init(), or set in begin() below if constructed manually
}

Device::StateSnapshot Device::getStateSnapshot() const {
    StateSnapshot snap{};
    if (gStateMtx && xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE) {
        snap.state    = currentState;
        snap.sinceMs  = stateSinceMs;
        snap.seq      = stateSeq;
        xSemaphoreGive(gStateMtx);
    } else {
        snap.state   = currentState;
        snap.sinceMs = stateSinceMs;
        snap.seq     = stateSeq;
    }
    return snap;
}

DeviceState Device::getState() const {
    return currentState;
}

bool Device::waitForStateEvent(StateSnapshot& out, TickType_t toTicks) {
    if (!stateEvtQueue) {
        // queue not ready yet; wait and report false
        vTaskDelay(toTicks);
        return false;
    }
    return xQueueReceive(stateEvtQueue, &out, toTicks) == pdTRUE;
}

bool Device::waitForCommandAck(DevCommandAck& ack, TickType_t toTicks) {
    if (!ackQueue) {
        vTaskDelay(toTicks);
        return false;
    }
    return xQueueReceive(ackQueue, &ack, toTicks) == pdTRUE;
}

bool Device::submitCommand(DevCommand& cmd) {
    if (!cmdQueue) return false;
    DevCommand c = cmd;
    // assign id
    if (gStateMtx && xSemaphoreTake(gStateMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        cmdSeq++;
        c.id = cmdSeq;
        xSemaphoreGive(gStateMtx);
    } else {
        cmdSeq++;
        c.id = cmdSeq;
    }
    cmd.id = c.id;
    return xQueueSendToBack(cmdQueue, &c, 0) == pdTRUE;
}

static const char* deviceStateName(DeviceState s) {
    switch (s) {
        case DeviceState::Idle:     return "Idle";
        case DeviceState::Running:  return "Running";
        case DeviceState::Error:    return "Error";
        case DeviceState::Shutdown: return "Shutdown";
        default:                    return "?";
    }
}

void Device::startCommandTask() {
    if (cmdTaskHandle) return;
    xTaskCreatePinnedToCore(
        Device::commandTask,
        "DevCmdTask",
        4096,
        this,
        1,
        &cmdTaskHandle,
        APP_CPU_NUM
    );
}

void Device::commandTask(void* param) {
    Device* self = static_cast<Device*>(param);
    for (;;) {
        DevCommand cmd{};
        if (self->cmdQueue &&
            xQueueReceive(self->cmdQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            self->handleCommand(cmd);
        }
    }
}

void Device::handleCommand(const DevCommand& cmd) {
    auto sendAck = [&](bool ok) {
        if (!ackQueue) return;
        DevCommandAck ack{cmd.type, cmd.id, ok};
        if (xQueueSendToBack(ackQueue, &ack, 0) != pdTRUE) {
            DevCommandAck dump{};
            xQueueReceive(ackQueue, &dump, 0);
            xQueueSendToBack(ackQueue, &ack, 0);
        }
    };

    auto requiresSafe = [&](DevCmdType t) {
        switch (t) {
            case DevCmdType::SET_BUZZER_MUTE:
            case DevCmdType::SET_RELAY:
            case DevCmdType::SET_OUTPUT:
            case DevCmdType::SET_FAN_SPEED:
                return false;
            default:
                return true;
        }
    };

    if (requiresSafe(cmd.type)) {
        while (getState() == DeviceState::Running) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    auto floatEq = [](float a, float b) {
        return fabsf(a - b) <= 1e-3f;
    };

    bool ok = true;
    switch (cmd.type) {
        case DevCmdType::SET_LED_FEEDBACK:
            if (CONF->GetBool(LED_FEEDBACK_KEY, false) != cmd.b1) {
                CONF->PutBool(LED_FEEDBACK_KEY, cmd.b1);
            }
            break;
        case DevCmdType::SET_ON_TIME_MS:
            if (CONF->GetInt(ON_TIME_KEY, 500) != cmd.i1) {
                CONF->PutInt(ON_TIME_KEY, cmd.i1);
            }
            break;
        case DevCmdType::SET_OFF_TIME_MS:
            if (CONF->GetInt(OFF_TIME_KEY, 500) != cmd.i1) {
                CONF->PutInt(OFF_TIME_KEY, cmd.i1);
            }
            break;
        case DevCmdType::SET_DESIRED_VOLTAGE:
            if (!floatEq(CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0), cmd.f1)) {
                CONF->PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, cmd.f1);
            }
            break;
        case DevCmdType::SET_AC_FREQ:
            {
                int hz = cmd.i1;
                if (hz < 50) hz = 50;
                if (hz > 500) hz = 500;
                if (CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY) != hz) {
                    CONF->PutInt(AC_FREQUENCY_KEY, hz);
                }
                if (currentSensor) {
                    const uint32_t periodMs = (hz > 0) ? std::max(2, static_cast<int>(lroundf(1000.0f / hz))) : 2;
                    currentSensor->startContinuous(periodMs);
                }
            }
            break;
        case DevCmdType::SET_CHARGE_RES:
            if (!floatEq(CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f), cmd.f1)) {
                CONF->PutFloat(CHARGE_RESISTOR_KEY, cmd.f1);
            }
            break;
        case DevCmdType::SET_DC_VOLT:
            if (!floatEq(CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f), cmd.f1)) {
                CONF->PutFloat(DC_VOLTAGE_KEY, cmd.f1);
            }
            break;
        case DevCmdType::SET_ACCESS_FLAG: {
            const char* accessKeys[10] = {
                OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                OUT10_ACCESS_KEY
            };
            if (cmd.i1 >= 1 && cmd.i1 <= 10) {
                if (CONF->GetBool(accessKeys[cmd.i1 - 1], false) != cmd.b1) {
                    CONF->PutBool(accessKeys[cmd.i1 - 1], cmd.b1);
                }
                wireConfigStore.setAccessFlag(cmd.i1, cmd.b1);
            } else {
                ok = false;
            }
            break;
        }
        case DevCmdType::SET_WIRE_RES: {
            const char* rkeys[10] = {
                R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
                R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
            };
            if (cmd.i1 >= 1 && cmd.i1 <= 10) {
                if (!floatEq(CONF->GetFloat(rkeys[cmd.i1 - 1], DEFAULT_WIRE_RES_OHMS), cmd.f1)) {
                    CONF->PutFloat(rkeys[cmd.i1 - 1], cmd.f1);
                }
                wireConfigStore.setWireResistance(cmd.i1, cmd.f1);
                if (WIRE) {
                    WIRE->setWireResistance(cmd.i1, cmd.f1);
                }
            } else {
                ok = false;
            }
            break;
        }
        case DevCmdType::SET_TARGET_RES:
            if (!floatEq(CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS), cmd.f1)) {
                CONF->PutFloat(R0XTGT_KEY, cmd.f1);
            }
            wireConfigStore.setTargetResOhm(cmd.f1);
            if (WIRE) {
                WIRE->setTargetResistanceAll(cmd.f1);
            }
            break;
        case DevCmdType::SET_WIRE_OHM_PER_M:
            if (!floatEq(CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M), cmd.f1)) {
                CONF->PutFloat(WIRE_OHM_PER_M_KEY, cmd.f1);
            }
            wireConfigStore.setWireOhmPerM(cmd.f1);
            break;
        case DevCmdType::SET_WIRE_GAUGE: {
            int gauge = cmd.i1;
            if (gauge <= 0 || gauge > 60) {
                gauge = DEFAULT_WIRE_GAUGE;
            }
            if (CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE) != gauge) {
                CONF->PutInt(WIRE_GAUGE_KEY, gauge);
            }
            wireConfigStore.setWireGaugeAwg(gauge);
            if (WIRE) {
                WIRE->setWireGaugeAwg(gauge);
            }
            break;
        }
        case DevCmdType::SET_MANUAL_MODE:
            manualMode = cmd.b1;
            if (manualMode && gEvt) {
                xEventGroupSetBits(gEvt, EVT_STOP_REQ);
            }
            break;
        case DevCmdType::SET_COOLING_PROFILE: {
            applyCoolingProfile(cmd.b1);
            if (CONF && CONF->GetBool(COOLING_PROFILE_KEY, DEFAULT_COOLING_PROFILE_FAST) != cmd.b1) {
                CONF->PutBool(COOLING_PROFILE_KEY, cmd.b1);
            }
            break;
        }
        case DevCmdType::SET_LOOP_MODE: {
            int mode = (cmd.i1 == 1) ? 1 : 0;
            loopModeSetting = (mode == 1) ? LoopMode::Sequential : LoopMode::Advanced;
            if (CONF && CONF->GetInt(LOOP_MODE_KEY, DEFAULT_LOOP_MODE) != mode) {
                CONF->PutInt(LOOP_MODE_KEY, mode);
            }
            break;
        }
        case DevCmdType::SET_BUZZER_MUTE:
            BUZZ->setMuted(cmd.b1);
            break;
        case DevCmdType::SET_FAN_SPEED: {
            int pct = constrain(cmd.i1, 0, 100);
            FAN->setSpeedPercent(pct);
            break;
        }
        case DevCmdType::SET_RELAY:
            if (relayControl) {
                if (cmd.b1) relayControl->turnOn();
                else        relayControl->turnOff();
            }
            break;
        case DevCmdType::SET_OUTPUT:
            if (cmd.i1 >= 1 && cmd.i1 <= HeaterManager::kWireCount && WIRE) {
                WIRE->setOutput(cmd.i1, cmd.b1);
                if (indicator) indicator->setLED(cmd.i1, cmd.b1);
            } else {
                ok = false;
            }
            break;
        case DevCmdType::REQUEST_RESET:
            if (WIRE) WIRE->disableAll();
            if (indicator) indicator->clearAll();
            if (relayControl) relayControl->turnOff();
            setState(DeviceState::Shutdown);
            CONF->PutBool(RESET_FLAG, true);
            CONF->RestartSysDelayDown(3000);
            break;
        default:
            ok = false;
            break;
    }

    sendAck(ok);
}

void Device::setState(DeviceState next) {
    DeviceState prev;

    if (gStateMtx &&
        xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE)
    {
        prev = currentState;
        if (prev == next) {
            xSemaphoreGive(gStateMtx);
            return;
        }
        currentState  = next;
        stateSeq++;
        stateSinceMs  = millis();
        xSemaphoreGive(gStateMtx);
    } else {
        prev = currentState;
        if (prev == next) return;
        currentState  = next;
        stateSeq++;
        stateSinceMs  = millis();
    }

    StateSnapshot snap{};
    snap.state   = next;
    snap.sinceMs = stateSinceMs;
    snap.seq     = stateSeq;
    pushStateEvent(snap);

    onStateChanged(prev, next);
}

void Device::onStateChanged(DeviceState prev, DeviceState next) {
    DEBUG_PRINTF("[Device] State changed: %s -> %s\n",
                 deviceStateName(prev),
                 deviceStateName(next));
}

void Device::prepareForDeepSleep() {
    DEBUG_PRINTLN("[Device] Preparing for deep sleep (power down paths)");
    stopTemperatureMonitor();
    stopFanControlTask();
    if (FAN) {
        FAN->stopCap();
        FAN->stopHeatsink();
        FAN->setSpeedPercent(0);
    }
    if (WIRE) WIRE->disableAll();
    if (indicator) indicator->clearAll();
    if (relayControl) relayControl->turnOff();
    if (discharger) discharger->setBypassRelayGate(false);
    RGB->setOff();
    setState(DeviceState::Shutdown);
}

bool Device::pushStateEvent(const StateSnapshot& snap) {
    if (!stateEvtQueue) return false;
    if (xQueueSendToBack(stateEvtQueue, &snap, 0) == pdTRUE) return true;
    StateSnapshot dump{};
    xQueueReceive(stateEvtQueue, &dump, 0); // drop oldest
    return xQueueSendToBack(stateEvtQueue, &snap, 0) == pdTRUE;
}

Device::Device(TempSensor* temp,
               CurrentSensor* current,
               Relay* relay,
               CpDischg* discharger,
               Indicator* ledIndicator)
    : tempSensor(temp),
      currentSensor(current),
      relayControl(relay),
      discharger(discharger),
      indicator(ledIndicator) {}

void Device::begin() {
    // Adopt stack/static construction if user didn't call Init()
    if (!instance) instance = this;

    if (!gStateMtx) gStateMtx = xSemaphoreCreateMutex();
    if (!gEvt)      gEvt      = xEventGroupCreate();
    if (!stateEvtQueue) stateEvtQueue = xQueueCreate(8, sizeof(StateSnapshot));
    if (!cmdQueue)  cmdQueue  = xQueueCreate(12, sizeof(DevCommand));
    if (!ackQueue)  ackQueue  = xQueueCreate(12, sizeof(DevCommandAck));

    setState(DeviceState::Shutdown);        // OFF at boot
    wifiStatus   = WiFiStatus::NotConnected;
    RGB->setOff();                          // LEDs off at boot

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Device Manager               #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    pinMode(DETECT_12V_PIN, INPUT);
    // Boot cues (background + overlay + sound)
    BUZZ->bipStartupSequence();
    RGB->postOverlay(OverlayEvent::WAKE_FLASH); 

    wireConfigStore.loadFromNvs();
    checkAllowedOutputs();
    loadRuntimeSettings();


    // Per-channel LED feedback maintainer
    xTaskCreatePinnedToCore(
        Device::LedUpdateTask,
        "LedUpdateTask",
        LED_UPDATE_TASK_STACK_SIZE,
        this,
        LED_UPDATE_TASK_PRIORITY,
        &ledTaskHandle,
        LED_UPDATE_TASK_CORE
    );
    // Initialize persistent power/session statistics
    POWER_TRACKER->begin();
    // Start fans (dual-channel) and the closed-loop control task
    startFanControlTask(); // runs continuously; reads DS18B20 roles

    // Start external command handler
    startCommandTask();

    // Start bus sampler (synchronized voltage+current history)
    busSampler = BUS_SAMPLER;
    if (busSampler && currentSensor && discharger) {
        busSampler->begin(currentSensor, discharger, 5);
    }

    // Keep sampling + thermal integration alive even outside RUN.
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
    startThermalTask();

    DEBUG_PRINTLN("[Device] Configuring system I/O pins ðŸ§°");
}

void Device::syncWireRuntimeFromHeater() {
    const uint32_t nowMs = millis();

    for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
        WireRuntimeState& ws = wireStateModel.wire(i);
        ws.allowedByAccess = wireConfigStore.getAccessFlag(i);

        if (WIRE) {
            WireInfo wi = WIRE->getWireInfo(i);
            ws.tempC        = wi.temperatureC;
            ws.present      = wi.connected;
            ws.lastUpdateMs = nowMs;
        }

        ws.overTemp = isfinite(ws.tempC) && ws.tempC >= WIRE_T_MAX_C;
    }

    if (WIRE) {
        wireStateModel.setLastMask(WIRE->getOutputMask());
    }
}

void Device::checkAllowedOutputs() {
    DEBUG_PRINTLN("[Device] Checking allowed outputs from preferences");
    syncWireRuntimeFromHeater();

    for (uint8_t i = 0; i < 10; ++i) {
        WireRuntimeState& ws = wireStateModel.wire(i + 1);
        const bool cfgAllowed  = ws.allowedByAccess;
        const bool thermLocked = ws.locked ||
                                 ws.overTemp ||
                                 (isfinite(ws.tempC) && ws.tempC >= WIRE_T_MAX_C);
        const bool presentOk = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0) ? true : ws.present;
        allowedOutputs[i] = cfgAllowed && presentOk && !thermLocked;

        /*DEBUG_PRINTF(
            "[Device] OUT%02u => %s (cfg=%s, thermal=%s)\n",
            i + 1,
            allowedOutputs[i] ? "ENABLED" : "DISABLED",
            cfgAllowed  ? "ON" : "OFF",
            thermLocked ? "LOCKED" : "OK"
        );*/
    }
}

void Device::shutdown() {
    DEBUGGSTART();
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Shutdown Sequence ");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Main loop finished, proceeding to shutdown");
    DEBUGGSTOP();

    BUZZ->bipSystemShutdown();
    stopTemperatureMonitor();

    DEBUG_PRINTLN("[Device] Turning OFF Main Relay");
    RGB->postOverlay(OverlayEvent::RELAY_OFF);
    relayControl->turnOn(); // original behavior kept

    DEBUG_PRINTLN("[Device] Starting Capacitor Discharge");
    // discharger->discharge();

    DEBUG_PRINTLN("[Device] Updating Status LEDs");
    RGB->setOff();  // final visual
    stopFanControlTask();
    FAN->stopCap();
    FAN->stopHeatsink();
    DEBUGGSTART();
    DEBUG_PRINTLN("[Device] Shutdown Complete â€“ System is Now OFF ");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUGGSTOP();
}

void Device::startTemperatureMonitor() {
    if (tempMonitorTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            Device::monitorTemperatureTask,
            "TempMonitorTask",
            TEMP_MONITOR_TASK_STACK_SIZE,
            this,
            TEMP_MONITOR_TASK_PRIORITY,
            &tempMonitorTaskHandle,
            TEMP_MONITOR_TASK_CORE
        );
        DEBUG_PRINTLN("[Device] Temperature monitor started ");
    }
}

void Device::monitorTemperatureTask(void* param) {
    Device* self = static_cast<Device*>(param);

    const float   threshold   = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
    const uint8_t sensorCount = self->tempSensor->getSensorCount();

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[Device] No temperature sensors found! Skipping monitoring");
        vTaskDelete(nullptr);
        return;
    }

    self->tempSensor->startTemperatureTask(2500);
    DEBUG_PRINTF("[Device] Monitoring %u temperature sensors every 2s\n", sensorCount);

    while (true) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            const float temp = self->tempSensor->getTemperature(i);
           // DEBUG_PRINTF("[Device] TempSensor[%u] = %.2f°C\n", i, temp);

            if (temp >= threshold) {
                DEBUG_PRINTF("[Device] Overtemperature Detected! Sensor[%u] = %.2f°C\n", i, temp);
                BUZZ->bipOverTemperature();

                  // Visual: critical temperature overlay + fault background
                  RGB->postOverlay(OverlayEvent::TEMP_CRIT);
                  RGB->setFault();
                  RGB->showError(ErrorCategory::THERMAL, 1);
  
                  self->setState(DeviceState::Error);
                  WIRE->disableAll();
                  self->indicator->clearAll();
                vTaskDelete(nullptr);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TEMP_MONITOR_TASK_DELAY_MS));
    }
}

void Device::stopTemperatureMonitor() {
    if (tempSensor) {
        tempSensor->stopTemperatureTask();
    }
    if (tempMonitorTaskHandle != nullptr) {
        DEBUG_PRINTLN("[Device] Stopping Temperature Monitor Task ");
        vTaskDelete(tempMonitorTaskHandle);
        tempMonitorTaskHandle = nullptr;
    }
}

void Device::stopLoopTask() {
    if (loopTaskHandle != nullptr) {
        DEBUG_PRINTLN("[Device] Stopping Device Loop Task ");
        vTaskDelete(loopTaskHandle);
        loopTaskHandle = nullptr;
    } else {
        DEBUG_PRINTLN("[Device] Loop Task not running no action taken ");
    }
}

void Device::LedUpdateTask(void* param) {
    Device* device = static_cast<Device*>(param);
    const TickType_t delayTicks = pdMS_TO_TICKS(LED_UPDATE_TASK_DELAY_MS);

    while (true) {
        if (CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK)) {
            for (uint8_t i = 1; i <= 10; i++) {
                const bool state = WIRE->getOutputState(i);
                device->indicator->setLED(i, state);
            }
        }


        vTaskDelay(delayTicks);
    }
}


void Device::updateLed() {
    if (CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK)) {
        for (uint8_t i = 1; i <= 10; i++) {
            const bool state = WIRE->getOutputState(i);
            indicator->setLED(i, state);
        }
    }
}

// === Power-loss helpers =====================================================

bool Device::is12VPresent() const {
    // HIGH means 12V detected; LOW/disconnected triggers shutdown
    return digitalRead(DETECT_12V_PIN) == HIGH;
}

void Device::handle12VDrop() {
    DEBUG_PRINTLN("[Device] 12V lost during RUN  Emergency stop");
    // Visual + audible
    RGB->postOverlay(OverlayEvent::RELAY_OFF);
    RGB->setFault();
    RGB->showError(ErrorCategory::POWER, 3);
    BUZZ->bip();

    // Cut power paths & loads immediately
    WIRE->disableAll();
    indicator->clearAll();
    relayControl->turnOff();

    // Flip state so StartLoop() will unwind
    setState(DeviceState::Error);
}

/**
 * @brief Sleep for ms, but wake early if 12V disappears (or STOP requested).
 * @return true if full sleep elapsed, false if aborted.
 */
bool Device::delayWithPowerWatch(uint32_t ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); // or whatever granularity you used

    while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < ms) {
        vTaskDelay(period);

        // 1) Check 12V presence (existing behavior)
        if (!is12VPresent()) {
            DEBUG_PRINTLN("[Device] 12V lost during wait abort");
            handle12VDrop();
            return false;
        }

        // 2) Check STOP request (existing behavior)
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP requested during wait abort");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                setState(DeviceState::Idle);
                return false;
            }
        }

        // 3) NEW: Check over-current latch
        /*if (currentSensor && currentSensor->isOverCurrentLatched()) {
            DEBUG_PRINTLN("[Device] Over-current latch set during wait â†’ abort");
            handleOverCurrentFault();
            return false;
        }*/
    }

    return true;
}

bool Device::calibrateCapVoltageGain() {
    // Do not recalibrate while already in RUN.
    if (getState() == DeviceState::Running) {
        DEBUG_PRINTLN("[Device] Cap gain calibration skipped (already running)");
        return false;
    }

    if (!discharger || !currentSensor || !relayControl || !WIRE) {
        DEBUG_PRINTLN("[Device] Cap gain calibration skipped (missing dependency)");
        return false;
    }

    // Short pulse and sampling settings to avoid heating yet gather data.
    static constexpr uint8_t  kSamplesPerWire   = 6;
    static constexpr uint32_t kSettleMs         = 25;
    static constexpr uint32_t kBetweenWireMs    = 15;
    static constexpr uint32_t kSampleDelayMs    = 5;
    static constexpr float    kMinAdcVolts      = 0.01f;
    static constexpr float    kMinBusVolts      = 1.0f;
    static constexpr float    kMinCurrentAbsA   = 0.1f;
    static constexpr float    kMinPresenceFrac  = 0.05f; // 5% of expected current
    static constexpr uint8_t  kCoarseSamples    = 12;
    static constexpr float    kCoarseMinAdcV    = 0.02f;

    // Make sure path is quiet; start with relay open for coarse estimate.
    relayControl->turnOff();
    WIRE->disableAll();
    if (indicator) {
        indicator->clearAll();
    }
    vTaskDelay(pdMS_TO_TICKS(40));

    float cfgBusV = DEFAULT_DC_VOLTAGE;
    if (CONF) {
        cfgBusV = CONF->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);
        if (cfgBusV <= 0.0f) {
            cfgBusV = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, DEFAULT_DESIRED_OUTPUT_VOLTAGE);
        }
    }
    if (!isfinite(cfgBusV) || cfgBusV <= 0.0f) {
        cfgBusV = DEFAULT_DC_VOLTAGE;
    }
    // Coarse expected mains-derived DC (full-wave rectified 220–230 VAC).
    const float coarseTarget = constrain(cfgBusV, 311.0f, 325.0f);

    // --- Coarse gain from open-relay measurement ---
    float coarseVadcSum = 0.0f;
    uint8_t coarseCnt   = 0;
    for (uint8_t i = 0; i < kCoarseSamples; ++i) {
        float vadc = discharger->adcCodeToAdcVolts(discharger->sampleAdcRaw());
        if (isfinite(vadc)) {
            coarseVadcSum += vadc;
            ++coarseCnt;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    float coarseGain = NAN;
    if (coarseCnt > 0) {
        const float vadc = coarseVadcSum / coarseCnt;
        if (vadc > kCoarseMinAdcV && coarseTarget > CAP_EMP_OFFSET) {
            coarseGain = (coarseTarget - CAP_EMP_OFFSET) / vadc;
            if (isfinite(coarseGain)) {
                if (coarseGain < CAP_EMP_GAIN_MIN) coarseGain = CAP_EMP_GAIN_MIN;
                if (coarseGain > CAP_EMP_GAIN_MAX) coarseGain = CAP_EMP_GAIN_MAX;
                discharger->setEmpiricalGain(coarseGain, false);
                DEBUG_PRINTF("[Device] Cap coarse gain: target=%.1fV Vadc=%.3fV -> gain=%.2f\n",
                             (double)coarseTarget,
                             (double)vadc,
                             (double)coarseGain);
            }
        }
    }

    // For fine calibration we need relay on.
    relayControl->turnOn();
    vTaskDelay(pdMS_TO_TICKS(20));

    float   gains[HeaterManager::kWireCount] = {0};
    uint8_t gainCount = 0;

    for (uint8_t idx = 1; idx <= HeaterManager::kWireCount; ++idx) {
        if (!wireConfigStore.getAccessFlag(idx)) {
            continue;
        }

        WireInfo wi = WIRE->getWireInfo(idx);
        const float R = wi.resistanceOhm;
        if (!isfinite(R) || R <= 0.1f) {
            continue;
        }

        // Energize a single wire and sample current + ADC voltage.
        WIRE->setOutput(idx, true);
        if (!delayWithPowerWatch(kSettleMs)) {
            WIRE->setOutput(idx, false);
            return false;
        }

        float sumI = 0.0f;
        float sumVadc = 0.0f;
        uint8_t valid = 0;

        for (uint8_t s = 0; s < kSamplesPerWire; ++s) {
            float i    = currentSensor->readCurrent();
            float vadc = discharger->adcCodeToAdcVolts(discharger->sampleAdcRaw());

            if (isfinite(i) && isfinite(vadc)) {
                sumI   += i;
                sumVadc += vadc;
                ++valid;
            }

            if (!delayWithPowerWatch(kSampleDelayMs)) {
                WIRE->setOutput(idx, false);
                return false;
            }
        }

        WIRE->setOutput(idx, false);
        vTaskDelay(pdMS_TO_TICKS(kBetweenWireMs));

        if (valid == 0) {
            continue;
        }

        const float iAvg    = sumI / valid;
        const float vAdcAvg = sumVadc / valid;
        const float absI    = fabsf(iAvg);

        // Presence check: require either absolute floor or % of expected.
        float expectedI = 0.0f;
        if (cfgBusV > 0.0f && R > 0.0f) {
            expectedI = cfgBusV / R;
        }
        float presenceFloor = kMinCurrentAbsA;
        if (isfinite(expectedI) && expectedI > 0.0f) {
            const float frac = expectedI * kMinPresenceFrac;
            if (frac > presenceFloor) {
                presenceFloor = frac;
            }
        }
        const bool present = absI >= presenceFloor;
        WIRE->setWirePresence(idx, present, absI);
        if (!present) {
            continue;
        }

        if (vAdcAvg <= kMinAdcVolts) {
            continue;
        }

        const float busV = absI * R;
        if (!isfinite(busV) || busV <= CAP_EMP_OFFSET || busV < kMinBusVolts) {
            continue;
        }

        float gain = (busV - CAP_EMP_OFFSET) / vAdcAvg;
        if (!isfinite(gain)) {
            continue;
        }
        if (gain < CAP_EMP_GAIN_MIN) gain = CAP_EMP_GAIN_MIN;
        if (gain > CAP_EMP_GAIN_MAX) gain = CAP_EMP_GAIN_MAX;

        gains[gainCount++] = gain;

        DEBUG_PRINTF("[Device] Cap gain cal OUT%u: R=%.2f I=%.3fA Vbus=%.2fV Vadc=%.3fV -> gain=%.2f\n",
                     (unsigned)idx,
                     (double)R,
                     (double)absI,
                     (double)busV,
                     (double)vAdcAvg,
                     (double)gain);
    }

    if (gainCount == 0) {
        if (isfinite(coarseGain)) {
            discharger->setEmpiricalGain(coarseGain, true);
            DEBUG_PRINTLN("[Device] Cap gain calibration: fine skipped, coarse applied");
            return true;
        }
        DEBUG_PRINTLN("[Device] Cap gain calibration skipped (no valid wire samples)");
        return false;
    }

    float sumGain = 0.0f;
    for (uint8_t i = 0; i < gainCount; ++i) {
        sumGain += gains[i];
    }
    const float finalGain = sumGain / static_cast<float>(gainCount);
    discharger->setEmpiricalGain(finalGain, true);

    DEBUG_PRINTF("[Device] Cap empirical gain calibrated to %.2f using %u wire(s)%s\n",
                 (double)finalGain,
                 (unsigned)gainCount,
                 isfinite(coarseGain) ? " (coarse seed used)" : "");
    return true;
}

bool Device::calibrateCapacitance() {
    if (getState() == DeviceState::Running) {
        DEBUG_PRINTLN("[Device] Capacitance calibration skipped (already running)");
        return false;
    }
    if (!discharger || !relayControl || !WIRE) {
        DEBUG_PRINTLN("[Device] Capacitance calibration skipped (missing dependency)");
        return false;
    }

    static constexpr float    kMinBusV        = 40.0f;
    static constexpr float    kMinAdcV        = 0.02f;
    static constexpr float    kCapMinF        = 1e-6f;   // 1 µF
    static constexpr float    kCapMaxF        = 0.5f;    // 0.5 F (sanity)
    static constexpr uint32_t kSettleMs       = 30;
    static constexpr uint32_t kRechargeMs     = 250;
    static constexpr uint8_t  kMaxCoarseTries = 3;

    // Choose a valid, present wire with the highest resistance (slowest discharge).
    uint8_t bestIdx = 0;
    float   bestR   = 0.0f;

    for (uint8_t idx = 1; idx <= HeaterManager::kWireCount; ++idx) {
        if (!wireConfigStore.getAccessFlag(idx)) {
            continue;
        }
        WireInfo wi = WIRE->getWireInfo(idx);
        if (!(DEVICE_FORCE_ALL_WIRES_PRESENT != 0 || wi.connected)) {
            continue;
        }
        const float R = wi.resistanceOhm;
        if (!isfinite(R) || R <= 0.5f) {
            continue;
        }
        if (R > bestR) {
            bestR   = R;
            bestIdx = idx;
        }
    }

    if (bestIdx == 0) {
        DEBUG_PRINTLN("[Device] Capacitance calibration failed (no valid/present wire)");
        return false;
    }

    // Ensure quiet state.
    WIRE->disableAll();
    if (indicator) {
        indicator->clearAll();
    }

    // Ensure the bank is charged enough for a meaningful decay measurement.
    relayControl->turnOn();
    vTaskDelay(pdMS_TO_TICKS(kRechargeMs));
    float busV = discharger->readCapVoltage();
    if (!isfinite(busV) || busV < kMinBusV) {
        vTaskDelay(pdMS_TO_TICKS(kRechargeMs));
        busV = discharger->readCapVoltage();
    }
    if (!isfinite(busV) || busV < kMinBusV) {
        DEBUG_PRINTF("[Device] Capacitance calibration failed (bus too low: %.2fV)\n", (double)busV);
        return false;
    }

    // Disconnect input so we measure a true discharge curve.
    relayControl->turnOff();
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    // --- Coarse estimate (2-point) ---
    float Ccoarse = NAN;
    uint32_t tCoarseUs = 5000; // start with 5ms

    for (uint8_t attempt = 0; attempt < kMaxCoarseTries; ++attempt) {
        const float v0 = discharger->adcCodeToAdcVolts(discharger->sampleAdcRaw());
        if (!(isfinite(v0) && v0 > kMinAdcV)) {
            break;
        }

        WIRE->setOutput(bestIdx, true);
#ifdef ESP32
        ets_delay_us(tCoarseUs);
#else
        delayMicroseconds(tCoarseUs);
#endif
        const float v1 = discharger->adcCodeToAdcVolts(discharger->sampleAdcRaw());
        WIRE->setOutput(bestIdx, false);

        if (!(isfinite(v1) && v1 > kMinAdcV && v1 < v0)) {
            tCoarseUs = std::min<uint32_t>(tCoarseUs * 2u, 60000u);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const float lnRatio = logf(v1 / v0);
        if (!(isfinite(lnRatio) && lnRatio < -1e-4f)) {
            tCoarseUs = std::min<uint32_t>(tCoarseUs * 2u, 60000u);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const float dt = (float)tCoarseUs * 1e-6f;
        const float C  = -dt / (bestR * lnRatio);
        if (isfinite(C) && C >= kCapMinF && C <= kCapMaxF) {
            Ccoarse = C;
            break;
        }

        tCoarseUs = std::min<uint32_t>(tCoarseUs * 2u, 60000u);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!isfinite(Ccoarse)) {
        DEBUG_PRINTLN("[Device] Capacitance calibration failed (coarse estimate invalid)");
        // Restore relay for normal operation.
        relayControl->turnOn();
        return false;
    }

    // Recharge slightly (coarse probe may have drained the bank).
    relayControl->turnOn();
    vTaskDelay(pdMS_TO_TICKS(kRechargeMs));
    relayControl->turnOff();
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    // --- Fine estimate (linear regression on ln(Vadc) vs time) ---
    float dtTargetS = 0.7f * bestR * Ccoarse; // ~50% drop target for good SNR
    if (dtTargetS < 0.005f) dtTargetS = 0.005f;
    if (dtTargetS > 0.200f) dtTargetS = 0.200f;

    static constexpr uint8_t kSamples = 40;
    const uint32_t dtTargetUs = (uint32_t)lroundf(dtTargetS * 1e6f);
    uint32_t sampleStepUs = (kSamples > 1) ? (dtTargetUs / (kSamples - 1u)) : dtTargetUs;
    if (sampleStepUs < 200u)  sampleStepUs = 200u;
    if (sampleStepUs > 8000u) sampleStepUs = 8000u;

    float tSum = 0.0f;
    float lnSum = 0.0f;
    float t2Sum = 0.0f;
    float tLnSum = 0.0f;
    uint8_t n = 0;

    const uint32_t t0Us = micros();
    WIRE->setOutput(bestIdx, true);

    for (uint8_t i = 0; i < kSamples; ++i) {
        const uint32_t targetUs = t0Us + i * sampleStepUs;
        while ((int32_t)(micros() - targetUs) < 0) {
#ifdef ESP32
            ets_delay_us(50);
#else
            delayMicroseconds(50);
#endif
        }

        const float v = discharger->adcCodeToAdcVolts(discharger->sampleAdcRaw());
        if (isfinite(v) && v > kMinAdcV) {
            const float t = (float)(targetUs - t0Us) * 1e-6f;
            const float l = logf(v);
            if (isfinite(l)) {
                tSum   += t;
                lnSum  += l;
                t2Sum  += t * t;
                tLnSum += t * l;
                ++n;
            }
        }
    }

    WIRE->setOutput(bestIdx, false);

    // Restore relay for normal operation.
    relayControl->turnOn();

    if (n < 8) {
        DEBUG_PRINTLN("[Device] Capacitance calibration failed (not enough valid samples)");
        return false;
    }

    const float denom = (float)n * t2Sum - (tSum * tSum);
    if (!(isfinite(denom) && fabsf(denom) > 1e-9f)) {
        DEBUG_PRINTLN("[Device] Capacitance calibration failed (degenerate regression)");
        return false;
    }

    const float slope = ((float)n * tLnSum - (tSum * lnSum)) / denom; // ln(V) = b + slope*t
    if (!(isfinite(slope) && slope < 0.0f)) {
        DEBUG_PRINTLN("[Device] Capacitance calibration failed (invalid slope)");
        return false;
    }

    const float Cfine = -1.0f / (slope * bestR);
    if (!(isfinite(Cfine) && Cfine >= kCapMinF && Cfine <= kCapMaxF)) {
        DEBUG_PRINTF("[Device] Capacitance calibration failed (C out of range: %.6f F)\n", (double)Cfine);
        return false;
    }

    capBankCapF = Cfine;
    if (CONF) {
        CONF->PutFloat(CAP_BANK_CAP_F_KEY, capBankCapF);
    }

    DEBUG_PRINTF("[Device] Capacitance calibrated: C=%.6f F using OUT%u (R=%.2fΩ)\n",
                 (double)capBankCapF,
                 (unsigned)bestIdx,
                 (double)bestR);
    return true;
}

bool Device::runCalibrationsStandalone(uint32_t timeoutMs) {
    if (getState() == DeviceState::Running) {
        DEBUG_PRINTLN("[Device] Calibration skipped (already running)");
        return false;
    }
    if (!relayControl || !discharger) {
        DEBUG_PRINTLN("[Device] Calibration skipped (missing relay/discharger)");
        return false;
    }

    const TickType_t start = xTaskGetTickCount();
    auto timedOut = [&]() -> bool {
        return (xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeoutMs;
    };

    auto failSafe = [&](const char* msg) -> bool {
        if (msg) DEBUG_PRINTLN(msg);
        if (WIRE) WIRE->disableAll();
        if (indicator) indicator->clearAll();
        relayControl->turnOff();
        setState(DeviceState::Idle);
        return false;
    };

    DEBUG_PRINTLN("[Device] Manual calibration sequence starting");

    if (WIRE) WIRE->disableAll();
    if (indicator) indicator->clearAll();

    // 0) Zero current calibration (relay off)
    relayControl->turnOff();
    vTaskDelay(pdMS_TO_TICKS(40));
    if (currentSensor) {
        currentSensor->calibrateZeroCurrent();
    }
    if (timedOut()) return failSafe("[Device] Calibration timeout (zero current)");

    // 1) Charge caps to threshold
    relayControl->turnOn();
    TickType_t lastChargePost = 0;
    while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
        if (timedOut()) return failSafe("[Device] Calibration timeout (charging caps)");
        TickType_t now = xTaskGetTickCount();
        if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
            if (RGB) RGB->postOverlay(OverlayEvent::PWR_CHARGING);
            lastChargePost = now;
        }
        if (!delayWithPowerWatch(200)) {
            return failSafe("[Device] Calibration aborted (power/watch stop)");
        }
    }

    // 2) Idle current calibration (relay on, heaters off)
    calibrateIdleCurrent();
    if (timedOut()) return failSafe("[Device] Calibration timeout (idle current)");

    // 3) Cap voltage gain calibration (presence + empirical gain)
    if (!calibrateCapVoltageGain()) {
        return failSafe("[Device] Cap gain calibration failed");
    }
    if (timedOut()) return failSafe("[Device] Calibration timeout (gain)");

    // 4) Capacitance calibration (relay cycled inside)
    if (!calibrateCapacitance()) {
        return failSafe("[Device] Capacitance calibration failed");
    }
    if (timedOut()) return failSafe("[Device] Calibration timeout (capacitance)");

    // 5) Recharge after discharge
    TickType_t lastRechargePost = 0;
    while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
        if (timedOut()) return failSafe("[Device] Calibration timeout (recharge)");
        TickType_t now = xTaskGetTickCount();
        if ((now - lastRechargePost) * portTICK_PERIOD_MS >= 1000) {
            if (RGB) RGB->postOverlay(OverlayEvent::PWR_CHARGING);
            lastRechargePost = now;
        }
        if (!delayWithPowerWatch(200)) {
            return failSafe("[Device] Calibration aborted (power/watch stop)");
        }
    }

    DEBUG_PRINTLN("[Device] Manual calibration sequence completed");
    return true;
}

void Device::calibrateIdleCurrent() {
    if (!currentSensor) {
        DEBUG_PRINTLN("[Device] Idle current calibration skipped (no CurrentSensor) ");
        return;
    }

    // Ensure all heater outputs are OFF during calibration
    if (WIRE) {
        WIRE->disableAll();
    }
    if (indicator) {
        indicator->clearAll();
    }

    // Short settle to let caps / relay stabilize
    vTaskDelay(pdMS_TO_TICKS(50));

    const uint16_t samples = 64;          // Enough to smooth noise
    const uint16_t delayMs = 5;           // 64 * 5ms = 320ms total
    float sum = 0.0f;

    for (uint16_t i = 0; i < samples; ++i) {
        float I = currentSensor->readCurrent();  // Uses its own averaging
        sum += I;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }

    float idle = sum / samples;
    if (idle < 0.0f) idle = 0.0f;

    // Store in NVS so applyHeatingFromCapture() can use it.
    CONF->PutFloat(IDLE_CURR_KEY, idle);

    DEBUG_PRINTF("[Device] Idle current calibrated: %.4f A (relay+AC, no heaters)\n", idle);
}

void Device::handleOverCurrentFault()
{
    DEBUG_PRINTLN("[Device] âš¡ Over-current detected EMERGENCY SHUTDOWN");

    // 1) Latch global state to FAULT
    setState(DeviceState::Error);

    // 2) Immediately disable all loads and power paths
    if (WIRE)       WIRE->disableAll();
    if (indicator)  indicator->clearAll();
    if (relayControl) relayControl->turnOff();

    // 3) Feedback: critical current trip
      if (RGB) {
          RGB->setDeviceState(DevState::FAULT);          // red strobe background
          RGB->postOverlay(OverlayEvent::CURR_TRIP);     // short critical burst
          RGB->showError(ErrorCategory::POWER, 1);
      }

    if (BUZZ) {
        BUZZ->bipFault();  // reuse your existing FAULT pattern
    }
}
// ------------------- Fan control helpers -------------------
static inline uint8_t _mapTempToPct(float T, float Ton, float Tfull,float Toff, uint8_t lastPct)
{
    if (!isfinite(T)) {
        // Sensor missing? Keep previous command; don't slam fans.
        return lastPct;
    }

    // Hysteresis: below Toff -> demand 0; between Toff and Ton -> hold last
    if (T <= Toff) return 0;
    if (T < Ton)   return lastPct;

    if (T >= Tfull) return 100;

    // Linear ramp Ton..Tfull -> 0..100
    float pct = ( (T - Ton) / (Tfull - Ton) ) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    // Guarantee a minimum spin when non-zero
    if (pct > 0.0f && pct < FAN_MIN_RUN_PCT) pct = FAN_MIN_RUN_PCT;
    return (uint8_t)(pct + 0.5f);
}

// ------------------- Fan control task RTOS API -------------------
void Device::startFanControlTask() {
    if (fanTaskHandle) return;

    BaseType_t ok = xTaskCreatePinnedToCore(
        Device::fanControlTask,
        "FanCtrlTask",
        3072,
        this,
        2,
        &fanTaskHandle,
        1
    );
    if (ok != pdPASS) {
        fanTaskHandle = nullptr;
        DEBUG_PRINTLN("[Device] Failed to start FanCtrlTask ");
    } else {
        DEBUG_PRINTLN("[Device] FanCtrlTask started ");
    }
}

void Device::stopFanControlTask() {
    if (fanTaskHandle) {
        vTaskDelete(fanTaskHandle);
        fanTaskHandle = nullptr;
        DEBUG_PRINTLN("[Device] FanCtrlTask stopped ");
    }
}

void Device::fanControlTask(void* param) {
    Device* self = static_cast<Device*>(param);
    const TickType_t period = pdMS_TO_TICKS(FAN_CTRL_PERIOD_MS);

    // Ensure FanManager is alive
    FAN->begin();

    for (;;) {
        // If 12V path is gone, shut fans off gracefully.
        if (!self->is12VPresent()) {                     // uses your existing helper
            if (self->lastCapFanPct) { FAN->stopCap();      self->lastCapFanPct = 0; }
            if (self->lastHsFanPct)  { FAN->stopHeatsink(); self->lastHsFanPct  = 0; }
            vTaskDelay(period);
            continue;
        }

        // Read temperatures via semantic roles
        float tHS  = NAN;
        float tB0  = NAN, tB1 = NAN, tCAP = NAN;

        if (self->tempSensor) {
            tHS = self->tempSensor->getHeatsinkTemp();        // role-based (Heatsink)
            tB0 = self->tempSensor->getBoardTemp(0);          // Board0
            tB1 = self->tempSensor->getBoardTemp(1);          // Board1
        }

        // Capacitor/board fan uses the hotter of the two board sensors
        if (isfinite(tB0) && isfinite(tB1))      tCAP = max(tB0, tB1);
        else if (isfinite(tB0))                  tCAP = tB0;
        else if (isfinite(tB1))                  tCAP = tB1;  // else stays NAN

        // Compute targets with hysteresis & min-run
        uint8_t capPct = _mapTempToPct(tCAP, CAP_FAN_ON_C, CAP_FAN_FULL_C,
                                       CAP_FAN_OFF_C, self->lastCapFanPct);
        uint8_t hsPct  = _mapTempToPct(tHS,  HS_FAN_ON_C,  HS_FAN_FULL_C,
                                       HS_FAN_OFF_C,  self->lastHsFanPct);

        // Apply only if changed by > deadband
        if ( (capPct == 0 && self->lastCapFanPct != 0) ||
             (capPct > 0 && abs((int)capPct - (int)self->lastCapFanPct) >= FAN_CMD_DEADBAND_PCT) )
        {
            if (capPct == 0) FAN->stopCap();
            else            FAN->setCapSpeedPercent(capPct);
            self->lastCapFanPct = capPct;
        }

        if ( (hsPct == 0 && self->lastHsFanPct != 0) ||
             (hsPct > 0 && abs((int)hsPct - (int)self->lastHsFanPct) >= FAN_CMD_DEADBAND_PCT) )
        {
            if (hsPct == 0) FAN->stopHeatsink();
            else            FAN->setHeatsinkSpeedPercent(hsPct);
            self->lastHsFanPct = hsPct;
        }

        vTaskDelay(period);
    }
}

void Device::applyCoolingProfile(bool fastProfile) {
    coolingFastProfile = fastProfile;
    coolingScale = fastProfile ? COOLING_SCALE_AIR : coolingBuriedScale;
    wireThermalModel.setCoolingScale(coolingScale);
}

void Device::loadRuntimeSettings() {
    bool fast = DEFAULT_COOLING_PROFILE_FAST;
    int mode = DEFAULT_LOOP_MODE;

    if (CONF) {
        fast = CONF->GetBool(COOLING_PROFILE_KEY, DEFAULT_COOLING_PROFILE_FAST);
        mode = CONF->GetInt(LOOP_MODE_KEY, DEFAULT_LOOP_MODE);
        capBankCapF = CONF->GetFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
        coolingBuriedScale = CONF->GetFloat(COOL_BURIED_SCALE_KEY, DEFAULT_COOLING_SCALE_BURIED);
        coolingKCold       = CONF->GetFloat(COOL_KCOLD_KEY,        DEFAULT_COOL_K_COLD);
        coolingMaxDropC    = CONF->GetFloat(COOL_DROP_MAX_KEY,     DEFAULT_MAX_COOL_DROP_C);

        if (!isfinite(capBankCapF) || capBankCapF < 0.0f) {
            capBankCapF = DEFAULT_CAP_BANK_CAP_F;
        }

        if (!isfinite(coolingBuriedScale) || coolingBuriedScale <= 0.0f) {
            coolingBuriedScale = DEFAULT_COOLING_SCALE_BURIED;
        }
        if (!isfinite(coolingKCold) || coolingKCold <= 0.0f) {
            coolingKCold = DEFAULT_COOL_K_COLD;
        }
        if (!isfinite(coolingMaxDropC) || coolingMaxDropC <= 0.0f) {
            coolingMaxDropC = DEFAULT_MAX_COOL_DROP_C;
        }
    }

    applyCoolingProfile(fast);
    wireThermalModel.setCoolingParams(coolingKCold, coolingMaxDropC, coolingScale);

    if (mode != 0 && mode != 1) {
        mode = DEFAULT_LOOP_MODE;
    }
    loopModeSetting = (mode == 1) ? LoopMode::Sequential : LoopMode::Advanced;
}
