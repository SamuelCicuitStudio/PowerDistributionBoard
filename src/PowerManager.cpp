// PowerManager.cpp

#include "PowerManager.h"
#include "Config.h"

PowerManager* PowerManager::instance = nullptr;

// Pin lists
const int PowerManager::nichromePins[10] = {
    ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
    ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA010_E_PIN
};
const int PowerManager::floorLedPins[10] = {
    FL01_LED_PIN, FL02_LED_PIN, FL03_LED_PIN, FL04_LED_PIN, FL05_LED_PIN,
    FL06_LED_PIN, FL07_LED_PIN, FL08_LED_PIN, FL09_LED_PIN, FL10_LED_PIN
};

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
PowerManager::PowerManager(ConfigManager* config,
                           Logger*           log,
                           DallasTemperature* sensor)
  : Config(config),
    Log(log),
    Sensor(sensor),
    systemOn(false),
    systemOnwifi(false),
    ledFeedbackEnabled(false),
    onTime(config->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME)),
    offTime(config->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME)),
    measuredVoltage(0.0f),
    AcFreq(config->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY)),
    calibMax(1),

    // Task handles, initialized to nullptr
    startupHandle(nullptr),
    voltageHandle(nullptr),
    switchHandle(nullptr),
    tempHandle(nullptr),
    safetyHandle(nullptr),
    powerLossHandle(nullptr),
    capMaintHandle(nullptr),
    sequenceHandle(nullptr),

    chargeResistorOhs(config->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS))
{
    if (DEBUGMODE) {
        Serial.println(F("+++ PowerManager ctor +++"));
    }
    Sensor->begin();
    for (int i = 0; i < 4; ++i) {
        Sensor->getAddress(tempDeviceAddress[i], i);
    }
    instance = this;
}

//----------------------------------------------------------------------
// begin(): quick HW init and spawn startupTask
//----------------------------------------------------------------------
void PowerManager::begin() {
    // 1) Nichrome outputs & floor LEDs off
    for (auto p : nichromePins)   pinMode(p, OUTPUT), digitalWrite(p, LOW);
    for (auto p : floorLedPins)   pinMode(p, OUTPUT), digitalWrite(p, LOW);

    // 2) Bypass driver pins (UCC27524): ENA_E_PIN = enable, INA_E_PIN = PWM input
    pinMode(ENA_E_PIN, OUTPUT);   digitalWrite(ENA_E_PIN, LOW);
    pinMode(INA_E_PIN, OUTPUT);   digitalWrite(INA_E_PIN, LOW);

    // 3) PWM for bypass inrush control
    /*ledcSetup(BYPASS_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(INA_E_PIN, BYPASS_PWM_CHANNEL);
    ledcWrite(BYPASS_PWM_CHANNEL, 0);

    // 4) PWM for nichrome power level
    ledcSetup(OPT_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(INA_OPT_PWM_PIN, OPT_PWM_CHANNEL);
    ledcWrite(OPT_PWM_CHANNEL, CHANNEL_POWER_DUTY);*/

    // 5) Ready / Power-off LEDs
    pinMode(READY_LED_PIN,    OUTPUT); digitalWrite(READY_LED_PIN,    LOW);
    pinMode(POWER_OFF_LED_PIN, OUTPUT); digitalWrite(POWER_OFF_LED_PIN, HIGH);

    // 6) Inputs
    pinMode(POWER_ON_SWITCH_PIN, INPUT);
    lastState = digitalRead(POWER_ON_SWITCH_PIN);
    pinMode(DETECT_12V_PIN, INPUT_PULLDOWN);

    // 7) Launch non-blocking startup manager
  /*if (!startupHandle) 
    {
        xTaskCreate(startupTask, "StartupMgr", 8192, this, 4, &startupHandle);
    }*/
}

//----------------------------------------------------------------------
// startupTask: wait for 12 V, calibrate ADC, spawn monitors, then exit
//----------------------------------------------------------------------
void PowerManager::startupTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);

    // Wait for gate-drive rail (12 V)
    while (digitalRead(DETECT_12V_PIN) == HIGH) {
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    Serial.println("DETECTED 12V");

    // ADC peak calibration over one AC cycle
    TickType_t period = pdMS_TO_TICKS(1000 / self->AcFreq);
    TickType_t start  = xTaskGetTickCount();
    self->calibMax = 1;
    while (xTaskGetTickCount() - start < period) {
        int raw = analogRead(CAPACITOR_ADC_PIN);
        if (raw > self->calibMax) self->calibMax = raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    Serial.println("startVoltageMonitorTask");
    // Spawn always-on background monitors
    instance ->startVoltageMonitorTask();

// Spawn always-on monitors, but only if not already created
/*if (!self->tempHandle) 
{
    xTaskCreate(self->tempMonitorTask,"TempMon",4096,self,2,&self->tempHandle);
}

if (!self->safetyHandle) 
{
    xTaskCreate(self->safetyMonitorTask,"SafetyMon",4096,self,3,&self->safetyHandle);
}*/

#ifndef TEST_MODE
#ifdef NO_HARD_RESISTOR
if (!self->capMaintHandle)
 {
    xTaskCreate( self->capMaintenanceTask,"CapMaint",4096,self,1,&self->capMaintHandle);
}
#endif
#endif


    // Exit this one-shot task
   if(self->startupHandle)
    {
        vTaskDelete(&self->startupHandle);
    }

}

//----------------------------------------------------------------------
// toggleSystem(): start/stop heating & manage bypass PWM
//----------------------------------------------------------------------
void PowerManager::toggleSystem() {
    systemOn = !systemOn;
    
    if (systemOn) {
        // --- TURN ON ---
        // Enable bypass driver
        digitalWrite(ENA_E_PIN, HIGH);

    #ifndef NO_HARD_RESISTOR
        // Resistor-mode: leave bypass MOSFET on
        ledcWrite(BYPASS_PWM_CHANNEL,255);
    #else
        // Soft-inrush: full PWM on bypass MOSFET
        ledcWrite(BYPASS_PWM_CHANNEL, PWM_DUTY_CYCLE);
    #endif

        // Spawn the heating sequence task
        if (!sequenceHandle)
        {
            xTaskCreate(sequenceControlTask, "SeqCtrl",8192, this, 1, &sequenceHandle );
        }

    } else {
        // --- TURN OFF ---
        // 1) Kill the heating sequence
        if (sequenceHandle) {
            vTaskDelete(sequenceHandle);
            sequenceHandle = nullptr;
        }

        // 2) Disable bypass MOSFET
        digitalWrite(ENA_E_PIN, LOW);
        ledcWrite(BYPASS_PWM_CHANNEL, 0);

        // 3) Turn off all nichrome channels and LEDs
        for (auto p : nichromePins)   digitalWrite(p, LOW);
        for (auto p : floorLedPins)   digitalWrite(p, LOW);

        // 4) Show “power off” state
        digitalWrite(POWER_OFF_LED_PIN, HIGH);

        // 5) Restart one‐shot voltage monitor to update READY_LED
        startVoltageMonitorTask();
    }
}


//----------------------------------------------------------------------
// switchMonitorTask: debounce start/stop button
//----------------------------------------------------------------------
void PowerManager::switchMonitorTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    // Initialize lastState with current switch position
    self->lastState = digitalRead(POWER_ON_SWITCH_PIN);
    for (;;) {
        // Read the physical switch
        bool currentState = digitalRead(POWER_ON_SWITCH_PIN);
        // If the switch was just pressed, or a Wi-Fi toggle was requested:
        if ((currentState && !self->lastState) || self->systemOnwifi) {
            self->toggleSystem();
            // Simple debounce delay
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Remember this reading for the next loop
        self->lastState = currentState;
        // Poll at 500 ms intervals
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//----------------------------------------------------------------------
// tempMonitorTask: read DS18B20s every second
//----------------------------------------------------------------------
void PowerManager::tempMonitorTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    for (;;) {
        self->Sensor->requestTemperatures();
        for (int i = 0; i < 4; ++i) {
            self->Temps[i] = self->Sensor->getTempC(self->tempDeviceAddress[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//----------------------------------------------------------------------
// capMaintenanceTask: trickle-charge via bypass PWM when idle
//----------------------------------------------------------------------
#ifndef TEST_MODE
#ifdef NO_HARD_RESISTOR
void PowerManager::capMaintenanceTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    int baseDuty = PWM_DUTY_CYCLE / 10;  // 10%
    float scale = DEFAULT_CHARGE_RESISTOR_OHMS / self->chargeResistorOhs;
    int maintainDuty = constrain(int(baseDuty * scale), 0, PWM_DUTY_CYCLE);

    for (;;) {
        if (!self->systemOn ) {
            int raw = analogRead(CAPACITOR_ADC_PIN);
            float pct = float(raw) / self->calibMax * 100.0f;
            ledcWrite(BYPASS_PWM_CHANNEL,
                      (pct < CHARGE_THRESHOLD_PERCENT) ? maintainDuty : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif
#endif

// voltageMonitorTask: average ADC, light READY when ≥80% then spawn SWITCH
//----------------------------------------------------------------------
void PowerManager::voltageMonitorTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    constexpr int SAMPLES = 20;

    for (;;) {
        long sum = 0;
        for (int i = 0; i < SAMPLES; ++i) {
            sum += analogRead(CAPACITOR_ADC_PIN);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        float avg = sum / float(SAMPLES);
        self->measuredVoltage =
            (avg / 4095.0f) * 3.3f * ((470000.0f + 4700.0f) / 4700.0f);

        float pct = (avg / float(self->calibMax)) * 100.0f;
        if (pct >= 80.0f) {
            digitalWrite(READY_LED_PIN,    HIGH);
            digitalWrite(POWER_OFF_LED_PIN, LOW);

            // Now allow user to press START
            if (!self->switchHandle)
            {
                xTaskCreate(self->switchMonitorTask,"SwitchMon", 2048, self, 2,&self->switchHandle);
            }

            // Exit this one-shot voltage monitor
            if (self->voltageHandle)
            {
                vTaskDelete(&self->voltageHandle);
            }
           
        } else {
            digitalWrite(READY_LED_PIN,    LOW);
            digitalWrite(POWER_OFF_LED_PIN, HIGH);
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

//----------------------------------------------------------------------
// safetyMonitorTask: shut off on over-temperature
//----------------------------------------------------------------------
void PowerManager::safetyMonitorTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    float maxT = self->Config->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
    for (;;) {
        for (int i = 0; i < 4; ++i) {
            if (self->Temps[i] > maxT && self->systemOn) {
                self->Log->logError("Over-temp → stopping");
                self->toggleSystem();
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//----------------------------------------------------------------------
// powerLossTask: shut off if 12V lost
//----------------------------------------------------------------------
void PowerManager::powerLossTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    for (;;) {
        if (digitalRead(DETECT_12V_PIN) == LOW && self->systemOn ) {
            self->Log->logError("12V lost → stopping");
            self->toggleSystem();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//----------------------------------------------------------------------
// sequenceControlTask: per-channel heating loop
//----------------------------------------------------------------------
void PowerManager::sequenceControlTask(void* pv) {
    auto* self = static_cast<PowerManager*>(pv);
    for (;;) {
        if (self->systemOn) {
            for (int i = 0; i < 10; ++i) {
                digitalWrite(nichromePins[i], HIGH);
                if (self->ledFeedbackEnabled)
                    digitalWrite(floorLedPins[i], HIGH);
                vTaskDelay(pdMS_TO_TICKS(self->onTime));
                digitalWrite(nichromePins[i], LOW);
                if (self->ledFeedbackEnabled)
                    digitalWrite(floorLedPins[i], LOW);
                vTaskDelay(pdMS_TO_TICKS(self->offTime));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

//----------------------------------------------------------------------
// reportStatus(): log key metrics
//----------------------------------------------------------------------
void PowerManager::reportStatus() {
    Log->logInfo("On:" + String(systemOn) +
                 " V:" + String(measuredVoltage, 2) + "V" +
                 " 12V:" + String(digitalRead(DETECT_12V_PIN)));
    for (int i = 0; i < 4; ++i) {
        Log->logInfo("T" + String(i) + ":" + String(Temps[i], 1) + "C");
    }
    Log->logInfo("Cycle:" + String(onTime) + "/" + String(offTime) + "ms");
}

//----------------------------------------------------------------------
// getVoltage(): return volts
//----------------------------------------------------------------------
float PowerManager::getVoltage() const {
    return measuredVoltage;
}

//----------------------------------------------------------------------
// getVoltagePercentage(): raw ADC % of calibMax
//----------------------------------------------------------------------
float PowerManager::getVoltagePercentage() const {
    int raw = analogRead(CAPACITOR_ADC_PIN);
    return float(raw) / calibMax * 100.0f;
}

//----------------------------------------------------------------------
// getTemperatureArray(): pointer to Temps[]
//----------------------------------------------------------------------
const float* PowerManager::getTemperatureArray() const {
    return Temps;
}

//----------------------------------------------------------------------
// startVoltageMonitorTask(): pin to core 1
//----------------------------------------------------------------------
void PowerManager::startVoltageMonitorTask() {
    if(!voltageHandle)
    {
        xTaskCreatePinnedToCore(voltageMonitorTask, "VoltMon",4096,this,1, &voltageHandle,  1);
    }    
}

//----------------------------------------------------------------------
// setCycleTime(): update on/off times and save to NVS
//----------------------------------------------------------------------
void PowerManager::setCycleTime(uint32_t onMs, uint32_t offMs) {
    onTime  = onMs;                              // update runtime value
    offTime = offMs;                             // update runtime value
    Config->PutInt(ON_TIME_KEY,  static_cast<int>(onMs));   // persist
    Config->PutInt(OFF_TIME_KEY, static_cast<int>(offMs));  // persist
}
//----------------------------------------------------------------------
// setLedFeedback(): turn floor‐LED feedback on/off and persist
//----------------------------------------------------------------------
void PowerManager::setLedFeedback(bool enabled) {
    ledFeedbackEnabled = enabled;                          // update runtime flag
    Config->PutBool(LED_FEEDBACK_KEY, enabled);            // save preference
}
