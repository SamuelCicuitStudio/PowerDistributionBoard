#include "sensing/BusSampler.h"
#include <math.h>

BusSampler* BusSampler::Get() {
    static BusSampler instance;
    return &instance;
}

void BusSampler::begin(CurrentSensor* cs, CpDischg* cp, uint32_t periodMs) {
    currentSensor = cs;
    cpDischg      = cp;

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }

    if (taskHandle != nullptr) {
        return;
    }

    if (periodMs == 0) periodMs = 5; // ~200 Hz

    BaseType_t ok = xTaskCreate(
        BusSampler::taskThunk,
        "BusSampler",
        3072,
        reinterpret_cast<void*>(periodMs),
        2,
        &taskHandle
    );

    if (ok != pdPASS) {
        taskHandle = nullptr;
    }
}

void BusSampler::taskThunk(void* param) {
    uint32_t periodMs = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    BusSampler* self = BusSampler::Get();
    if (self) {
        self->taskLoop(periodMs);
    }
    vTaskDelete(nullptr);
}

void BusSampler::taskLoop(uint32_t periodMs) {
    const TickType_t delayTicks = pdMS_TO_TICKS(periodMs);
    for (;;) {
        const uint32_t ts = millis();
        float v = NAN;
        float i = NAN;

        if (cpDischg) {
            v = cpDischg->sampleVoltageNow();
        }
        if (currentSensor) {
            i = currentSensor->readCurrent(); // uses existing averaging; acceptable for sync window
        }

        pushSample(ts, v, i);
        vTaskDelay(delayTicks);
    }
}

void BusSampler::pushSample(uint32_t tsMs, float v, float i) {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        uint32_t idx = _historyHead % BUS_HISTORY_SAMPLES;
        _history[idx].timestampMs = tsMs;
        _history[idx].voltageV    = v;
        _history[idx].currentA    = i;
        _historyHead++;
        _historySeq++;
        xSemaphoreGive(_mutex);
    }
}

size_t BusSampler::getHistorySince(uint32_t lastSeq,
                                   Sample* out,
                                   size_t maxOut,
                                   uint32_t& newSeq) const
{
    if (!out || maxOut == 0) {
        newSeq = lastSeq;
        return 0;
    }

    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t seqNow = _historySeq;
    if (seqNow == 0) {
        xSemaphoreGive(_mutex);
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t maxSpan = (seqNow > BUS_HISTORY_SAMPLES) ? BUS_HISTORY_SAMPLES : seqNow;
    const uint32_t minSeq  = seqNow - maxSpan;

    if (lastSeq < minSeq) lastSeq = minSeq;
    if (lastSeq > seqNow) lastSeq = seqNow;

    uint32_t available = seqNow - lastSeq;
    if (available > maxOut) available = maxOut;

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t sSeq = lastSeq + i;
        uint32_t idx  = sSeq % BUS_HISTORY_SAMPLES;
        out[i] = _history[idx];
    }

    newSeq = lastSeq + available;
    xSemaphoreGive(_mutex);
    return (size_t)available;
}
