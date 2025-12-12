#include "sensing/CurrentSensor.h"
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
      _zeroCurrentMv(ACS781_ZERO_CURRENT_MV),
      _sensitivityMvPerA(ACS781_SENSITIVITY_MV_PER_A),
      _ocLimitA(0.0f),
      _ocMinDurationMs(0),
      _ocLatched(false),
      _ocOverStartMs(0)
{
}

// ============================================================================
// begin()
//   - Create mutex
//   - Configure ADC pin
//   - Auto zero-current calibration (NO LOAD at boot)
//   - Configure default over-current protection
// ============================================================================

void CurrentSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Initializing Current Sensor             #");
    DEBUG_PRINTLN("###########################################################");

    // Create mutex first so all subsequent APIs are safe.
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        DEBUG_PRINTLN("[CurrentSensor] ERROR: Failed to create mutex ");
        DEBUGGSTOP();
        return;
    }
    // Configure hardware input.
    pinMode(ACS_LOAD_CURRENT_VOUT_PIN, INPUT);

    DEBUG_PRINTF("[CurrentSensor] ADC pin            : %d\n", ACS_LOAD_CURRENT_VOUT_PIN);
    DEBUG_PRINTF("[CurrentSensor] History window     : %u samples @ %u Hz (~%u s)\n",
                 (unsigned)HISTORY_SAMPLES,
                 (unsigned)HISTORY_HZ,
                 (unsigned)(HISTORY_SAMPLES / HISTORY_HZ));
    DEBUG_PRINTF("[CurrentSensor] Default sample period: %lu ms\n",
                 (unsigned long)(1000UL / HISTORY_HZ));

    DEBUG_PRINTF("[CurrentSensor] Nominal zero offset: %.2f mV\n", _zeroCurrentMv);
    DEBUG_PRINTF("[CurrentSensor] Nominal sensitivity: %.4f mV/A\n", _sensitivityMvPerA);

    // --------------------------------------------------------------------
    // Auto zero-current calibration at startup
    // REQUIREMENT: System must be at 0 A during this step.
    // --------------------------------------------------------------------
    DEBUG_PRINTLN("[CurrentSensor] Auto zero-current calibration starting (NO LOAD)...");
    calibrateZeroCurrent(CURRENT_SENSOR_AUTO_ZERO_CAL_SAMPLES,
                         CURRENT_SENSOR_AUTO_ZERO_CAL_SETTLE_MS);
    DEBUG_PRINTF("[CurrentSensor] Auto-calibrated zero offset: %.3f mV\n", _zeroCurrentMv);

    // --------------------------------------------------------------------
    // Default Over-Current configuration for 35 A system
    // --------------------------------------------------------------------
    configureOverCurrent(CURRENT_LIMIT, CURRENT_TIME);

    if (_ocLimitA > 0.0f && _ocMinDurationMs > 0) {
        DEBUG_PRINTF("[CurrentSensor] Over-current limit : %.2f A for >= %u ms (latched)\n",
                     _ocLimitA,
                     (unsigned)_ocMinDurationMs);
    } else {
        DEBUG_PRINTLN("[CurrentSensor] Over-current limit : DISABLED");
    }

    DEBUG_PRINTLN("[CurrentSensor] Initialized ");
    DEBUGGSTOP();
}

// ============================================================================
// sampleOnce()
//   - Single ADC read -> current in A using calibrated parameters.
// ============================================================================

float CurrentSensor::sampleOnce() {
    int   adc        = analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    float voltage_mv = analogToMillivolts(adc);
    float delta_mv   = voltage_mv - _zeroCurrentMv;
    float current    = delta_mv / _sensitivityMvPerA;
    return current;
}

// ============================================================================
// readCurrent()
//   - If capturing: return last known (_lastCurrentA).
//   - Else: 25-sample averaged ADC read (using calibration).
// ============================================================================

float CurrentSensor::readCurrent() {
    if (_capturing) {
        return _lastCurrentA;
    }

    constexpr uint8_t NUM_SAMPLES = 25;

    if (!lock()) {
        return sampleOnce();
    }

    long sumAdc = 0;
    for (uint8_t i = 0; i < NUM_SAMPLES; ++i) {
        sumAdc += analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    }

    int   adc        = static_cast<int>(sumAdc / NUM_SAMPLES);
    float voltage_mv = analogToMillivolts(adc);
    float delta_mv   = voltage_mv - _zeroCurrentMv;
    float current    = delta_mv / _sensitivityMvPerA;

    _lastCurrentA = current;
    _updateOverCurrentStateLocked(current, millis());

    unlock();
    return 4;
    return current;
}

// ============================================================================
// Continuous Sampling: startContinuous()
// ============================================================================

void CurrentSensor::startContinuous(uint32_t samplePeriodMs) {
    if (samplePeriodMs == 0) {
        samplePeriodMs = 1000 / HISTORY_HZ;
    }
    if (samplePeriodMs == 0) {
        samplePeriodMs = 5; // absolute floor
    }

    if (!lock()) {
        return;
    }

    _samplePeriodMs = samplePeriodMs;

    // If already running, just update the period.
    if (_continuousRunning) {
        unlock();
        DEBUG_PRINTF("[CurrentSensor] Updated continuous period to %lu ms\n",
                     (unsigned long)_samplePeriodMs);
        return;
    }

    // Continuous mode and explicit capture are mutually exclusive.
    _capturing = false;

    // Reset ring buffer indices.
    _historyHead       = 0;
    _historySeq        = 0;
    _continuousRunning = true;

    unlock();

    if (_samplingTaskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            _samplingTaskThunk,
            "CurrentSampler",
            4096,
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
    // Task will exit and self-delete.
}

// ============================================================================
// Continuous Sampling Task
// ============================================================================

void CurrentSensor::_samplingTaskThunk(void* arg) {
    auto* self = static_cast<CurrentSensor*>(arg);
    self->_samplingTaskLoop();
}

void CurrentSensor::_samplingTaskLoop() {
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(_samplePeriodMs));

        if (!lock()) {
            continue;
        }
        bool shouldRun = _continuousRunning;
        unlock();

        if (!shouldRun) {
            break;
        }

        float    current = sampleOnce();
        uint32_t nowMs   = millis();

        if (!lock()) {
            _lastCurrentA = current;
            continue;
        }

        _lastCurrentA = current;

        const uint32_t idx = _historyHead % HISTORY_SAMPLES;
        _history[idx].timestampMs = nowMs;
        _history[idx].currentA    = current;
        _historyHead++;
        _historySeq++;

        _updateOverCurrentStateLocked(current, nowMs);

        unlock();
    }

    if (lock()) {
        _samplingTaskHandle = nullptr;
        unlock();
    }

    vTaskDelete(nullptr);
}

// ============================================================================
// getHistorySince()
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
        const_cast<CurrentSensor*>(this)->unlock();
        newSeq = 0;
        return 0;
    }

    const uint32_t maxSpan = (seqNow > HISTORY_SAMPLES)
                           ? HISTORY_SAMPLES
                           : seqNow;
    const uint32_t minSeq = seqNow - maxSpan;

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

    const_cast<CurrentSensor*>(this)->unlock();
    return (size_t)available;
}

// ============================================================================
// startCapture()
// ============================================================================

bool CurrentSensor::startCapture(size_t maxSamples) {
    if (!lock()) {
        return false;
    }

    _continuousRunning = false;

    if (_capturing && _captureBuf != nullptr) {
        _captureCount = 0;
        unlock();
        return true;
    }

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

    if (maxSamples == 0 || maxSamples > CURRENT_CAPTURE_MAX_SAMPLES) {
        maxSamples = CURRENT_CAPTURE_MAX_SAMPLES;
    }

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

    _updateOverCurrentStateLocked(current, nowMs);

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
        const_cast<CurrentSensor*>(this)->unlock();
        return 0;
    }

    const size_t n = (_captureCount < maxCount) ? _captureCount : maxCount;
    for (size_t i = 0; i < n; ++i) {
        out[i] = _captureBuf[i];
    }

    const_cast<CurrentSensor*>(this)->unlock();
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
// pushCaptureSample()
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
    if (_ocLimitA <= 0.0f || _ocMinDurationMs == 0 || _ocLatched) {
        return;
    }

    const float a = fabsf(currentA);

    if (a >= _ocLimitA) {
        if (_ocOverStartMs == 0) {
            _ocOverStartMs = nowMs;
        } else {
            const uint32_t dt = nowMs - _ocOverStartMs;
            if (dt >= _ocMinDurationMs) {
                _ocLatched = true;
            }
        }
    } else {
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
// Calibration helpers
// ============================================================================

void CurrentSensor::setCalibration(float zeroCurrentMv, float sensitivityMvPerA)
{
    if (!lock()) return;

    if (sensitivityMvPerA > 0.0f) {
        _sensitivityMvPerA = sensitivityMvPerA;
    }

    if (zeroCurrentMv > 0.0f &&
        zeroCurrentMv < (ADC_REF_VOLTAGE * 1000.0f)) {
        _zeroCurrentMv = zeroCurrentMv;
    }

    unlock();

    DEBUG_PRINTF("[CurrentSensor] Calibration set: zero=%.3f mV, sens=%.5f mV/A\n",
                 _zeroCurrentMv, _sensitivityMvPerA);
}

void CurrentSensor::setMiddlePoint(int adcValue)
{
    // Convert raw ADC code to millivolts and use it as the new zero-current offset.
    float mv = analogToMillivolts(adcValue);

    if (!lock()) {
        return;
    }

    _zeroCurrentMv = mv;

    unlock();

    DEBUG_PRINTF("[CurrentSensor] Middle point set: ADC=%d -> zero=%.3f mV\n",
                 adcValue, _zeroCurrentMv);
}

void CurrentSensor::calibrateZeroCurrent(uint16_t samples, uint16_t settleMs)
{
    if (samples == 0) {
        samples = 200;
    }

    if (!lock()) {
        return;
    }

    DEBUG_PRINTLN("[CurrentSensor] Zero-current calibration started (NO LOAD required)");

    _ocLatched     = false;
    _ocOverStartMs = 0;

    unlock();

    vTaskDelay(pdMS_TO_TICKS(settleMs));

    uint64_t sum = 0;

    for (uint16_t i = 0; i < samples; ++i) {
        int adc = analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
        sum += (uint32_t)adc;
#ifdef ESP32
        ets_delay_us(100);
#else
        delayMicroseconds(100);
#endif
    }

    int avgAdc = (int)(sum / samples);
    // Reuse middle-point helper so all zero-point handling stays in one place.
    setMiddlePoint(avgAdc);
    DEBUG_PRINTF("[CurrentSensor] Zero-current calibrated from avg ADC=%d\n", avgAdc);
}

// ============================================================================
// getRmsCurrent() - RMS over recent window using history buffer
// ============================================================================

float CurrentSensor::getRmsCurrent(uint32_t windowMs) const
{
    const uint32_t maxWindow = HISTORY_SECONDS * 1000UL;
    if (windowMs == 0 || windowMs > maxWindow) {
        windowMs = maxWindow;
    }

    if (!const_cast<CurrentSensor*>(this)->lock()) {
        return fabsf(_lastCurrentA);
    }

    const uint32_t seqNow = _historySeq;

    if (seqNow == 0) {
        float last = _lastCurrentA;
        const_cast<CurrentSensor*>(this)->unlock();
        return fabsf(last);
    }

    uint32_t maxCount = (seqNow > HISTORY_SAMPLES)
                      ? HISTORY_SAMPLES
                      : seqNow;

    if (maxCount == 0) {
        float last = _lastCurrentA;
        const_cast<CurrentSensor*>(this)->unlock();
        return fabsf(last);
    }

    const uint32_t newestSeq = seqNow - 1;
    const uint32_t newestIdx = newestSeq % HISTORY_SAMPLES;
    const uint32_t newestTs  = _history[newestIdx].timestampMs;

    const uint32_t minTs = (newestTs > windowMs)
                         ? (newestTs - windowMs)
                         : 0;

    double    sumSq = 0.0;
    uint32_t  n     = 0;

    for (uint32_t i = 0; i < maxCount; ++i) {
        const uint32_t sSeq = seqNow - 1 - i;
        const uint32_t idx  = sSeq % HISTORY_SAMPLES;
        const Sample&  s    = _history[idx];

        if (s.timestampMs < minTs) {
            break;
        }

        const double ia = (double)s.currentA;
        sumSq += ia * ia;
        ++n;
    }

    const_cast<CurrentSensor*>(this)->unlock();

    if (n == 0) {
        return fabsf(_lastCurrentA);
    }

    const double rms = sqrt(sumSq / (double)n);
    return (float)rms;
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
