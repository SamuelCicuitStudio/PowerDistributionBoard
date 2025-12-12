/**************************************************************
 * BusSampler.h
 *
 * Samples bus voltage and current together with a shared timestamp,
 * providing a synchronized history buffer for power/thermal estimation.
 **************************************************************/
#ifndef BUS_SAMPLER_H
#define BUS_SAMPLER_H

#include <Arduino.h>
#include "sensing/CurrentSensor.h"
#include "control/CpDischg.h"

class BusSampler {
public:
    struct Sample {
        uint32_t timestampMs;
        float    voltageV;
        float    currentA;
    };

    // Singleton-style accessor
    static BusSampler* Get();

    // Start sampling task. periodMs = sampling interval (default ~200 Hz -> 5ms).
    void begin(CurrentSensor* cs, CpDischg* cp, uint32_t periodMs = 5);

    // Get history since lastSeq (similar to CurrentSensor API).
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

private:
    BusSampler() = default;

    static void taskThunk(void* param);
    void taskLoop(uint32_t periodMs);
    void pushSample(uint32_t tsMs, float v, float i);

    CurrentSensor* currentSensor = nullptr;
    CpDischg*      cpDischg      = nullptr;

    static constexpr size_t BUS_HISTORY_SAMPLES = 256;
    Sample   _history[BUS_HISTORY_SAMPLES]{};
    uint32_t _historyHead = 0;
    uint32_t _historySeq  = 0;

    TaskHandle_t      taskHandle   = nullptr;
    SemaphoreHandle_t _mutex       = nullptr;
};

#define BUS_SAMPLER BusSampler::Get()

#endif // BUS_SAMPLER_H
