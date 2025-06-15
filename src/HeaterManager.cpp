#include "HeaterManager.h"

void HeaterManager::begin(ConfigManager* cfg) {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                Starting Heater Manager                  #");
    DEBUG_PRINTLN("###########################################################");

    config = cfg;

    // Set ENA pins as outputs and disable all (LOW = inactive for UCC27524ADR)
    for (uint8_t i = 0; i < 10; ++i) {
        pinMode(enaPins[i], OUTPUT);
        digitalWrite(enaPins[i], LOW);
    }
    DEBUG_PRINTLN("All ENA outputs initialized and disabled 🔌");

    // Setup PWM for INA_OPT
    ledcSetup(INA_OPT_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(INA_OPT_PWM_PIN, INA_OPT_PWM_CHANNEL);
    DEBUG_PRINTLN("PWM channel configured for INA_OPT ⚙️");

    // Load target voltage from preferences (fallback to default if not set)
    float desiredVoltage = config->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, DEFAULT_DESIRED_OUTPUT_VOLTAGE);
    setPower(desiredVoltage);
    DEBUG_PRINTF("Initial power set for %.1fV output ⚡️\n", desiredVoltage);
}

void HeaterManager::setOutput(uint8_t index, bool enable) {
    if (index >= 1 && index <= 10) {
        digitalWrite(enaPins[index - 1], enable ? HIGH : LOW);
        DEBUG_PRINTF("Output #%d %s ✅\n", index, enable ? "enabled" : "disabled");
    }
}

void HeaterManager::disableAll() {
    for (uint8_t i = 0; i < 10; ++i) {
        digitalWrite(enaPins[i], LOW);  // LOW = disable output
    }
    ledcWrite(INA_OPT_PWM_CHANNEL, 0);  // Turn off PWM
    DEBUG_PRINTLN("All outputs disabled and PWM off 📴");
}

void HeaterManager::setPower(float desiredVoltage) {
    float dcMax = config->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);
    if (dcMax <= 0.0f) {
        ledcWrite(INA_OPT_PWM_CHANNEL, 0); // Avoid divide-by-zero
        DEBUG_PRINTLN("Invalid max DC voltage, PWM set to 0 ❌");
        return;
    }

    float ratio = constrain(desiredVoltage / dcMax, 0.0f, 1.0f);
    uint8_t duty = static_cast<uint8_t>(ratio * 255);
    ledcWrite(INA_OPT_PWM_CHANNEL, duty);
    DEBUG_PRINTF("PWM duty set to %d (%.1f%% of %.1fV) 🔋\n", duty, ratio * 100.0f, dcMax);
}
