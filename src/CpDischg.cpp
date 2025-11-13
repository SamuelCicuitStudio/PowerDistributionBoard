#include "CpDischg.h"
#include <float.h>

// ---------------------------------------

// Monitor behavior constants
static constexpr uint16_t MONITOR_WINDOW_MS       = 300;   // integration window
static constexpr uint16_t MONITOR_SAMPLE_DELAY_MS = 2;     // between samples
static constexpr uint16_t MONITOR_STALE_MS        = 1000;  // if no update >1s → restart

// ============================================================================
// Public API
// ============================================================================

void CpDischg::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#               Starting CpDischarge  Manager 🌡️          #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    pinMode(CAPACITOR_ADC_PIN, INPUT);

    // Init mutex once
    if (voltageMutex == nullptr) {
        voltageMutex = xSemaphoreCreateMutex();
        if (!voltageMutex) {
            DEBUG_PRINTLN("[CpDischg] Failed to create voltage mutex ❌");
        }
    }

    // Seed cached voltage with a single immediate measurement.
    {
        uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
        float v = adcCodeToBusVolts(raw);

        if (voltageMutex &&
            xSemaphoreTake(voltageMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            lastMinBusVoltage = v;
            lastSampleTick    = xTaskGetTickCount();
            xSemaphoreGive(voltageMutex);
        } else {
            lastMinBusVoltage = v;
            lastSampleTick    = xTaskGetTickCount();
        }
    }

    // Ensure monitor task exists and is healthy.
    ensureMonitorTask();
}

void CpDischg::discharge() {
    for (;;) {
        float v = readCapVoltage();
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.2f V ⚡\n", v);

        if (v <= SAFE_VOLTAGE_THRESHOLD) {
            break;
        }

        if (WIRE) {
            for (int i = 1; i <= 10; ++i) {
                WIRE->setOutput(i, true);
                delay(20);
                WIRE->setOutput(i, false);
            }
        }

        delay(100);
    }

    if (WIRE) {
        WIRE->disableAll();
    }
}

float CpDischg::readCapVoltage() {
    float      v    = lastMinBusVoltage;
    TickType_t age  = 0;
    TickType_t now  = xTaskGetTickCount();

    if (voltageMutex &&
        xSemaphoreTake(voltageMutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        v   = lastMinBusVoltage;
        age = now - lastSampleTick;
        xSemaphoreGive(voltageMutex);
    } else {
        age = now - lastSampleTick;
    }

    if (age > pdMS_TO_TICKS(MONITOR_STALE_MS)) {
        DEBUG_PRINTLN("[CpDischg] Stale voltage reading detected → ensure monitor running");
        ensureMonitorTask();
    }

    return v;
}

// ============================================================================
// Internal: Ensure monitor task is running
// ============================================================================
void CpDischg::ensureMonitorTask() {
    if (monitorTaskHandle != nullptr) {
        eTaskState st = eTaskGetState(monitorTaskHandle);
        if (st != eDeleted && st != eInvalid) {
            return; // Healthy
        }
        monitorTaskHandle = nullptr;
        DEBUG_PRINTLN("[CpDischg] Monitor task not valid → restarting");
    }

    BaseType_t ok = xTaskCreate(
        CpDischg::monitorTaskThunk,
        "CapVMon",
        2048,
        this,
        3,
        &monitorTaskHandle
    );

    if (ok != pdPASS) {
        monitorTaskHandle = nullptr;
        DEBUG_PRINTLN("[CpDischg] Failed to start monitor task ❌");
    } else {
        DEBUG_PRINTLN("[CpDischg] Monitor task (re)started ✅");
    }
}

// ============================================================================
// Background Monitor Task
// ============================================================================
void CpDischg::monitorTaskThunk(void* param) {
    auto* self = static_cast<CpDischg*>(param);
    self->monitorTask(MONITOR_WINDOW_MS, MONITOR_SAMPLE_DELAY_MS, MONITOR_STALE_MS);
    self->monitorTaskHandle = nullptr;
    DEBUG_PRINTLN("[CpDischg] monitorTask exited unexpectedly ❌");
    vTaskDelete(nullptr);
}

void CpDischg::monitorTask(uint16_t windowMs,
                           uint16_t sampleDelayMs,
                           uint16_t /*staleWatchdogMs*/)
{
    const TickType_t windowTicks = pdMS_TO_TICKS(windowMs);
    const TickType_t delayTicks  = pdMS_TO_TICKS(sampleDelayMs);

    for (;;) {
        TickType_t start = xTaskGetTickCount();
        float      minV  = FLT_MAX;

        // Collect samples for this window, tracking minimum bus voltage.
        while ((xTaskGetTickCount() - start) < windowTicks) {
            uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
            float v = adcCodeToBusVolts(raw);

            if (v < minV) {
                minV = v;
            }

            vTaskDelay(delayTicks);
        }

        if (minV == FLT_MAX || !isfinite(minV)) {
            continue;
        }

        if (voltageMutex &&
            xSemaphoreTake(voltageMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            lastMinBusVoltage = minV;
            lastSampleTick    = xTaskGetTickCount();
            xSemaphoreGive(voltageMutex);
        } else {
            lastMinBusVoltage = minV;
            lastSampleTick    = xTaskGetTickCount();
        }
    }
}

// ============================================================================
// ADC code -> bus volts
// ============================================================================
float CpDischg::adcCodeToBusVolts(uint16_t raw) const {
    // Offset-correct the raw code
    int32_t correctedRaw = static_cast<int32_t>(raw) - ADC_OFFSET;
    if (correctedRaw < 0) correctedRaw = 0;

    // Convert to ADC pin voltage
    const float v_adc = (static_cast<float>(correctedRaw) / ADC_MAX) * ADC_REF_VOLTAGE;

    // Scale up to bus voltage using divider and op-amp gain:
    // Vbus = Vadc * VOLTAGE_SCALE  (see CpDischg.h)
    return v_adc * VOLTAGE_SCALE;
}
