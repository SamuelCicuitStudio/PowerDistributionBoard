#include "CpDischg.h"

void CpDischg::begin(HeaterManager* heater) {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting CPDis Manager ⚙️               #");
    DEBUG_PRINTLN("###########################################################");

    heaterManager = heater;
    pinMode(CAPACITOR_ADC_PIN, INPUT);
    DEBUG_PRINTLN("[CpDischg] Ready to discharge using heater banks 🔥");
}

void CpDischg::discharge() {
    DEBUG_PRINTLN("[CpDischg] Starting discharge cycle 🚨");

    // Monitor and discharge
    while (true) {
        float voltage = readCapVoltage();
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.2fV ⚡\n", voltage);

        if (voltage <= SAFE_VOLTAGE_THRESHOLD) {
            DEBUG_PRINTLN("[CpDischg] Capacitor discharged safely ✅");
            break;
        }

        delay(100);  // Wait before applying next discharge pulse

        // Sequentially pulse each heater to safely bleed energy
        for (int i = 1; i <= 10; ++i) {
            heaterManager->setOutput(i, true);
            delay(20);  // Short pulse
            heaterManager->setOutput(i, false);
        }
    }

    heaterManager->disableAll();
    DEBUG_PRINTLN("[CpDischg] All heater outputs disabled 📴");
}

