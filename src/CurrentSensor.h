#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include "Utils.h"

// ============================================================================
// ACS781 Current Sensor with Triggered Capture
// ============================================================================
//
// Goals:
// - Keep legacy behavior for readCurrent() when idle (25-sample average).
// - Allow master to explicitly start/stop a capture session.
// - During capture, samples are taken ONLY when master calls addCaptureSample().
// - readCurrent() is cheap during capture: returns last known value, no ADC spam.
// - Capture buffer is allocated lazily, prefers PSRAM, falls back to DRAM.
// - Max depth sized for up to ~6s of data (configurable), but only used on demand.
// ============================================================================

// Sensor characteristics (for ACS781LLRTR-100B-T)
#define ACS781_SENSITIVITY_MV_PER_A   13.2f        // Sensitivity [mV/A]
#define ACS781_ZERO_CURRENT_MV        1650.0f      // Zero-current output [mV]
#define ADC_REF_VOLTAGE               3.3f         // [V]
#define ADC_MAX                       4095.0f

// Max capture depth.
// Example: 6000 samples = 6s @ 1 kHz, or 60s @ 100 Hz.
// Actual used count is chosen by master in startCapture().
#define CURRENT_CAPTURE_MAX_SAMPLES   6000U

class CurrentSensor {
public:
    struct Sample {
        uint32_t timestampMs;  // millis() when taken
        float    currentA;     // measured current [A]
    };

    CurrentSensor();

    // Initialize ADC input and mutex.
    void begin();

    // Legacy-style current read:
    // - If NOT capturing: 25-sample averaged read from ADC.
    // - If capturing: returns last captured current (cheap, no extra ADC work).
    float readCurrent();

    // ---- Capture control ----------------------------------------------------

    // Start a capture session.
    // - maxSamples: requested maximum stored samples for this session
    //               (will be clamped to CURRENT_CAPTURE_MAX_SAMPLES).
    // Behavior:
    // - Allocates/clears capture buffer (PSRAM preferred).
    // - Sets capturing=true.
    // - Returns true on success, false if allocation failed.
    bool startCapture(size_t maxSamples);

    // Stop capture session.
    // - Does NOT free the buffer; data remains available for getCapture().
    // - Safe to call even if not capturing.
    void stopCapture();

    // Returns true if a capture session is currently active.
    bool isCapturing() const { return _capturing; }

    // Add one sample (timestamp + current) to capture buffer IF capturing.
    // - Uses a single ADC read internally (fast).
    // - Returns true if stored, false if not capturing or buffer full.
    bool addCaptureSample();

    // Copy captured samples to 'out' (oldest -> newest).
    // Returns number of samples copied.
    // Safe to call after stopCapture().
    size_t getCapture(Sample* out, size_t maxCount) const;

    // Return how many valid samples are in the current capture buffer.
    size_t getCaptureCount() const { return _captureCount; }

    // Explicitly free capture buffer to release memory when no longer needed.
    void freeCaptureBuffer();

private:
    // Convert ADC reading to millivolts.
    float analogToMillivolts(int adcValue) const;

    // Take a single, immediate ADC-based current sample (no averaging).
    float sampleOnce();

    // Lock/unlock helper.
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // ---- Members ------------------------------------------------------------

    SemaphoreHandle_t _mutex;

    // Last known current value (for cheap readCurrent() while capturing).
    volatile float _lastCurrentA;

    // Capture state
    bool     _capturing;
    Sample*  _captureBuf;
    size_t   _captureCapacity;  // allocated slots
    size_t   _captureCount;     // used slots

    // Internal helper to push into capture buffer (no extra ADC).
    inline bool pushSample(float currentA, uint32_t tsMs);
};

#endif // CURRENT_SENSOR_H
