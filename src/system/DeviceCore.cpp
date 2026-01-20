#include <Device.hpp>

#include <Utils.hpp>

#include <RGBLed.hpp>    // keep

#include <Buzzer.hpp>    // BUZZ macro

#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>



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

bool Device::waitForEventNotice(EventNotice& out, TickType_t toTicks) {

    if (!eventEvtQueue) {
        vTaskDelay(toTicks);
        return false;
    }
    return xQueueReceive(eventEvtQueue, &out, toTicks) == pdTRUE;

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

    xTaskCreate(

        Device::commandTask,

        "DevCmdTask",

        4096,

        this,

        1,

        &cmdTaskHandle

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

        case DevCmdType::SET_AC_FREQ:

            {

                int hz = cmd.i1;

                if (hz < 50) hz = 50;

                if (hz > 500) hz = 500;

                if (CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY) != hz) {

                    CONF->PutInt(AC_FREQUENCY_KEY, hz);

                }

            if (currentSensor && currentSensor->isContinuousRunning()) {

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


        case DevCmdType::SET_CURR_LIMIT: {

            float limitA = cmd.f1;

            if (limitA < 0.0f) limitA = 0.0f;

            if (CONF && !floatEq(CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A), limitA)) {

                CONF->PutFloat(CURR_LIMIT_KEY, limitA);

            }

            if (currentSensor) {

                currentSensor->configureOverCurrent(limitA, CURRENT_TIME);

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

void Device::pushEventUnlocked(Device* self,
                               Device::EventKind kind,
                               const char* reason,
                               uint32_t nowMs,
                               uint32_t epoch)
{
    if (!self || !reason || !reason[0]) return;

    Device::EventEntry& e = self->eventHistory[self->eventHead];
    e.kind = kind;
    e.ms = nowMs;
    e.epoch = epoch;
    strncpy(e.reason, reason, sizeof(e.reason) - 1);
    e.reason[sizeof(e.reason) - 1] = '\0';

    self->eventHead = (self->eventHead + 1) % self->kEventHistorySize;
    if (self->eventCount < self->kEventHistorySize) {
        self->eventCount++;
    }

    if (kind == Device::EventKind::Warning) {
        Device::EventEntry& w = self->warnHistory[self->warnHistoryHead];
        w = e;
        self->warnHistoryHead =
            (self->warnHistoryHead + 1) % self->kEventHistorySize;
        if (self->warnHistoryCount < self->kEventHistorySize) {
            self->warnHistoryCount++;
        }
    } else if (kind == Device::EventKind::Error) {
        Device::EventEntry& r = self->errorHistory[self->errorHistoryHead];
        r = e;
        self->errorHistoryHead =
            (self->errorHistoryHead + 1) % self->kEventHistorySize;
        if (self->errorHistoryCount < self->kEventHistorySize) {
            self->errorHistoryCount++;
        }
    }

    if (kind == Device::EventKind::Warning) {
        if (self->unreadWarn < self->kEventHistorySize) self->unreadWarn++;
    } else if (kind == Device::EventKind::Error) {
        if (self->unreadErr < self->kEventHistorySize) self->unreadErr++;
    }

    Device::EventNotice note{};
    note.kind = kind;
    note.ms = nowMs;
    note.epoch = epoch;
    note.unreadWarn = self->unreadWarn;
    note.unreadErr = self->unreadErr;
    strncpy(note.reason, reason, sizeof(note.reason) - 1);
    note.reason[sizeof(note.reason) - 1] = '\0';
    self->pushEventNotice(note);
}

void Device::setLastErrorReason(const char* reason) {
    if (!reason || !reason[0]) return;
    if (eventMtx == nullptr) {
        eventMtx = xSemaphoreCreateMutex();
    }
    const uint32_t nowMs = millis();
    uint32_t epoch = 0;
    if (RTC) {
        epoch = static_cast<uint32_t>(RTC->getUnixTime());
    }
    if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(lastErrorReason, reason, sizeof(lastErrorReason) - 1);
        lastErrorReason[sizeof(lastErrorReason) - 1] = '\0';
        lastErrorMs = nowMs;
        lastErrorEpoch = epoch;
        pushEventUnlocked(this, EventKind::Error, reason, nowMs, epoch);
        xSemaphoreGive(eventMtx);
    } else {
        strncpy(lastErrorReason, reason, sizeof(lastErrorReason) - 1);
        lastErrorReason[sizeof(lastErrorReason) - 1] = '\0';
        lastErrorMs = nowMs;
        lastErrorEpoch = epoch;
        pushEventUnlocked(this, EventKind::Error, reason, nowMs, epoch);
    }
}

void Device::addWarningReason(const char* reason) {
    if (!reason || !reason[0]) return;
    if (eventMtx == nullptr) {
        eventMtx = xSemaphoreCreateMutex();
    }
    const uint32_t nowMs = millis();
    uint32_t epoch = 0;
    if (RTC) {
        epoch = static_cast<uint32_t>(RTC->getUnixTime());
    }
    if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        pushEventUnlocked(this, EventKind::Warning, reason, nowMs, epoch);
        xSemaphoreGive(eventMtx);
    } else {
        pushEventUnlocked(this, EventKind::Warning, reason, nowMs, epoch);
    }
}

void Device::setLastStopReason(const char* reason) {
    if (!reason || !reason[0]) return;
    if (eventMtx == nullptr) {
        eventMtx = xSemaphoreCreateMutex();
    }
    const uint32_t nowMs = millis();
    uint32_t epoch = 0;
    if (RTC) {
        epoch = static_cast<uint32_t>(RTC->getUnixTime());
    }
    if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(lastStopReason, reason, sizeof(lastStopReason) - 1);
        lastStopReason[sizeof(lastStopReason) - 1] = '\0';
        lastStopMs = nowMs;
        lastStopEpoch = epoch;
        xSemaphoreGive(eventMtx);
    } else {
        strncpy(lastStopReason, reason, sizeof(lastStopReason) - 1);
        lastStopReason[sizeof(lastStopReason) - 1] = '\0';
        lastStopMs = nowMs;
        lastStopEpoch = epoch;
    }
}

Device::LastEventInfo Device::getLastEventInfo() const {
    LastEventInfo out{};
    if (const_cast<Device*>(this)->eventMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        if (lastErrorReason[0]) {
            out.hasError = true;
            out.errorMs = lastErrorMs;
            out.errorEpoch = lastErrorEpoch;
            strncpy(out.errorReason, lastErrorReason, sizeof(out.errorReason) - 1);
        }
        if (lastStopReason[0]) {
            out.hasStop = true;
            out.stopMs = lastStopMs;
            out.stopEpoch = lastStopEpoch;
            strncpy(out.stopReason, lastStopReason, sizeof(out.stopReason) - 1);
        }
        xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
    } else {
        if (lastErrorReason[0]) {
            out.hasError = true;
            out.errorMs = lastErrorMs;
            out.errorEpoch = lastErrorEpoch;
            strncpy(out.errorReason, lastErrorReason, sizeof(out.errorReason) - 1);
        }
        if (lastStopReason[0]) {
            out.hasStop = true;
            out.stopMs = lastStopMs;
            out.stopEpoch = lastStopEpoch;
            strncpy(out.stopReason, lastStopReason, sizeof(out.stopReason) - 1);
        }
    }
    out.errorReason[sizeof(out.errorReason) - 1] = '\0';
    out.stopReason[sizeof(out.stopReason) - 1] = '\0';
    return out;
}

size_t Device::getEventHistory(EventEntry* out, size_t maxOut) const {
    if (!out || maxOut == 0) return 0;
    size_t count = 0;
    auto copyOut = [&](size_t n) {
        const size_t total = n;
        for (size_t i = 0; i < total && count < maxOut; ++i) {
            const size_t idx =
                (eventHead + kEventHistorySize - 1 - i) % kEventHistorySize;
            out[count++] = eventHistory[idx];
        }
    };

    if (const_cast<Device*>(this)->eventMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        copyOut(eventCount);
        xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
    } else {
        copyOut(eventCount);
    }
    return count;
}

size_t Device::getErrorHistory(EventEntry* out, size_t maxOut) const {
    if (!out || maxOut == 0) return 0;
    size_t count = 0;
    auto copyOut = [&](size_t n) {
        const size_t total = n;
        for (size_t i = 0; i < total && count < maxOut; ++i) {
            const size_t idx =
                (errorHistoryHead + kEventHistorySize - 1 - i) %
                kEventHistorySize;
            out[count++] = errorHistory[idx];
        }
    };

    if (const_cast<Device*>(this)->eventMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        copyOut(errorHistoryCount);
        xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
    } else {
        copyOut(errorHistoryCount);
    }
    return count;
}

size_t Device::getWarningHistory(EventEntry* out, size_t maxOut) const {
    if (!out || maxOut == 0) return 0;
    size_t count = 0;
    auto copyOut = [&](size_t n) {
        const size_t total = n;
        for (size_t i = 0; i < total && count < maxOut; ++i) {
            const size_t idx =
                (warnHistoryHead + kEventHistorySize - 1 - i) %
                kEventHistorySize;
            out[count++] = warnHistory[idx];
        }
    };

    if (const_cast<Device*>(this)->eventMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        copyOut(warnHistoryCount);
        xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
    } else {
        copyOut(warnHistoryCount);
    }
    return count;
}

void Device::getUnreadEventCounts(uint8_t& warnCount, uint8_t& errCount) const {
    warnCount = 0;
    errCount = 0;
    if (const_cast<Device*>(this)->eventMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        warnCount = unreadWarn;
        errCount = unreadErr;
        xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
    } else {
        warnCount = unreadWarn;
        errCount = unreadErr;
    }
}

void Device::markEventHistoryRead() {
    if (eventMtx == nullptr) {
        eventMtx = xSemaphoreCreateMutex();
    }
    if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        unreadWarn = 0;
        unreadErr = 0;
        xSemaphoreGive(eventMtx);
    } else {
        unreadWarn = 0;
        unreadErr = 0;
    }
}



void Device::prepareForDeepSleep() {

    DEBUG_PRINTLN("[Device] Preparing for deep sleep (power down paths)");

    stopWireTargetTest();
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

bool Device::pushEventNotice(const EventNotice& note) {

    if (!eventEvtQueue) return false;

    if (xQueueSendToBack(eventEvtQueue, &note, 0) == pdTRUE) return true;

    EventNotice dump{};

    xQueueReceive(eventEvtQueue, &dump, 0); // drop oldest

    return xQueueSendToBack(eventEvtQueue, &note, 0) == pdTRUE;

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
    if (!eventEvtQueue) eventEvtQueue = xQueueCreate(8, sizeof(EventNotice));

    if (!cmdQueue)  cmdQueue  = xQueueCreate(12, sizeof(DevCommand));

    if (!ackQueue)  ackQueue  = xQueueCreate(12, sizeof(DevCommandAck));
    if (!controlMtx) controlMtx = xSemaphoreCreateMutex();



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

    xTaskCreate(

        Device::LedUpdateTask,

        "LedUpdateTask",

        LED_UPDATE_TASK_STACK_SIZE,

        this,

        LED_UPDATE_TASK_PRIORITY,

        &ledTaskHandle

    );

    // Initialize persistent power/session statistics

    POWER_TRACKER->begin();

    // Start fans (dual-channel) and the closed-loop control task

    startFanControlTask(); // runs continuously; reads DS18B20 roles



    // Start external command handler

    startCommandTask();



    // Start bus sampler (synchronized voltage+current history)

    busSampler = BUS_SAMPLER;

    if (busSampler && discharger) {

        busSampler->begin(currentSensor, discharger, 5);
        busSampler->attachNtc(NTC);

    }



    // Current sensor stays idle unless explicitly needed (wire presence probing).

    // Apply persisted over-current limit (default to hardware safe limit)
    if (currentSensor && CONF) {

        float limitA = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);

        if (limitA < 0.0f) limitA = 0.0f;

        currentSensor->configureOverCurrent(limitA, CURRENT_TIME);

    }

    startThermalTask();
    startControlTask();



    DEBUG_PRINTLN("[Device] Configuring system I/O pins");

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

    const uint16_t overrideMask = allowedOverrideMask;

    for (uint8_t i = 0; i < 10; ++i) {

        WireRuntimeState& ws = wireStateModel.wire(i + 1);

        const bool overrideActive = (overrideMask != 0);
        const bool overrideAllowed =
            overrideActive && ((overrideMask & (1u << i)) != 0);
        if (overrideActive) {
            ws.allowedByAccess = overrideAllowed;
        } else {
            ws.allowedByAccess = wireConfigStore.getAccessFlag(i + 1);
        }
        const bool cfgAllowed = ws.allowedByAccess;

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

    if (overrideMask != 0) {
        for (uint8_t i = 0; i < 10; ++i) {
            if (allowedOutputs[i] && !(overrideMask & (1u << i))) {
                allowedOutputs[i] = false;
            }
        }
    }

}

bool Device::probeWirePresence() {
    if (!WIRE || !discharger) return false;
    if (getState() != DeviceState::Idle) return false;

    if (indicator) indicator->clearAll();
    WIRE->disableAll();
    if (relayControl) {
        relayControl->turnOn();
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            delay(300);
        }
    }

    const bool ok = wirePresenceManager.probeAll(*WIRE,
                                                 wireStateModel,
                                                 wireConfigStore,
                                                 discharger);
    checkAllowedOutputs();
    return ok;
}



void Device::shutdown() {

    DEBUGGSTART();

    DEBUG_PRINTLN("-----------------------------------------------------------");

    DEBUG_PRINTLN("[Device] Initiating Shutdown Sequence ");

    DEBUG_PRINTLN("-----------------------------------------------------------");

    DEBUG_PRINTLN("[Device] Main loop finished, proceeding to shutdown");

    DEBUGGSTOP();



    BUZZ->bipSystemShutdown();

    stopWireTargetTest();
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

    DEBUG_PRINTLN("[Device] Shutdown Complete System is Now OFF ");

    DEBUG_PRINTLN("-----------------------------------------------------------");

    DEBUGGSTOP();

}



void Device::startTemperatureMonitor() {

    if (tempMonitorTaskHandle == nullptr) {

        xTaskCreate(

            Device::monitorTemperatureTask,

            "TempMonitorTask",

            TEMP_MONITOR_TASK_STACK_SIZE,

            this,

            TEMP_MONITOR_TASK_PRIORITY,

            &tempMonitorTaskHandle

        );

        DEBUG_PRINTLN("[Device] Temperature monitor started ");

    }

}



void Device::monitorTemperatureTask(void* param) {

    Device* self = static_cast<Device*>(param);


    const uint8_t sensorCount = self->tempSensor->getSensorCount();



    if (sensorCount == 0) {

        DEBUG_PRINTLN("[Device] No temperature sensors found! Skipping monitoring");

        vTaskDelete(nullptr);

        return;

    }



    self->tempSensor->startTemperatureTask(2500);

    DEBUG_PRINTF("[Device] Monitoring %u temperature sensors every 2s\n", sensorCount);



    while (true) {

        float tripC = DEFAULT_TEMP_THRESHOLD;
        float warnC = DEFAULT_TEMP_WARN_C;
        if (CONF) {
            tripC = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
            warnC = CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C);
        }
        if (!isfinite(tripC) || tripC <= 0.0f) tripC = DEFAULT_TEMP_THRESHOLD;
        if (!isfinite(warnC) || warnC < 0.0f) warnC = 0.0f;
        if (warnC > 0.0f && warnC >= tripC) warnC = tripC - 1.0f;

        bool anyWarn = false;
        float warnMax = -INFINITY;
        int warnIdx = -1;

        for (uint8_t i = 0; i < sensorCount; ++i) {

            const float temp = self->tempSensor->getTemperature(i);

           // DEBUG_PRINTF("[Device] TempSensor[%u] = %.2f??C\n", i, temp);



            if (warnC > 0.0f && temp >= warnC) {
                anyWarn = true;
                if (temp > warnMax) {
                    warnMax = temp;
                    warnIdx = i;
                }
            }

            if (temp >= tripC) {

                DEBUG_PRINTF("[Device] Overtemperature Detected! Sensor[%u] = %.2f??C\n", i, temp);

                BUZZ->bipOverTemperature();



                  // Visual: critical temperature overlay + fault background

                  RGB->postOverlay(OverlayEvent::TEMP_CRIT);

                  RGB->setFault();

                  RGB->showError(ErrorCategory::THERMAL, 1);

                  char reason[96] = {0};
                  snprintf(reason, sizeof(reason),
                           "Overtemp trip sensor[%u]=%.1fC (trip %.1fC)",
                           static_cast<unsigned>(i),
                           static_cast<double>(temp),
                           static_cast<double>(tripC));
                  self->setLastErrorReason(reason);

                  self->setState(DeviceState::Error);

                  WIRE->disableAll();

                  self->indicator->clearAll();

                vTaskDelete(nullptr);

            }

        }

        if (anyWarn && self->getState() != DeviceState::Error) {
            RGB->postOverlay(OverlayEvent::TEMP_WARN);
            if (!self->tempWarnLatched && warnIdx >= 0 && isfinite(warnMax)) {
                char warnReason[96] = {0};
                snprintf(warnReason, sizeof(warnReason),
                         "Temp warning sensor[%u]=%.1fC (warn %.1fC)",
                         static_cast<unsigned>(warnIdx),
                         static_cast<double>(warnMax),
                         static_cast<double>(warnC));
                self->addWarningReason(warnReason);
                self->tempWarnLatched = true;
            }
        } else if (!anyWarn) {
            self->tempWarnLatched = false;
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

    {
        float vcap = NAN;
        float curA = NAN;
        if (discharger) vcap = discharger->readCapVoltage();
        if (isfinite(vcap)) {
            int src = DEFAULT_CURRENT_SOURCE;
            if (CONF) {
                src = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
            }
            if (src == CURRENT_SRC_ACS && currentSensor) {
                const float i = currentSensor->readCurrent();
                if (isfinite(i)) {
                    curA = i;
                }
            }
            if (!isfinite(curA) && WIRE) {
                curA = WIRE->estimateCurrentFromVoltage(vcap, WIRE->getOutputMask());
            }
        }
        char reason[96] = {0};
        if (isfinite(vcap) && isfinite(curA)) {
            snprintf(reason, sizeof(reason),
                     "12V lost (Vcap=%.1fV I=%.2fA)",
                     static_cast<double>(vcap),
                     static_cast<double>(curA));
            setLastErrorReason(reason);
        } else if (isfinite(vcap)) {
            snprintf(reason, sizeof(reason), "12V lost (Vcap=%.1fV)",
                     static_cast<double>(vcap));
            setLastErrorReason(reason);
        } else if (isfinite(curA)) {
            snprintf(reason, sizeof(reason), "12V lost (I=%.2fA)",
                     static_cast<double>(curA));
            setLastErrorReason(reason);
        } else {
            setLastErrorReason("12V supply lost during run");
        }
    }

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

                setLastStopReason("Stop requested");

                setState(DeviceState::Shutdown);

                return false;

            }

        }



        // 3) NEW: Check over-current latch

        /*if (currentSensor && currentSensor->isOverCurrentLatched()) {

            DEBUG_PRINTLN("[Device] Over-current latch set during wait ???????? abort");

            handleOverCurrentFault();

            return false;

        }*/

    }


    return true;
}

bool Device::dischargeCapBank(float thresholdV, uint8_t maxRounds) {
    if (!discharger || !relayControl || !WIRE) return false;

    relayControl->turnOff();
    vTaskDelay(pdMS_TO_TICKS(20));

    for (uint8_t round = 0; round < maxRounds; ++round) {
        float v = discharger->readCapVoltage();
        if (isfinite(v) && v <= thresholdV) break;

        for (uint8_t idx = 1; idx <= HeaterManager::kWireCount; ++idx) {
            if (!wireConfigStore.getAccessFlag(idx)) continue;
            WIRE->setOutput(idx, true);
            delayWithPowerWatch(1000);
            WIRE->setOutput(idx, false);

            v = discharger->readCapVoltage();
            if (isfinite(v) && v <= thresholdV) break;
        }
    }

    WIRE->disableAll();
    const float vfinal = discharger->readCapVoltage();
    return isfinite(vfinal) && vfinal <= thresholdV;
}

bool Device::calibrateCapacitance() {
    if (!discharger || !relayControl || !WIRE) return false;

    uint16_t dischargeMask = 0;
    double gTot = 0.0;
    for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
        if (!wireConfigStore.getAccessFlag(i)) continue;
        WireInfo wi = WIRE->getWireInfo(i);
        if (!isfinite(wi.resistanceOhm) || wi.resistanceOhm <= 0.01f) {
            continue;
        }
        gTot += 1.0 / static_cast<double>(wi.resistanceOhm);
        dischargeMask |= static_cast<uint16_t>(1u << (i - 1));
    }
    if (!(gTot > 0.0) || dischargeMask == 0) {
        return false;
    }

    const double rLoad = 1.0 / gTot;
    const bool relayWasOn = relayControl->isOn();

    auto restore = [&]() {
        if (WIRE) WIRE->disableAll();
        if (relayControl) {
            if (relayWasOn) relayControl->turnOn();
            else relayControl->turnOff();
        }
    };

    if (WIRE) WIRE->disableAll();
    relayControl->turnOff();
    if (!delayWithPowerWatch(20)) {
        restore();
        return false;
    }

    const float v0 = discharger->sampleVoltageNow();
    if (!isfinite(v0) || v0 <= 0.0f) {
        restore();
        return false;
    }

    double capGuess = capBankCapF;
    if (!isfinite(capGuess) || capGuess <= 0.0) {
        capGuess = DEFAULT_CAP_BANK_CAP_F;
    }
    double dtS = 0.2;
    const double tau = rLoad * capGuess;
    if (isfinite(tau) && tau > 0.0) {
        dtS = tau * 0.35;
    }
    if (dtS < 0.05) dtS = 0.05;
    if (dtS > 0.6) dtS = 0.6;
    uint32_t dischargeMs = static_cast<uint32_t>(dtS * 1000.0);
    if (dischargeMs < 20) dischargeMs = 20;

    for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
        if (dischargeMask & (1u << (i - 1))) {
            WIRE->setOutput(i, true);
        }
    }

    if (!delayWithPowerWatch(dischargeMs)) {
        restore();
        return false;
    }

    const float v1 = discharger->sampleVoltageNow();
    if (WIRE) WIRE->disableAll();

    if (!isfinite(v1) || v1 <= 0.0f || v1 >= v0) {
        restore();
        return false;
    }

    const double ratio = static_cast<double>(v1) / static_cast<double>(v0);
    if (!isfinite(ratio) || ratio <= 0.05 || ratio >= 0.98) {
        restore();
        return false;
    }

    const double lnRatio = log(ratio);
    if (!isfinite(lnRatio) || lnRatio >= 0.0) {
        restore();
        return false;
    }

    const double capF = -dtS / (rLoad * lnRatio);
    if (!isfinite(capF) || capF <= 0.0) {
        restore();
        return false;
    }

    capBankCapF = static_cast<float>(capF);
    if (CONF) {
        CONF->PutFloat(CAP_BANK_CAP_F_KEY, capBankCapF);
    }

    DEBUG_PRINTF("[Device] Capacitance calibrated: V0=%.2fV V1=%.2fV dt=%.3fs R=%.2f ohm C=%.6fF\n",
                 (double)v0,
                 (double)v1,
                 (double)dtS,
                 (double)rLoad,
                 (double)capBankCapF);

    restore();
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

    const DeviceState startState = getState();

    const TickType_t start = xTaskGetTickCount();

    auto timedOut = [&]() -> bool {

        return (xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeoutMs;

    };



    auto failSafe = [&](const char* msg) -> bool {

        if (msg) DEBUG_PRINTLN(msg);

        if (WIRE) WIRE->disableAll();

        if (indicator) indicator->clearAll();

        relayControl->turnOff();

        setLastStopReason(msg ? msg : "Calibration aborted");

        if (getState() != DeviceState::Error) {
            setState(DeviceState::Shutdown);
        }
        if (startState == DeviceState::Idle && gEvt) {
            xEventGroupSetBits(gEvt, EVT_STOP_REQ);
        }

        return false;

    };



    DEBUG_PRINTLN("[Device] Manual calibration sequence starting");



    if (WIRE) WIRE->disableAll();
    if (indicator) indicator->clearAll();

    // Pre-discharge to a safe baseline before calibrations.
    dischargeCapBank(5.0f, 3);

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



    // 2) Capacitance calibration (relay cycled inside)

    if (!calibrateCapacitance()) {

        return failSafe("[Device] Capacitance calibration failed");

    }

    if (CONF) {
        CONF->PutBool(CALIB_CAP_DONE_KEY, true);
    }
    if (currentSensor) {
        if (WIRE) WIRE->disableAll();
        if (relayControl) relayControl->turnOff();
        if (!delayWithPowerWatch(50)) {
            return failSafe("[Device] Calibration aborted (power/watch stop)");
        }
        currentSensor->calibrateZeroCurrent();
        if (timedOut()) return failSafe("[Device] Calibration timeout (current sensor)");
        if (relayControl) relayControl->turnOn();
    }

    if (timedOut()) return failSafe("[Device] Calibration timeout (capacitance)");



    // 3) Recharge after discharge

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

    if (WIRE) WIRE->disableAll();
    if (indicator) indicator->clearAll();
    if (relayControl) {
        relayControl->turnOff();
        if (RGB) {
            RGB->postOverlay(OverlayEvent::RELAY_OFF);
            RGB->setOff();
        }
    }
    if (getState() != DeviceState::Error) {
        setState(DeviceState::Shutdown);
    }
    if (startState == DeviceState::Idle && gEvt) {
        xEventGroupSetBits(gEvt, EVT_STOP_REQ);
    }

    return true;

}



void Device::handleOverCurrentFault()

{

    DEBUG_PRINTLN("[Device] Over-current detected EMERGENCY SHUTDOWN");
    {
        float curA = NAN;
        float limitA = DEFAULT_CURR_LIMIT_A;
        if (discharger) {
            const float vcap = discharger->readCapVoltage();
            if (isfinite(vcap)) {
                int src = DEFAULT_CURRENT_SOURCE;
                if (CONF) {
                    src = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
                }
                if (src == CURRENT_SRC_ACS && currentSensor) {
                    const float i = currentSensor->readCurrent();
                    if (isfinite(i)) {
                        curA = i;
                    }
                }
                if (!isfinite(curA) && WIRE) {
                    curA = WIRE->estimateCurrentFromVoltage(vcap, WIRE->getOutputMask());
                }
            }
        }
        if (CONF) limitA = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
        if (!isfinite(limitA) || limitA <= 0.0f) limitA = DEFAULT_CURR_LIMIT_A;
        char reason[96] = {0};
        if (isfinite(curA)) {
            snprintf(reason, sizeof(reason), "Over-current trip (I=%.2fA lim=%.1fA)",
                     static_cast<double>(curA), static_cast<double>(limitA));
            setLastErrorReason(reason);
        } else {
            setLastErrorReason("Over-current trip");
        }
    }



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



    BaseType_t ok = xTaskCreate(

        Device::fanControlTask,

        "FanCtrlTask",

        3072,

        this,

        2,

        &fanTaskHandle

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



void Device::loadRuntimeSettings() {
    if (CONF) {
        capBankCapF = CONF->GetFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
    }

    if (!isfinite(capBankCapF) || capBankCapF < 0.0f) {
        capBankCapF = DEFAULT_CAP_BANK_CAP_F;
    }

    applyWireModelParamsFromNvs();
}

void Device::applyWireModelParamsFromNvs() {
    if (!CONF) return;

    static const char* kTauKeys[HeaterManager::kWireCount] = {
        W1TAU_KEY, W2TAU_KEY, W3TAU_KEY, W4TAU_KEY, W5TAU_KEY,
        W6TAU_KEY, W7TAU_KEY, W8TAU_KEY, W9TAU_KEY, W10TAU_KEY
    };
    static const char* kKKeys[HeaterManager::kWireCount] = {
        W1KLS_KEY, W2KLS_KEY, W3KLS_KEY, W4KLS_KEY, W5KLS_KEY,
        W6KLS_KEY, W7KLS_KEY, W8KLS_KEY, W9KLS_KEY, W10KLS_KEY
    };
    static const char* kCKeys[HeaterManager::kWireCount] = {
        W1CAP_KEY, W2CAP_KEY, W3CAP_KEY, W4CAP_KEY, W5CAP_KEY,
        W6CAP_KEY, W7CAP_KEY, W8CAP_KEY, W9CAP_KEY, W10CAP_KEY
    };

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        double tau = CONF->GetDouble(kTauKeys[i], DEFAULT_WIRE_MODEL_TAU);
        double k = CONF->GetDouble(kKKeys[i], DEFAULT_WIRE_MODEL_K);
        double c = CONF->GetDouble(kCKeys[i], DEFAULT_WIRE_MODEL_C);
        wireThermalModel.setWireThermalParams(i + 1, tau, k, c);
    }
}
