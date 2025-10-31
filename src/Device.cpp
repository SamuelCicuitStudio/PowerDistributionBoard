#include "Device.h"
#include "Utils.h"
#include "RGBLed.h"    // keep
#include "Buzzer.h"    // BUZZ macro
// Single, shared instances (linked once)
SemaphoreHandle_t gStateMtx = nullptr;
EventGroupHandle_t gEvt = nullptr;

// ===== Singleton storage & accessors =====
Device* Device::instance = nullptr;

void Device::Init(HeaterManager* heater,
                  TempSensor* temp,
                  CurrentSensor* current,
                  Relay* relay,
                  BypassMosfet* bypass,
                  CpDischg* discharger,
                  Indicator* ledIndicator)
{
    if (!instance) {
        instance = new Device(heater, temp, current, relay, bypass, discharger, ledIndicator);
    }
}

Device* Device::Get() {
    return instance; // nullptr until Init(), or set in begin() below if constructed manually
}

// Map of output keys (0-indexed for outputs 1 to 10)
const char* outputKeys[10] = {
    OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
    OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
};

Device::Device(HeaterManager* heater,
               TempSensor* temp,
               CurrentSensor* current,
               Relay* relay,
               BypassMosfet* bypass,
               CpDischg* discharger,
               Indicator* ledIndicator)
    : heaterManager(heater),
      tempSensor(temp),
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

    DEBUG_PRINTLN("[Device] Configuring system I/O pins 🧰");
}

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

void Device::checkAllowedOutputs() {
    DEBUG_PRINTLN("[Device] Checking allowed outputs from preferences 🔍");
    for (uint8_t i = 0; i < 10; ++i) {
        allowedOutputs[i] = CONF->GetBool(outputKeys[i], false);
        DEBUG_PRINTF("[Device] OUT%02u => %s ✅\n", i + 1, allowedOutputs[i] ? "ENABLED" : "DISABLED");
    }
}

void Device::StartLoop() {
    if (currentState != DeviceState::Running) return;

    DEBUGGSTART();
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Loop Sequence 🔻");
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUGGSTOP();

    // Background running cue
    RGB->setRun();

    startTemperatureMonitor();  // Start temperature monitoring in background
    bypassFET->enable();
    checkAllowedOutputs();

    DEBUG_PRINTLN("[Device] Starting Output Activation Cycle 🔁");

    const uint16_t onTime      = CONF->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
    const uint16_t offTime     = CONF->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    const bool     ledFeedback = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    while (currentState == DeviceState::Running) {

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
            delay(200);

            for (uint8_t i = 1; i <= 10; ++i) {
                if (currentState != DeviceState::Running) break;
                if (!allowedOutputs[i - 1]) continue;

                DEBUG_PRINTF("[Device] [Batch] Activating Output %u 🔥\n", i);

                // Visual: announce which channel is active (once per channel)
                RGB->postOutputEvent(i, true);

                // Pulse this output 10 times
                for (uint8_t pulse = 0; pulse < 10; ++pulse) {
                    if (currentState != DeviceState::Running) break;

                    if (ledFeedback) indicator->setLED(i, true);
                    heaterManager->setOutput(i, true);
                    delay(onTime);   // ON time

                    heaterManager->setOutput(i, false);
                    if (ledFeedback) indicator->setLED(i, false);
                    delay(offTime);  // OFF time between pulses
                }

                // Channel done
                RGB->postOutputEvent(i, false);
            }

            // Recharge wait loop (RUN background + throttled charging overlay)
            TickType_t lastChargePost = 0;
            while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
                const TickType_t now = xTaskGetTickCount();
                if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
                    RGB->postOverlay(OverlayEvent::PWR_CHARGING);
                    lastChargePost = now;
                }
                DEBUG_PRINTF("[Device] [Batch] Recharging... Cap: %.2fV / Target: %.2fV ⏳\n",
                             discharger->readCapVoltage(), GO_THRESHOLD_RATIO);
                // Background stays RUN
                RGB->setRun();
                delay(200);
            }
        }

        // (Overcurrent check omitted—left as in your source)
    }

    // Background monitors
    stopTemperatureMonitor();

    heaterManager->disableAll();
    indicator->clearAll();
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
                self->heaterManager->disableAll();
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
                const bool state = device->heaterManager->getOutputState(i);
                device->indicator->setLED(i, state);
            }
        }
        vTaskDelay(delayTicks);
    }
}

void Device::updateLed() {
    if (CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK)) {
        for (uint8_t i = 1; i <= 10; i++) {
            const bool state = heaterManager->getOutputState(i);
            indicator->setLED(i, state);
        }
    }
}
