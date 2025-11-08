#include "CurrentSensor.h"
#include <Arduino.h>

#ifdef ESP32
#include "esp_heap_caps.h"
#endif

// ============================================================================
// Constructor
// ============================================================================
CurrentSensor::CurrentSensor()
    : _mutex(nullptr),
      _lastCurrentA(0.0f),
      _capturing(false),
      _captureBuf(nullptr),
      _captureCapacity(0),
      _captureCount(0) {}

// ============================================================================
// begin()
// ============================================================================
void CurrentSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Current Manager               #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    _mutex = xSemaphoreCreateMutex();

    pinMode(ACS_LOAD_CURRENT_VOUT_PIN, INPUT);

    DEBUG_PRINTLN("[CurrentSensor] Initialized 📈");
}

// ============================================================================
// sampleOnce()
//   - Single ADC read -> current in A
//   - No averaging, minimal cost
// ============================================================================
float CurrentSensor::sampleOnce() {
    int   adc        = analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    float voltage_mv = analogToMillivolts(adc);
    float delta_mv   = voltage_mv - ACS781_ZERO_CURRENT_MV;
    float current    = delta_mv / ACS781_SENSITIVITY_MV_PER_A;
    return current;
}

// ============================================================================
// readCurrent()
//   - If not capturing: legacy 25-sample average
//   - If capturing: return _lastCurrentA (no heavy work)
// ============================================================================
float CurrentSensor::readCurrent() {
    // During capture, NEVER disturb timing with extra ADC loops.
    // Just return the last value recorded by addCaptureSample().
    if (_capturing) {
        return _lastCurrentA;
    }

    constexpr uint8_t NUM_SAMPLES = 25;

    if (!lock()) {
        // Fallback: best-effort single sample
        return sampleOnce();
    }

    long sumAdc = 0;
    for (uint8_t i = 0; i < NUM_SAMPLES; ++i) {
        sumAdc += analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    }

    int   adc        = static_cast<int>(sumAdc / NUM_SAMPLES);
    float voltage_mv = analogToMillivolts(adc);
    float delta_mv   = voltage_mv - ACS781_ZERO_CURRENT_MV;
    float current    = delta_mv / ACS781_SENSITIVITY_MV_PER_A;

    _lastCurrentA = current;

    unlock();
    return current;
}

// ============================================================================
// startCapture()
//   - Allocate/prepare buffer
//   - Enable capturing mode
// ============================================================================
bool CurrentSensor::startCapture(size_t maxSamples) {
    if (!lock()) {
        return false;
    }

    // If already capturing, reset state instead of reallocating.
    if (_capturing && _captureBuf != nullptr) {
        _captureCount = 0;
        unlock();
        return true;
    }

    // Free any previous buffer (from earlier session)
    if (_captureBuf != nullptr) {
#ifdef ESP32
        heap_caps_free(_captureBuf);
#else
        free(_captureBuf);
#endif
        _captureBuf      = nullptr;
        _captureCapacity = 0;
        _captureCount    = 0;
    }

    // Clamp requested samples
    if (maxSamples == 0 || maxSamples > CURRENT_CAPTURE_MAX_SAMPLES) {
        maxSamples = CURRENT_CAPTURE_MAX_SAMPLES;
    }

    // Try allocate in PSRAM first (ESP32)
#ifdef ESP32
    _captureBuf = static_cast<Sample*>(
        heap_caps_malloc(maxSamples * sizeof(Sample),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // Fallback to internal RAM
    if (_captureBuf == nullptr) {
        _captureBuf = static_cast<Sample*>(
            heap_caps_malloc(maxSamples * sizeof(Sample),
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
#else
    _captureBuf = static_cast<Sample*>(malloc(maxSamples * sizeof(Sample)));
#endif

    if (_captureBuf == nullptr) {
        _captureCapacity = 0;
        _captureCount    = 0;
        _capturing       = false;
        DEBUG_PRINTLN("[CurrentSensor] ERROR: capture buffer alloc failed");
        unlock();
        return false;
    }

    _captureCapacity = maxSamples;
    _captureCount    = 0;
    _capturing       = true;

    // Reset last current so first read is well-defined
    _lastCurrentA    = 0.0f;

    // First sample is taken only when master calls addCaptureSample().
    DEBUG_PRINTF("[CurrentSensor] Capture started (%u samples max)\n",
                 (unsigned)_captureCapacity);

    unlock();
    return true;
}

// ============================================================================
// stopCapture()
//   - Stop recording; keep data available
// ============================================================================
void CurrentSensor::stopCapture() {
    if (!lock()) return;
    _capturing = false;
    unlock();
}

// ============================================================================
// addCaptureSample()
//   - Only does work if capturing=true and buffer not full
//   - Uses single ADC read
// ============================================================================
bool CurrentSensor::addCaptureSample() {
    if (!_capturing) {
        return false;
    }
    if (!lock()) {
        return false;
    }

    if (!_capturing || _captureBuf == nullptr || _captureCount >= _captureCapacity) {
        // Either stopped mid-lock or buffer full
        unlock();
        return false;
    }

    const uint32_t nowMs   = millis();
    const float    current = sampleOnce();

    _lastCurrentA = current;  // keep in sync for readCurrent()

    const bool ok = pushSample(current, nowMs);

    // If buffer filled by this write, we auto-stop capture to be safe.
    if (!ok || _captureCount >= _captureCapacity) {
        _capturing = false;
    }

    unlock();
    return ok;
}

// ============================================================================
// getCapture()
//   - Copy captured samples (oldest -> newest)
// ============================================================================
size_t CurrentSensor::getCapture(Sample* out, size_t maxCount) const {
    if (!out || maxCount == 0) return 0;
    if (!const_cast<CurrentSensor*>(this)->lock()) return 0;

    if (_captureBuf == nullptr || _captureCount == 0) {
        unlock();
        return 0;
    }

    const size_t n = (_captureCount < maxCount) ? _captureCount : maxCount;

    // We always store sequentially from index 0.._captureCount-1 in this design.
    for (size_t i = 0; i < n; ++i) {
        out[i] = _captureBuf[i];
    }

    unlock();
    return n;
}

// ============================================================================
// freeCaptureBuffer()
//   - Manually release memory when capture data no longer needed
// ============================================================================
void CurrentSensor::freeCaptureBuffer() {
    if (!lock()) return;

    _capturing = false;

    if (_captureBuf != nullptr) {
#ifdef ESP32
        heap_caps_free(_captureBuf);
#else
        free(_captureBuf);
#endif
        _captureBuf = nullptr;
    }

    _captureCapacity = 0;
    _captureCount    = 0;

    unlock();
}

// ============================================================================
// pushSample() - internal helper
// ============================================================================
inline bool CurrentSensor::pushSample(float currentA, uint32_t tsMs) {
    if (_captureBuf == nullptr || _captureCount >= _captureCapacity) {
        return false;
    }

    Sample& s = _captureBuf[_captureCount++];
    s.timestampMs = tsMs;
    s.currentA    = currentA;
    return true;
}
// ============================================================================
// analogToMillivolts()
//   - Convert raw ADC code to millivolts
//   - Matches ACS781 config used in header defines
// ============================================================================
float CurrentSensor::analogToMillivolts(int adcValue) const {
    if (adcValue < 0) {
        adcValue = 0;
    }
#if defined(ADC_MAX) && defined(ADC_REF_VOLTAGE)
    if (adcValue > (int)ADC_MAX) {
        adcValue = (int)ADC_MAX;
    }
    const float v = (static_cast<float>(adcValue) / ADC_MAX) * ADC_REF_VOLTAGE;
#else
    const float v = (static_cast<float>(adcValue) / 4095.0f) * 3.3f;
#endif
    return v * 1000.0f; // mV
}
