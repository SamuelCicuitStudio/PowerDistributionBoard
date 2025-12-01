/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include "system/Utils.h"

// ============================================================================
// ACS781 Current Sensor with Capture + Continuous History + Auto Calibration
// ============================================================================
//
// Tailored for ACS781LLRTR-100B-T (100 A, 13.2 mV/A, bidirectional, 3.3 V).
//
// Provides:
// 1. Legacy / explicit capture API:
//    - readCurrent()
//    - startCapture() / stopCapture()
//    - addCaptureSample()
//    - getCapture() / freeCaptureBuffer()
// 2. Continuous sampling + 10s rolling history (RTOS-friendly):
//    - startContinuous(samplePeriodMs)
//    - stopContinuous()
//    - getLastCurrent()
//    - getHistorySince()
// 3. Calibration & RMS helpers:
//    - Auto zero-current calibration in begin() (NO LOAD REQUIRED AT BOOT)
//    - setCalibration(zeroMv, sensitivityMvPerA)
//    - calibrateZeroCurrent(...)
//    - getZeroCurrentMv(), getSensitivityMvPerA()
//    - getRmsCurrent(windowMs)
//
// Notes:
//  - Continuous mode and capture mode are mutually exclusive.
//  - Uses a ring buffer for a 10s window.
// ============================================================================

// ---------------------- Sensor characteristics ------------------------------
// Defaults: refined at runtime by auto-calibration.
#define ACS781_SENSITIVITY_MV_PER_A        13.2f   // Typical sensitivity [mV/A]
#define ACS781_ZERO_CURRENT_MV             1650.0f // Nominal zero-current [mV]
#define ADC_REF_VOLTAGE                    3.3f    // ADC reference [V]
#define ADC_MAX                            4095.0f // 12-bit ADC full scale

// ---------------------- Auto zero calibration at startup --------------------
#define CURRENT_SENSOR_AUTO_ZERO_CAL_SAMPLES   800    // samples for zero cal
#define CURRENT_SENSOR_AUTO_ZERO_CAL_SETTLE_MS 50     // settle before sampling

// ---------------------- Capture configuration -------------------------------
#define CURRENT_CAPTURE_MAX_SAMPLES        6000U

// ---------------------- History configuration -------------------------------
//
// 10 seconds at 200 Hz â†’ 2000 samples (~16 kB).
//
#define HISTORY_SECONDS                    10
#define HISTORY_HZ                         200      // 200 Hz â†’ 5 ms
#define HISTORY_SAMPLES                    (HISTORY_SECONDS * HISTORY_HZ)

// ---------------------- Over-current defaults -------------------------------
//
// For your critical 35 A max system:
// Default: latch if |I| >= 36 A for >= 10 ms.
// Tune via configureOverCurrent() if needed.
//
#define CURRENT_LIMIT                      36.0f   // Default OC limit [A]
#define CURRENT_TIME                       10      // Default OC duration [ms]


class CurrentSensor {
public:
    struct Sample {
        uint32_t timestampMs;  ///< millis() when taken
        float    currentA;     ///< measured current [A]
    };

    CurrentSensor();

    // Initialize ADC input, mutex, default OC, and auto zero-current calibration.
    void begin();

    // ---------------------------------------------------------------------
    // Legacy-style current read
    // ---------------------------------------------------------------------
    //
    // If NOT capturing:
    //   - returns a 25-sample averaged read from the ADC (using calibration).
    // If capturing:
    //   - returns the last captured/updated current (no extra ADC reads).
    //
    float readCurrent();

    // ---------------------------------------------------------------------
    // Continuous sampling / 10s history (RTOS-friendly)
    // ---------------------------------------------------------------------

    void startContinuous(uint32_t samplePeriodMs = 0);
    void stopContinuous();
    bool  isContinuousRunning() const { return _continuousRunning; }
    float getLastCurrent() const      { return _lastCurrentA; }
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

    // ---------------------------------------------------------------------
    // Explicit capture API
    // ---------------------------------------------------------------------

    bool   startCapture(size_t maxSamples);
    void   stopCapture();
    bool   isCapturing() const { return _capturing; }
    bool   addCaptureSample();
    size_t getCapture(Sample* out, size_t maxCount) const;
    size_t getCaptureCount() const { return _captureCount; }
    void   freeCaptureBuffer();

    // ---------------------------------------------------------------------
    // Over-current protection
    // ---------------------------------------------------------------------

    void configureOverCurrent(float limitA, uint32_t minDurationMs);
    bool isOverCurrentLatched() const;
    void clearOverCurrentLatch();

    // ---------------------------------------------------------------------
    // Calibration & RMS helpers
    // ---------------------------------------------------------------------

    void setCalibration(float zeroCurrentMv, float sensitivityMvPerA);
    void calibrateZeroCurrent(uint16_t samples = 500, uint16_t settleMs = 50);
    float getZeroCurrentMv() const      { return _zeroCurrentMv; }
    float getSensitivityMvPerA() const  { return _sensitivityMvPerA; }
    float getRmsCurrent(uint32_t windowMs) const;

private:
    float analogToMillivolts(int adcValue) const;
    float sampleOnce();
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }
    inline bool pushCaptureSample(float currentA, uint32_t tsMs);

    // ---------------------------------------------------------------------
    // Members
    // ---------------------------------------------------------------------

    SemaphoreHandle_t _mutex;

    volatile float _lastCurrentA;

    // Continuous history sampling
    Sample   _history[HISTORY_SAMPLES];
    uint32_t _historyHead;
    uint32_t _historySeq;
    bool     _continuousRunning;
    uint32_t _samplePeriodMs;
    TaskHandle_t _samplingTaskHandle;

    static void _samplingTaskThunk(void* arg);
    void        _samplingTaskLoop();

    // Explicit capture state
    bool     _capturing;
    Sample*  _captureBuf;
    size_t   _captureCapacity;
    size_t   _captureCount;

    // Calibration state
    float _zeroCurrentMv;
    float _sensitivityMvPerA;

    // Over-current detection state
    float    _ocLimitA;
    uint32_t _ocMinDurationMs;
    bool     _ocLatched;
    uint32_t _ocOverStartMs;

    void _updateOverCurrentStateLocked(float currentA, uint32_t nowMs);
};

#endif // CURRENT_SENSOR_H


