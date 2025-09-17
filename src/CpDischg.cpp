#include "CpDischg.h"

// -------- Safe defaults if not provided elsewhere --------
#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE         3.3f
#endif
#ifndef ADC_MAX
#define ADC_MAX                 4095.0f
#endif
#ifndef VOLTAGE_DIVIDER_RATIO
#define VOLTAGE_DIVIDER_RATIO   100.0f
#endif
#ifndef SAFE_VOLTAGE_THRESHOLD
#define SAFE_VOLTAGE_THRESHOLD  5.0f
#endif
#ifndef CAP_VOLTAGE_TASK_STACK_SIZE
#define CAP_VOLTAGE_TASK_STACK_SIZE   3072
#endif
#ifndef CAP_VOLTAGE_TASK_PRIORITY
#define CAP_VOLTAGE_TASK_PRIORITY     1
#endif
#ifndef CAP_VOLTAGE_TASK_CORE
#define CAP_VOLTAGE_TASK_CORE         1
#endif
#ifndef CAP_VOLTAGE_TASK_DELAY_MS
#define CAP_VOLTAGE_TASK_DELAY_MS     200
#endif
// ---------------------------------------------------------

void CpDischg::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting CPDis Manager ⚙️               #");
    DEBUG_PRINTLN("###########################################################");

    pinMode(CAPACITOR_ADC_PIN, INPUT);
    // startCapVoltageTask(); // keep disabled here if you start it elsewhere
    DEBUG_PRINTLN("[CpDischg] Ready to discharge using heater banks 🔥");
}

void CpDischg::discharge() {
    DEBUG_PRINTLN("[CpDischg] Starting discharge cycle 🚨");
    startCapVoltageTask();

    while (true) {
        float v = g_capVoltage;
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.2fV ⚡\n", v);

        if (v <= SAFE_VOLTAGE_THRESHOLD) {
            DEBUG_PRINTLN("[CpDischg] Capacitor discharged safely ✅");
            break;
        }

        if (heaterManager) {
            for (int i = 1; i <= 10; ++i) {
                heaterManager->setOutput(i, true);
                delay(20);
                heaterManager->setOutput(i, false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (heaterManager) {
        heaterManager->disableAll();
    }
    stopCapVoltageTask();
    DEBUG_PRINTLN("[CpDischg] All heater outputs disabled 📴");
}

void CpDischg::startCapVoltageTask() {
    if (capVoltageTaskHandle != nullptr) return;

    xTaskCreatePinnedToCore(
        [](void* param) {
            auto* self = static_cast<CpDischg*>(param);
            const TickType_t delayTicks = pdMS_TO_TICKS(CAP_VOLTAGE_TASK_DELAY_MS);

            for (;;) {
                float v = self->measureOnceWithRelayGate();
                self->g_capVoltage = v;
                // DEBUG_PRINTF("[CapTask] One-shot ADC voltage: %.2fV 🧪\n", v);
                vTaskDelay(delayTicks);
            }
        },
        "CapVoltageTask",
        CAP_VOLTAGE_TASK_STACK_SIZE,
        this,
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

float CpDischg::measureOnceWithRelayGate() {
    // If user asked to bypass relay gating, or no relay provided → just read once.
    if (bypassRelayGate || !relay) {
        uint16_t rawNR = analogRead(CAPACITOR_ADC_PIN);
        return (rawNR / ADC_MAX) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
    }

    // Relay gating path:
    bool wasOn = relay->isOn();
    relay->turnOff();
    vTaskDelay(pdMS_TO_TICKS(10)); // settle

    uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
    float v = (raw / ADC_MAX) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;

    if (wasOn) relay->turnOn();
    else       relay->turnOff();

    return v;
}
