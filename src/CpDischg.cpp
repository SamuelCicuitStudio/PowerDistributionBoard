#include "CpDischg.h"

void CpDischg::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting CPDis Manager ⚙️               #");
    DEBUG_PRINTLN("###########################################################");

    pinMode(CAPACITOR_ADC_PIN, INPUT);
    //startCapVoltageTask();
    DEBUG_PRINTLN("[CpDischg] Ready to discharge using heater banks 🔥");
}

void CpDischg::discharge() {
    DEBUG_PRINTLN("[CpDischg] Starting discharge cycle 🚨");
    startCapVoltageTask();

    // Monitor and discharge
    while (true) {
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.2fV ⚡\n", g_capVoltage);

        if (g_capVoltage <= SAFE_VOLTAGE_THRESHOLD) {
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
    stopCapVoltageTask();
    DEBUG_PRINTLN("[CpDischg] All heater outputs disabled 📴");
}

void CpDischg::startCapVoltageTask() {
    if (capVoltageTaskHandle != nullptr) return;  // Prevent duplicate tasks

    xTaskCreatePinnedToCore(
        [](void* param) {
            CpDischg* self = static_cast<CpDischg*>(param);  // ⬅️ Access Device instance
            const uint16_t samples = 64;
            const TickType_t delayTicks = pdMS_TO_TICKS(CAP_VOLTAGE_TASK_DELAY_MS);  // Fixed 200ms interval

            while (true) {
                uint32_t total = 0;
                for (uint16_t i = 0; i < samples; ++i) {
                    total += analogRead(CAPACITOR_ADC_PIN);
                    delayMicroseconds(CAP_VOLTAGE_TASK_DELAY_MS);
                }

                float avgADC = total / static_cast<float>(samples);
                float voltage = (avgADC / ADC_MAX) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;

                self->g_capVoltage = voltage;  // ⬅️ Update member

                //DEBUG_PRINTF("[CapTask] Avg ADC: %.2f, Voltage: %.2fV 🧪\n", avgADC, voltage);
                vTaskDelay(delayTicks);
            }
        },
        "CapVoltageTask",
        CAP_VOLTAGE_TASK_STACK_SIZE,// Increased stack size to avoid canary crash
        this,                    // Pass 'this' as parameter
        CAP_VOLTAGE_TASK_PRIORITY,
        &capVoltageTaskHandle,
        CAP_VOLTAGE_TASK_CORE
    );
}

void CpDischg::stopCapVoltageTask() {
    if (capVoltageTaskHandle != nullptr) {
        vTaskDelete(capVoltageTaskHandle);
        capVoltageTaskHandle = nullptr;
    }
}

float CpDischg::readCapVoltage() {
    return g_capVoltage;
}
