#include "control/CpDischg.h"
#include <float.h>
#include <math.h>

// ---------------------------------------

// Monitor behavior constants
static constexpr uint16_t MONITOR_WINDOW_MS       = 300;   // integration window
static constexpr uint16_t MONITOR_SAMPLE_DELAY_MS = 2;     // between samples
static constexpr uint16_t MONITOR_STALE_MS        = 1000;  // if no update >1s â†’ restart

// ============================================================================
// Public API
// ============================================================================

void CpDischg::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#               Starting CpDischarge  Manager           #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    pinMode(CAPACITOR_ADC_PIN, INPUT);
    loadEmpiricalGainFromConfig();

    // Init mutex once
    if (voltageMutex == nullptr) {
        voltageMutex = xSemaphoreCreateMutex();
        if (!voltageMutex) {
            DEBUG_PRINTLN("[CpDischg] Failed to create voltage mutex");
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
            lastRawAdc        = raw;
            lastSampleTick    = xTaskGetTickCount();
            xSemaphoreGive(voltageMutex);
        } else {
            lastMinBusVoltage = v;
            lastRawAdc        = raw;
            lastSampleTick    = xTaskGetTickCount();
        }
    }

    // Ensure monitor task exists and is healthy.
    ensureMonitorTask();
}

void CpDischg::discharge() {
    for (;;) {
        float v = readCapVoltage();
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.2f V âš¡\n", v);

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
    float      v   = lastMinBusVoltage;
    TickType_t age = 0;
    TickType_t now = xTaskGetTickCount();

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
        if ((now - lastStaleWarnTick) > pdMS_TO_TICKS(MONITOR_STALE_MS)) {
            DEBUG_PRINTLN("[CpDischg] Stale voltage reading detected  ensure monitor running");
            lastStaleWarnTick = now;
        }

        const uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
        const float freshV = adcCodeToBusVolts(raw);
        const TickType_t sampleTick = xTaskGetTickCount();
        if (isfinite(freshV)) {
            if (voltageMutex &&
                xSemaphoreTake(voltageMutex, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                lastMinBusVoltage = freshV;
                lastRawAdc        = raw;
                lastSampleTick    = sampleTick;
                xSemaphoreGive(voltageMutex);
            } else {
                lastMinBusVoltage = freshV;
                lastRawAdc        = raw;
                lastSampleTick    = sampleTick;
            }
            v = freshV;
        }
        ensureMonitorTask();
    }

    return v;
}

float CpDischg::readCapAdcScaled() {
    uint16_t raw = lastRawAdc;
    if (voltageMutex &&
        xSemaphoreTake(voltageMutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        raw = lastRawAdc;
        xSemaphoreGive(voltageMutex);
    }
    return static_cast<float>(raw) / 100.0f;
}

float CpDischg::sampleVoltageNow() {
    uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
    return adcCodeToBusVolts(raw);
}

uint16_t CpDischg::sampleAdcRaw() const {
    return analogRead(CAPACITOR_ADC_PIN);
}

size_t CpDischg::getHistorySince(uint32_t lastSeq,
                                 Sample* out,
                                 size_t maxOut,
                                 uint32_t& newSeq) const
{
    if (!out || maxOut == 0) {
        newSeq = lastSeq;
        return 0;
    }

    if (!const_cast<CpDischg*>(this)->voltageMutex ||
        xSemaphoreTake(const_cast<CpDischg*>(this)->voltageMutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t seqNow = _historySeq;
    if (seqNow == 0) {
        xSemaphoreGive(voltageMutex);
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t maxSpan = (seqNow > VOLT_HISTORY_SAMPLES)
                           ? VOLT_HISTORY_SAMPLES
                           : seqNow;
    const uint32_t minSeq = seqNow - maxSpan;

    if (lastSeq < minSeq) lastSeq = minSeq;
    if (lastSeq > seqNow) lastSeq = seqNow;

    uint32_t available = seqNow - lastSeq;
    if (available > maxOut) available = maxOut;

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t sSeq = lastSeq + i;
        uint32_t idx  = sSeq % VOLT_HISTORY_SAMPLES;
        out[i] = _history[idx];
    }

    newSeq = lastSeq + available;
    xSemaphoreGive(voltageMutex);
    return (size_t)available;
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
        DEBUG_PRINTLN("[CpDischg] Monitor task not valid  restarting");
    }

    BaseType_t ok = xTaskCreate(
        CpDischg::monitorTaskThunk,
        "CapVMon",
        4096,
        this,
        3,
        &monitorTaskHandle
    );

    if (ok != pdPASS) {
        monitorTaskHandle = nullptr;
        DEBUG_PRINTLN("[CpDischg] Failed to start monitor task ");
    } else {
        DEBUG_PRINTLN("[CpDischg] Monitor task (re)started ");
    }
}

// ============================================================================
// Background Monitor Task
// ============================================================================
void CpDischg::monitorTaskThunk(void* param) {
    auto* self = static_cast<CpDischg*>(param);
    self->monitorTask(MONITOR_WINDOW_MS, MONITOR_SAMPLE_DELAY_MS, MONITOR_STALE_MS);
    self->monitorTaskHandle = nullptr;
    DEBUG_PRINTLN("[CpDischg] monitorTask exited unexpectedly ");
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
        uint16_t   minRaw = 0;

        // Collect samples for this window, tracking minimum bus voltage.
        while ((xTaskGetTickCount() - start) < windowTicks) {
            uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
            float v = adcCodeToBusVolts(raw);

            // Push sample into history with timestamp.
            if (isfinite(v)) {
                const uint32_t idx = _historyHead % VOLT_HISTORY_SAMPLES;
                _history[idx].timestampMs = millis();
                _history[idx].voltageV    = v;
                _historyHead++;
                _historySeq++;
            }

            if (v < minV) {
                minV = v;
                minRaw = raw;
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
            lastRawAdc        = minRaw;
            lastSampleTick    = xTaskGetTickCount();
            xSemaphoreGive(voltageMutex);
        } else {
            lastMinBusVoltage = minV;
            lastRawAdc        = minRaw;
            lastSampleTick    = xTaskGetTickCount();
        }
    }
}

// ============================================================================
// ADC code -> bus volts
// ============================================================================
float CpDischg::adcCodeToBusVolts(uint16_t raw) const {
    const float v_adc = adcCodeToAdcVolts(raw);

    // Empirical-only mapping: Vbus = gain * Vadc + offset.
    float gain = empiricalGain;
    if (!isfinite(gain)) {
        gain = CAP_EMP_GAIN;
    }
    if (gain < CAP_EMP_GAIN_MIN) gain = CAP_EMP_GAIN_MIN;
    if (gain > CAP_EMP_GAIN_MAX) gain = CAP_EMP_GAIN_MAX;

    return v_adc * gain + CAP_EMP_OFFSET;
}

float CpDischg::adcCodeToAdcVolts(uint16_t raw) const {
    int32_t correctedRaw = static_cast<int32_t>(raw) - ADC_OFFSET;
    if (correctedRaw < 0) correctedRaw = 0;
    return (static_cast<float>(correctedRaw) / ADC_MAX) * ADC_REF_VOLTAGE;
}

void CpDischg::setEmpiricalGain(float gain, bool persist) {
    if (!isfinite(gain)) {
        return;
    }
    float g = gain;
    if (g < CAP_EMP_GAIN_MIN) g = CAP_EMP_GAIN_MIN;
    if (g > CAP_EMP_GAIN_MAX) g = CAP_EMP_GAIN_MAX;

    empiricalGain = g;

    if (persist && CONF) {
        CONF->PutFloat(CP_EMP_GAIN_KEY, g);
    }
}

void CpDischg::loadEmpiricalGainFromConfig() {
    float g = CAP_EMP_GAIN;
    if (CONF) {
        g = CONF->GetFloat(CP_EMP_GAIN_KEY, DEFAULT_CAP_EMP_GAIN);
    }

    if (!isfinite(g) || g < CAP_EMP_GAIN_MIN || g > CAP_EMP_GAIN_MAX) {
        g = CAP_EMP_GAIN;
    }

    empiricalGain = g;
}
