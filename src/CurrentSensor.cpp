#include "CurrentSensor.h"
#include <Arduino.h>
#include <math.h>

#ifdef ESP32
#include "esp_heap_caps.h"
#endif

// ============================================================================
// Constructor
// ============================================================================

CurrentSensor::CurrentSensor()
    : _mutex(nullptr),
      _lastCurrentA(0.0f),
      _historyHead(0),
      _historySeq(0),
      _continuousRunning(false),
      _samplePeriodMs(1000 / HISTORY_HZ),
      _samplingTaskHandle(nullptr),
      _capturing(false),
      _captureBuf(nullptr),
      _captureCapacity(0),
      _captureCount(0),
      _ocLimitA(0.0f),
      _ocMinDurationMs(0),
      _ocLatched(false),
      _ocOverStartMs(0)
{
}

// ============================================================================
// begin()
// ============================================================================

void CurrentSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Initializing Current Sensor             #");
    DEBUG_PRINTLN("###########################################################");

    // Create mutex first so all subsequent APIs are safe
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        DEBUG_PRINTLN("[CurrentSensor] ERROR: Failed to create mutex ❌");
        DEBUGGSTOP();
        return;
    }

    // Configure hardware input
    pinMode(ACS_LOAD_CURRENT_VOUT_PIN, INPUT);

    // Default over-current protection:
    //  - Trip above 20 A
    //  - Sustained for at least 5 ms
    configureOverCurrent(CURRENT_LIMIT, CURRENT_TIME);

    // Info about history buffer / continuous sampling capabilities
    DEBUG_PRINTF("[CurrentSensor] ADC pin            : %d\n", ACS_LOAD_CURRENT_VOUT_PIN);
    DEBUG_PRINTF("[CurrentSensor] History window     : %u samples @ %u Hz (~%u s)\n",
                 (unsigned)HISTORY_SAMPLES,
                 (unsigned)HISTORY_HZ,
                 (unsigned)(HISTORY_SAMPLES / HISTORY_HZ));
    DEBUG_PRINTF("[CurrentSensor] Default sample period: %lu ms\n",
                 (unsigned long)(1000UL / HISTORY_HZ));

    // Over-current configuration summary
    if (_ocLimitA > 0.0f && _ocMinDurationMs > 0) {
        DEBUG_PRINTF("[CurrentSensor] Over-current limit : %.2f A for >= %u ms (latched)\n",
                     _ocLimitA,
                     (unsigned)_ocMinDurationMs);
    } else {
        DEBUG_PRINTLN("[CurrentSensor] Over-current limit : DISABLED");
    }

    DEBUG_PRINTLN("[CurrentSensor] Initialized ✅📈");
    DEBUGGSTOP();
}


// ============================================================================
// sampleOnce()
//   - Single ADC read -> current in A
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
//   - Legacy behavior:
//       * If capturing: return last known (_lastCurrentA).
//       * Else: 25-sample averaged ADC read.
//   - In continuous mode, _lastCurrentA is already maintained.
// ============================================================================

float CurrentSensor::readCurrent() {
    // During explicit capture, do NOT disturb timing with extra ADC loops.
    if (_capturing) {
        return _lastCurrentA;
    }

    constexpr uint8_t NUM_SAMPLES = 25;

    if (!lock()) {
        // Best-effort fallback: single sample
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
    _updateOverCurrentStateLocked(current, millis());

    unlock();
    return current;
}

// ============================================================================
// Continuous Sampling: startContinuous()
// ============================================================================

void CurrentSensor::startContinuous(uint32_t samplePeriodMs) {
    if (samplePeriodMs == 0) {
        samplePeriodMs = 1000 / HISTORY_HZ; // default based on HISTORY_HZ
    }
    if (samplePeriodMs == 0) {
        samplePeriodMs = 5;                 // absolute floor
    }

    if (!lock()) {
        return;
    }

    _samplePeriodMs = samplePeriodMs;

    // If already running, just update the period.
    if (_continuousRunning) {
        unlock();
        return;
    }

    // Continuous mode and explicit capture are mutually exclusive.
    _capturing = false;

    // Reset ring buffer indices.
    _historyHead       = 0;
    _historySeq        = 0;
    _continuousRunning = true;

    unlock();

    // Create sampling task if not already created.
    if (_samplingTaskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            _samplingTaskThunk,
            "CurrentSampler",
            4096,           // stack size; tune if needed
            this,
            tskIDLE_PRIORITY + 1,
            &_samplingTaskHandle
        );

        if (ok != pdPASS) {
            if (lock()) {
                _continuousRunning  = false;
                _samplingTaskHandle = nullptr;
                unlock();
            }
            DEBUG_PRINTLN("[CurrentSensor] ERROR: Failed to start continuous sampling task");
        } else {
            DEBUG_PRINTF("[CurrentSensor] Continuous sampling started (%lu ms period)\n",
                         (unsigned long)_samplePeriodMs);
        }
    }
}

// ============================================================================
// Continuous Sampling: stopContinuous()
// ============================================================================

void CurrentSensor::stopContinuous() {
    if (!lock()) return;
    _continuousRunning = false;
    unlock();
    // Sampling task will see this and self-delete.
}

// ============================================================================
// Continuous Sampling Task (static thunk + loop)
// ============================================================================

void CurrentSensor::_samplingTaskThunk(void* arg) {
    auto* self = static_cast<CurrentSensor*>(arg);
    self->_samplingTaskLoop();
}

void CurrentSensor::_samplingTaskLoop() {
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(_samplePeriodMs));

        // Check run flag
        if (!lock()) {
            continue;
        }
        bool shouldRun = _continuousRunning;
        unlock();

        if (!shouldRun) {
            break;
        }

        // Take one sample
        float    current = sampleOnce();
        uint32_t nowMs   = millis();

        if (!lock()) {
            // Best effort: keep lastCurrent updated without history/OC
            _lastCurrentA = current;
            continue;
        }

        _lastCurrentA = current;

        // Append to ring buffer (single producer)
        const uint32_t idx = _historyHead % HISTORY_SAMPLES;
        _history[idx].timestampMs = nowMs;
        _history[idx].currentA    = current;
        _historyHead++;
        _historySeq++;

        // Update over-current state
        _updateOverCurrentStateLocked(current, nowMs);

        unlock();
    }

    // Mark task as stopped
    if (lock()) {
        _samplingTaskHandle = nullptr;
        unlock();
    }

    vTaskDelete(nullptr);
}

// ============================================================================
// getHistorySince()
//   - SPSC-style consumer API for 10s ring buffer
// ============================================================================

size_t CurrentSensor::getHistorySince(uint32_t lastSeq,
                                      Sample* out,
                                      size_t maxOut,
                                      uint32_t& newSeq) const
{
    if (!out || maxOut == 0) {
        newSeq = lastSeq;
        return 0;
    }

    if (!const_cast<CurrentSensor*>(this)->lock()) {
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t seqNow = _historySeq;

    if (seqNow == 0) {
        // No samples yet
        unlock();
        newSeq = 0;
        return 0;
    }

    // Oldest sequence still valid (ring overwrite)
    const uint32_t maxSpan = (seqNow > HISTORY_SAMPLES)
                           ? HISTORY_SAMPLES
                           : seqNow;
    const uint32_t minSeq = seqNow - maxSpan;

    // Clamp lastSeq
    if (lastSeq < minSeq) lastSeq = minSeq;
    if (lastSeq > seqNow) lastSeq = seqNow;

    uint32_t available = seqNow - lastSeq;
    if (available > maxOut) {
        available = maxOut;
    }

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t sSeq = lastSeq + i;
        uint32_t idx  = sSeq % HISTORY_SAMPLES;
        out[i] = _history[idx];
    }

    newSeq = lastSeq + available;

    unlock();
    return static_cast<size_t>(available);
}

// ============================================================================
// startCapture()
//   - Allocate/prepare dedicated capture buffer
//   - Stop continuous sampling if active
// ============================================================================

bool CurrentSensor::startCapture(size_t maxSamples) {
    if (!lock()) {
        return false;
    }

    // Stop continuous mode while using explicit capture.
    _continuousRunning = false;

    // If already capturing with buffer, just reset.
    if (_capturing && _captureBuf != nullptr) {
        _captureCount = 0;
        unlock();
        return true;
    }

    // Free any previous buffer
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

    // Try PSRAM, then internal RAM
#ifdef ESP32
    _captureBuf = static_cast<Sample*>(
        heap_caps_malloc(maxSamples * sizeof(Sample),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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
    _lastCurrentA    = 0.0f;

    DEBUG_PRINTF("[CurrentSensor] Capture started (%u samples max)\n",
                 (unsigned)_captureCapacity);

    unlock();
    return true;
}

// ============================================================================
// stopCapture()
// ============================================================================

void CurrentSensor::stopCapture() {
    if (!lock()) return;
    _capturing = false;
    unlock();
}

// ============================================================================
// addCaptureSample()
//   - Only if capturing=true and buffer not full
// ============================================================================

bool CurrentSensor::addCaptureSample() {
    if (!_capturing) {
        return false;
    }
    if (!lock()) {
        return false;
    }

    if (!_capturing || _captureBuf == nullptr || _captureCount >= _captureCapacity) {
        unlock();
        return false;
    }

    const uint32_t nowMs   = millis();
    const float    current = sampleOnce();

    _lastCurrentA = current;

    const bool ok = pushCaptureSample(current, nowMs);

    // Over-current detection also applies to captured samples.
    _updateOverCurrentStateLocked(current, nowMs);

    // Auto-stop if full
    if (!ok || _captureCount >= _captureCapacity) {
        _capturing = false;
    }

    unlock();
    return ok;
}

// ============================================================================
// getCapture()
// ============================================================================

size_t CurrentSensor::getCapture(Sample* out, size_t maxCount) const {
    if (!out || maxCount == 0) return 0;
    if (!const_cast<CurrentSensor*>(this)->lock()) return 0;

    if (_captureBuf == nullptr || _captureCount == 0) {
        unlock();
        return 0;
    }

    const size_t n = (_captureCount < maxCount) ? _captureCount : maxCount;
    for (size_t i = 0; i < n; ++i) {
        out[i] = _captureBuf[i];
    }

    unlock();
    return n;
}

// ============================================================================
// freeCaptureBuffer()
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
// pushCaptureSample() - internal helper for explicit capture
// ============================================================================

inline bool CurrentSensor::pushCaptureSample(float currentA, uint32_t tsMs) {
    if (_captureBuf == nullptr || _captureCount >= _captureCapacity) {
        return false;
    }

    Sample& s = _captureBuf[_captureCount++];
    s.timestampMs = tsMs;
    s.currentA    = currentA;
    return true;
}

// ============================================================================
// Over-current detection (internal)
// ============================================================================

void CurrentSensor::_updateOverCurrentStateLocked(float currentA, uint32_t nowMs)
{
    // Disabled or already latched?
    if (_ocLimitA <= 0.0f || _ocMinDurationMs == 0 || _ocLatched) {
        return;
    }

    const float a = fabsf(currentA);

    if (a >= _ocLimitA) {
        // Above threshold
        if (_ocOverStartMs == 0) {
            _ocOverStartMs = nowMs;
        } else {
            const uint32_t dt = nowMs - _ocOverStartMs;
            if (dt >= _ocMinDurationMs) {
                _ocLatched = true;
            }
        }
    } else {
        // Back below threshold: reset window
        _ocOverStartMs = 0;
    }
}

// ============================================================================
// Over-current public API
// ============================================================================

void CurrentSensor::configureOverCurrent(float limitA, uint32_t minDurationMs)
{
    if (!lock()) return;

    if (limitA <= 0.0f || minDurationMs == 0) {
        // Disable detection
        _ocLimitA        = 0.0f;
        _ocMinDurationMs = 0;
        _ocOverStartMs   = 0;
        _ocLatched       = false;
    } else {
        _ocLimitA        = fabsf(limitA);
        _ocMinDurationMs = minDurationMs;
        _ocOverStartMs   = 0;
        _ocLatched       = false;
    }

    unlock();
}

bool CurrentSensor::isOverCurrentLatched() const
{
    bool latched = false;
    if (const_cast<CurrentSensor*>(this)->lock()) {
        latched = _ocLatched;
        const_cast<CurrentSensor*>(this)->unlock();
    }
    return latched;
}

void CurrentSensor::clearOverCurrentLatch()
{
    if (!lock()) return;
    _ocLatched     = false;
    _ocOverStartMs = 0;
    unlock();
}

// ============================================================================
// analogToMillivolts()
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
