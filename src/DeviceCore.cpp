#include "Device.h"
#include "Utils.h"
#include "RGBLed.h"    // keep
#include "Buzzer.h"    // BUZZ macro

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

void Device::Init(TempSensor* temp,
                  CurrentSensor* current,
                  Relay* relay,
                  BypassMosfet* bypass,
                  CpDischg* discharger,
                  Indicator* ledIndicator)
{
    if (!instance) {
        instance = new Device(temp, current, relay, bypass, discharger, ledIndicator);
    }
}

Device* Device::Get() {
    return instance; // nullptr until Init(), or set in begin() below if constructed manually
}

Device::Device(TempSensor* temp,
               CurrentSensor* current,
               Relay* relay,
               BypassMosfet* bypass,
               CpDischg* discharger,
               Indicator* ledIndicator)
    : tempSensor(temp),
      currentSensor(current),
      relayControl(relay),
      bypassFET(bypass),
      discharger(discharger),
      indicator(ledIndicator) {}

void Device::begin() {
    // Adopt stack/static construction if user didn't call Init()
    if (!instance) instance = this;

    if (!gStateMtx) gStateMtx = xSemaphoreCreateMutex();
    if (!gEvt)      gEvt      = xEventGroupCreate();

    currentState = DeviceState::Shutdown;   // OFF at boot
    wifiStatus   = WiFiStatus::NotConnected;
    RGB->setOff();                          // LEDs off at boot

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Device Manager ⚙️              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    pinMode(DETECT_12V_PIN, INPUT);
    // Boot cues (background + overlay + sound)
    BUZZ->bipStartupSequence();
    RGB->postOverlay(OverlayEvent::WAKE_FLASH);

    checkAllowedOutputs();


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

    DEBUG_PRINTLN("[Device] Configuring system I/O pins 🧰");
}

void Device::checkAllowedOutputs() {
    DEBUG_PRINTLN("[Device] Checking allowed outputs from preferences 🔍");
    for (uint8_t i = 0; i < 10; ++i) {
        const bool cfgAllowed   = CONF->GetBool(outputKeys[i], false);
        const bool thermLocked  = thermalInitDone && wireThermal[i].locked;

        bool presenceOk = true;
        if (WIRE) {
            WireInfo wi = WIRE->getWireInfo(i + 1);
            // Only enforce if we've actually probed (we treat default=true)
            if (!wi.connected) {
                presenceOk = false;
            }
        }

        allowedOutputs[i] = cfgAllowed && !thermLocked && presenceOk;

        DEBUG_PRINTF(
            "[Device] OUT%02u => %s (cfg=%s, thermal=%s, presence=%s)\n",
            i + 1,
            allowedOutputs[i] ? "ENABLED ✅" : "DISABLED ⛔",
            cfgAllowed  ? "ON" : "OFF",
            thermLocked ? "LOCKED" : "OK",
            presenceOk  ? "OK" : "NO-WIRE"
        );
    }
}


void Device::shutdown() {
    DEBUGGSTART();
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Shutdown Sequence 🔻");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Main loop finished, proceeding to shutdown 🛑");
    DEBUGGSTOP();

    BUZZ->bipSystemShutdown();
    stopTemperatureMonitor();

    DEBUG_PRINTLN("[Device] Turning OFF Main Relay 🔌");
    RGB->postOverlay(OverlayEvent::RELAY_OFF);
    relayControl->turnOn(); // original behavior kept

    DEBUG_PRINTLN("[Device] Starting Capacitor Discharge ⚡");
    // discharger->discharge();

    DEBUG_PRINTLN("[Device] Disabling Inrush Bypass MOSFET ⛔");
    bypassFET->disable();

    DEBUG_PRINTLN("[Device] Updating Status LEDs 💡");
    RGB->setOff();  // final visual
    stopFanControlTask();
    FAN->stopCap();
    FAN->stopHeatsink();
    DEBUGGSTART();
    DEBUG_PRINTLN("[Device] Shutdown Complete – System is Now OFF ✅");
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
        DEBUG_PRINTLN("[Device] Temperature monitor started 🧪");
    }
}

void Device::monitorTemperatureTask(void* param) {
    Device* self = static_cast<Device*>(param);

    const float   threshold   = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
    const uint8_t sensorCount = self->tempSensor->getSensorCount();

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[Device] No temperature sensors found! Skipping monitoring ❌");
        vTaskDelete(nullptr);
        return;
    }

    self->tempSensor->startTemperatureTask(2500);
    DEBUG_PRINTF("[Device] Monitoring %u temperature sensors every 2s ⚙️\n", sensorCount);

    while (true) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            const float temp = self->tempSensor->getTemperature(i);
            DEBUG_PRINTF("[Device] TempSensor[%u] = %.2f°C 🌡️\n", i, temp);

            if (temp >= threshold) {
                DEBUG_PRINTF("[Device] Overtemperature Detected! Sensor[%u] = %.2f°C ❌\n", i, temp);
                BUZZ->bipOverTemperature();

                // Visual: critical temperature overlay + fault background
                RGB->postOverlay(OverlayEvent::TEMP_CRIT);
                RGB->setFault();

                self->currentState = DeviceState::Error;
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
        DEBUG_PRINTLN("[Device] Stopping Temperature Monitor Task 🧊❌");
        vTaskDelete(tempMonitorTaskHandle);
        tempMonitorTaskHandle = nullptr;
    }
}

void Device::stopLoopTask() {
    if (loopTaskHandle != nullptr) {
        DEBUG_PRINTLN("[Device] Stopping Device Loop Task 🧵❌");
        vTaskDelete(loopTaskHandle);
        loopTaskHandle = nullptr;
    } else {
        DEBUG_PRINTLN("[Device] Loop Task not running – no action taken 💤");
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
    DEBUG_PRINTLN("[Device] 12V lost during RUN → Emergency stop ⚠️");
    // Visual + audible
    RGB->postOverlay(OverlayEvent::RELAY_OFF);
    RGB->setFault();
    BUZZ->bip();

    // Cut power paths & loads immediately
    WIRE->disableAll();
    indicator->clearAll();
    bypassFET->disable();
    relayControl->turnOff();

    // Flip state under lock so StartLoop() will unwind
    if (StateLock()) {
        currentState = DeviceState::Error;
        StateUnlock();
    }
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
            DEBUG_PRINTLN("[Device] 12V lost during wait → abort");
            handle12VDrop();
            return false;
        }

        // 2) Check STOP request (existing behavior)
        if (gEvt) {
            EventBits_t bits = xEventGroupGetBits(gEvt);
            if (bits & EVT_STOP_REQ) {
                DEBUG_PRINTLN("[Device] STOP requested during wait → abort");
                xEventGroupClearBits(gEvt, EVT_STOP_REQ);
                currentState = DeviceState::Idle;
                return false;
            }
        }

        // 3) NEW: Check over-current latch
        /*if (currentSensor && currentSensor->isOverCurrentLatched()) {
            DEBUG_PRINTLN("[Device] Over-current latch set during wait → abort");
            handleOverCurrentFault();
            return false;
        }*/
    }

    return true;
}

void Device::calibrateIdleCurrent() {
    if (!currentSensor) {
        DEBUG_PRINTLN("[Device] Idle current calibration skipped (no CurrentSensor) ⚠️");
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
    DEBUG_PRINTLN("[Device] ⚡ Over-current detected → EMERGENCY SHUTDOWN");

    // 1) Latch global state to FAULT
    if (StateLock()) {
        currentState = DeviceState::Error;
        StateUnlock();
    }

    // 2) Immediately disable all loads and power paths
    if (WIRE)       WIRE->disableAll();
    if (indicator)  indicator->clearAll();
    if (bypassFET)  bypassFET->disable();
    if (relayControl) relayControl->turnOff();

    // 3) Feedback: critical current trip
    if (RGB) {
        RGB->setDeviceState(DevState::FAULT);          // red strobe background
        RGB->postOverlay(OverlayEvent::CURR_TRIP);     // short critical burst
    }

    if (BUZZ) {
        BUZZ->bipFault();  // reuse your existing FAULT pattern
    }
}
// ------------------- Fan control helpers -------------------
static inline uint8_t _mapTempToPct(float T, float Ton, float Tfull,
                                    float Toff, uint8_t lastPct)
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
        DEBUG_PRINTLN("[Device] Failed to start FanCtrlTask ❌");
    } else {
        DEBUG_PRINTLN("[Device] FanCtrlTask started ✅");
    }
}

void Device::stopFanControlTask() {
    if (fanTaskHandle) {
        vTaskDelete(fanTaskHandle);
        fanTaskHandle = nullptr;
        DEBUG_PRINTLN("[Device] FanCtrlTask stopped 📴");
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
