#include "Device.h"

// Map of output keys (0-indexed for outputs 1 to 10)
const char* outputKeys[10] = {
    OUT01_ACCESS_KEY,
    OUT02_ACCESS_KEY,
    OUT03_ACCESS_KEY,
    OUT04_ACCESS_KEY,
    OUT05_ACCESS_KEY,
    OUT06_ACCESS_KEY,
    OUT07_ACCESS_KEY,
    OUT08_ACCESS_KEY,
    OUT09_ACCESS_KEY,
    OUT10_ACCESS_KEY
};


Device::Device(ConfigManager* cfg,
               HeaterManager* heater,
               FanManager* fan,
               TempSensor* temp,
               CurrentSensor* current,
               Relay* relay,
               BypassMosfet* bypass,
               CpDischg* discharger,
               Indicator* ledIndicator,
               WiFiManager* wifi)
    : config(cfg),
      heaterManager(heater),
      fanManager(fan),
      tempSensor(temp),
      currentSensor(current),
      relayControl(relay),
      bypassFET(bypass),
      discharger(discharger),
      indicator(ledIndicator),      // ✅ Initialize Indicator
      wifiManager(wifi)             // ✅ Initialize WiFiManager
{}

void Device::begin() {
    currentState = DeviceState::Idle;

    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Device Manager ⚙️              #");
    DEBUG_PRINTLN("###########################################################");

    DEBUG_PRINTLN("[Device] Configuring system I/O pins 🧰");
    pinMode(DETECT_12V_PIN, INPUT);
    pinMode(READY_LED_PIN, OUTPUT);
    pinMode(POWER_OFF_LED_PIN, OUTPUT);
    digitalWrite(READY_LED_PIN, LOW);
    digitalWrite(POWER_OFF_LED_PIN, HIGH);

    DEBUG_PRINTLN("[Device] Waiting for 12V input... 🔋");
    while (digitalRead(DETECT_12V_PIN)) {
        delay(100);
    }

    DEBUG_PRINTLN("[Device] 12V Detected – Enabling input relay ✅");
    relayControl->turnOn();

    float dc = config->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);
    float capVoltage = readCapVoltage();
    DEBUG_PRINTF("[Device] Initial Capacitor Voltage: %.2fV 🧠\n", capVoltage);

    while (capVoltage < 0.78f * dc) {
        DEBUG_PRINTF("[Device] Charging... Cap Voltage: %.2fV / Target: %.2fV ⏳\n", capVoltage, 0.78f * dc);
        delay(500);
        capVoltage = readCapVoltage();
    }

    DEBUG_PRINTLN("[Device] Voltage threshold met ✅ Bypassing inrush resistor 🔄");
    bypassFET->enable();

    DEBUG_PRINTLN("[Device] Updating LED indicators 💡");
    digitalWrite(READY_LED_PIN, HIGH);
    digitalWrite(POWER_OFF_LED_PIN, LOW);

    DEBUG_PRINTLN("[Device] System READY for operation ✅");

    pinMode(POWER_ON_SWITCH_PIN, INPUT_PULLUP);
    DEBUG_PRINTLN("[Device] Waiting for user to press POWER ON button 🔘");
    while (digitalRead(POWER_ON_SWITCH_PIN)) {
        delay(50);
    }

    DEBUG_PRINTLN("[Device] POWER ON button pressed ▶️ Launching main loop");
    currentState = DeviceState::Running;
    StartLoop();

    DEBUG_PRINTLN("[Device] Main loop finished, proceeding to shutdown 🛑");
    shutdown();
}

void Device::loop() {
    // Reserved for monitoring tasks if needed
}

void Device::checkAllowedOutputs() {
    DEBUG_PRINTLN("[Device] Checking allowed outputs from preferences 🔍");

    for (uint8_t i = 0; i < 10; ++i) {
        allowedOutputs[i] = config->GetBool(outputKeys[i], false);
        DEBUG_PRINTF("[Device] OUT%02u => %s ✅\n", i + 1, allowedOutputs[i] ? "ENABLED" : "DISABLED");
    }
}

void Device::StartLoop() {
    startTemperatureMonitor();  // Start temperature monitoring in background

    DEBUG_PRINTLN("[Device] Starting Output Activation Cycle 🔁");
    currentState = DeviceState::Running;

    uint16_t onTime     = config->GetInt(ON_TIME_KEY, DEFAULT_ON_TIME);
    uint16_t offTime    = config->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
    bool ledFeedback    = config->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);

    while (currentState == DeviceState::Running) {
        for (uint8_t i = 0; i < 10; ++i) {
            if (currentState != DeviceState::Running) break;
            if (!allowedOutputs[i]) continue;

            DEBUG_PRINTF("[Device] Activating Output %u 🔥\n", i + 1);
            if (ledFeedback) indicator->setLED(i + 1, true);
            heaterManager->setOutput(i + 1, true);
            delay(onTime);

            float current = currentSensor->readCurrent();
            DEBUG_PRINTF("[Device] Current Sensor Reading: %.2fA ⚡\n", current);

            if (current > 20.0f) {
                DEBUG_PRINTLN("[Device] Overcurrent Detected! Aborting ⚠️");
                heaterManager->disableAll();
                if (ledFeedback) indicator->clearAll();
                currentState = DeviceState::Error;
                return;
            }

            heaterManager->setOutput(i + 1, false);
            if (ledFeedback) indicator->setLED(i + 1, false);
            delay(offTime);
        }
    }

    DEBUG_PRINTLN("[Device] Output loop exited gracefully 🛑");
    heaterManager->disableAll();
    if (ledFeedback) indicator->clearAll();
    currentState = DeviceState::Idle;
}

void Device::shutdown() {
    DEBUG_PRINTLN("-----------------------------------------------------------");
    DEBUG_PRINTLN("[Device] Initiating Shutdown Sequence 🔻");
    DEBUG_PRINTLN("-----------------------------------------------------------");

    currentState = DeviceState::Shutdown;

    DEBUG_PRINTLN("[Device] Turning OFF Main Relay 🔌");
    relayControl->turnOff();

    DEBUG_PRINTLN("[Device] Starting Capacitor Discharge ⚡");
    discharger->discharge();

    DEBUG_PRINTLN("[Device] Disabling Inrush Bypass MOSFET ⛔");
    bypassFET->disable();

    DEBUG_PRINTLN("[Device] Stopping Temperature Monitoring Task 🧊");
    tempSensor->stopTemperatureTask();

    DEBUG_PRINTLN("[Device] Updating Status LEDs 💡");
    digitalWrite(READY_LED_PIN, LOW);
    digitalWrite(POWER_OFF_LED_PIN, HIGH);

    DEBUG_PRINTLN("[Device] Shutdown Complete – System is Now OFF ✅");
    DEBUG_PRINTLN("-----------------------------------------------------------");
}

void Device::startTemperatureMonitor() {
    if (tempMonitorTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            Device::monitorTemperatureTask,
            "TempMonitorTask",
            2048,
            this,
            1,
            &tempMonitorTaskHandle,
            APP_CPU_NUM
        );
        DEBUG_PRINTLN("[Device] Temperature monitor started 🧪");
    }
}

void Device::monitorTemperatureTask(void* param) {
    Device* self = static_cast<Device*>(param);

    float threshold = self->config->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
    uint8_t sensorCount = self->tempSensor->getSensorCount();

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[Device] No temperature sensors found! Skipping monitoring ❌");
        vTaskDelete(nullptr);
        return;
    }

    self->tempSensor->startTemperatureTask(1000);
    DEBUG_PRINTF("[Device] Monitoring %u temperature sensors every 2s ⚙️\n", sensorCount);

    while (true) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            float temp = self->tempSensor->getTemperature(i);
            DEBUG_PRINTF("[Device] TempSensor[%u] = %.2f°C 🌡️\n", i, temp);

            if (temp >= threshold) {
                DEBUG_PRINTF("[Device] Overtemperature Detected! Sensor[%u] = %.2f°C ❌\n", i, temp);
                self->currentState = DeviceState::Error;
                self->heaterManager->disableAll();
                self->indicator->clearAll();
                vTaskDelete(nullptr);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
