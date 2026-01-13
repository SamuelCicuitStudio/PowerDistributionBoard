/**************************************************************
 * BusSampler.h
 *
 * Samples bus voltage and derived current together with a shared timestamp,
 * providing a synchronized history buffer for power/thermal estimation.
 **************************************************************/
#ifndef BUS_SAMPLER_H
#define BUS_SAMPLER_H

#include <Arduino.h>
#include <CurrentSensor.hpp>
#include <CpDischg.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class NtcSensor;

class BusSampler {
public:
    struct Sample {
        uint32_t timestampMs;
        float    voltageV;
        float    currentA;
    };

    struct SyncSample {
        uint32_t timestampMs = 0;
        float    voltageV    = NAN;
        float    currentA    = NAN;
        float    tempC       = NAN;
        float    ntcVolts    = NAN;
        float    ntcOhm      = NAN;
        uint16_t ntcAdc      = 0;
        bool     ntcValid    = false;
        bool     pressed     = false;
    };

    // Singleton-style accessor
    static BusSampler* Get();

    // Start sampling task. periodMs = sampling interval (default ~200 Hz -> 5ms).
    void begin(CurrentSensor* cs, CpDischg* cp, uint32_t periodMs = 5);
    void attachNtc(NtcSensor* ntc);

    // On-demand sync sample for calibration (V, I, and NTC temp).
    bool sampleNow(SyncSample& out);

    // Get history since lastSeq (similar to CurrentSensor API).
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

    // Record a synchronized sample into history (e.g., per-packet pulse).
    void recordSample(uint32_t tsMs, float voltageV, float currentA);

private:
    BusSampler() = default;

    static void taskThunk(void* param);
    void taskLoop(uint32_t periodMs);
    void pushSample(uint32_t tsMs, float v, float i);

    CurrentSensor* currentSensor = nullptr;
    CpDischg*      cpDischg      = nullptr;
    NtcSensor*     ntcSensor     = nullptr;

    static constexpr size_t BUS_HISTORY_SAMPLES = 256;
    Sample   _history[BUS_HISTORY_SAMPLES]{};
    uint32_t _historyHead = 0;
    uint32_t _historySeq  = 0;

    TaskHandle_t      taskHandle   = nullptr;
    SemaphoreHandle_t _mutex       = nullptr;
};

#define BUS_SAMPLER BusSampler::Get()

#endif // BUS_SAMPLER_H
