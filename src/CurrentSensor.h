#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include "Utils.h"

// ============================================================================
// ACS781 Current Sensor with Capture + Continuous History
// ============================================================================
//
// This implementation provides two layers:
//
// 1. Legacy / explicit capture API (unchanged externally):
//    - readCurrent()
//    - startCapture() / stopCapture()
//    - addCaptureSample()
//    - getCapture() / freeCaptureBuffer()
//    These are kept for backwards compatibility with existing Device logic.
//
// 2. Continuous sampling + 10s rolling history (RTOS-friendly):
//    - startContinuous(samplePeriodMs)
//    - stopContinuous()
//    - getLastCurrent()
//    - getHistorySince(lastSeq, out[], maxOut, newSeq)
//    This is the recommended interface for new RTOS-based thermal / planner
//    tasks, allowing them to pull a time-aligned stream of (timestamp, current)
//    samples without coordinating ADC timing.
//
// Notes:
//  - Continuous mode and capture mode are treated as mutually exclusive:
//      * starting a capture stops continuous sampling;
//      * starting continuous sampling does NOT auto-start capture.
//  - The 10s history is implemented as a lock-protected ring buffer.
// ============================================================================

// Sensor characteristics (for ACS781LLRTR-100B-T)
#define ACS781_SENSITIVITY_MV_PER_A   13.2f        // Sensitivity [mV/A]
#define ACS781_ZERO_CURRENT_MV        1650.0f      // Zero-current output [mV]
#define ADC_REF_VOLTAGE               3.3f         // [V]
#define ADC_MAX                       4095.0f

// Max capture depth for legacy explicit capture API.
// (kept as-is for compatibility)
#define CURRENT_CAPTURE_MAX_SAMPLES   6000U
// ---------------------------------------------------------------------
// Continuous history configuration
// ---------------------------------------------------------------------
//
// 10 seconds at 200 Hz → 2000 samples.
// Each sample is 8 bytes → ~16 kB.
// Adjust HISTORY_HZ if needed.
//
#define HISTORY_SECONDS  10
#define HISTORY_HZ     200  // 200 Hz → 5 ms period
#define HISTORY_SAMPLES  HISTORY_SECONDS * HISTORY_HZ
#define CURRENT_LIMIT 20.0f
#define CURRENT_TIME 5

class CurrentSensor {
public:
    struct Sample {
        uint32_t timestampMs;  ///< millis() when taken
        float    currentA;     ///< measured current [A]
    };



    CurrentSensor();

    // Initialize ADC input and mutex.
    void begin();

    // ---------------------------------------------------------------------
    // Legacy-style current read
    // ---------------------------------------------------------------------
    //
    // If NOT capturing:
    //   - returns a 25-sample averaged read from the ADC.
    // If capturing:
    //   - returns the last captured/updated current (no extra ADC reads).
    //
    float readCurrent();

    // ---------------------------------------------------------------------
    // Continuous sampling / 10s history (new, RTOS-friendly)
    // ---------------------------------------------------------------------

    /**
     * @brief Start periodic continuous sampling into the history buffer.
     *
     * @param samplePeriodMs Sampling period in ms.
     *        If 0, defaults to 1000 / HISTORY_HZ.
     *
     * Behavior:
     *  - Spawns a FreeRTOS task (if not already running).
     *  - Each tick:
     *      * reads one ADC sample,
     *      * updates _lastCurrentA,
     *      * appends to the ring buffer (overwriting oldest after 10s).
     */
    void startContinuous(uint32_t samplePeriodMs = 0);

    /**
     * @brief Stop continuous sampling.
     *
     * The sampling task will exit gracefully on the next tick.
     */
    void stopContinuous();

    /**
     * @brief Check if continuous sampling is running.
     */
    bool isContinuousRunning() const { return _continuousRunning; }

    /**
     * @brief Get the latest known current value (cheap).
     *
     * Always returns the most recent sample from either:
     *  - continuous sampling, or
     *  - legacy readCurrent()/addCaptureSample().
     */
    float getLastCurrent() const { return _lastCurrentA; }

    /**
     * @brief Fetch history samples added since a given sequence number.
     *
     * @param lastSeq  Input: sequence index last consumed by the caller.
     *                 Use 0 for "from the oldest available".
     * @param out      Output array for samples.
     * @param maxOut   Capacity of @p out.
     * @param newSeq   Output: updated sequence value to use on next call.
     *
     * @return Number of samples written to @p out.
     *
     * Behavior:
     *  - Samples are returned in chronological order.
     *  - If @p lastSeq is too old (older than 10s window), it is clamped to
     *    the oldest still-retained sample.
     *  - Caller should store @p newSeq and pass it back as @p lastSeq on the
     *    next invocation to consume history incrementally.
     */
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

    // ---------------------------------------------------------------------
    // Explicit capture API (backwards compatible)
    // ---------------------------------------------------------------------

    /**
     * @brief Start an explicit capture session.
     *
     * @param maxSamples Requested max samples for this session.
     *                   Clamped to CURRENT_CAPTURE_MAX_SAMPLES.
     *
     * Behavior:
     *  - Stops continuous sampling if it is running.
     *  - Allocates/clears a dedicated capture buffer.
     *  - Sets capturing=true.
     */
    bool startCapture(size_t maxSamples);

    /**
     * @brief Stop the current capture session (if any).
     *
     * Data remains available for getCapture().
     */
    void stopCapture();

    /// @brief Returns true if a capture session is active.
    bool isCapturing() const { return _capturing; }

    /**
     * @brief Add one sample to the capture buffer (if capturing).
     *
     * Uses a single ADC read.
     *
     * @return true if stored, false if not capturing or buffer full.
     */
    bool addCaptureSample();

    /**
     * @brief Copy captured samples into @p out.
     *
     * @return Number of samples copied.
     */
    size_t getCapture(Sample* out, size_t maxCount) const;

    /// @brief Number of valid samples in the capture buffer.
    size_t getCaptureCount() const { return _captureCount; }

    /**
     * @brief Release the capture buffer to free memory.
     */
    void freeCaptureBuffer();
    // ---------------------------------------------------------------------
    // Over-current protection
    // ---------------------------------------------------------------------

    /**
     * @brief Configure over-current detection.
     *
     * @param limitA         Absolute current threshold in amperes.
     * @param minDurationMs  Time in ms the current must remain above the threshold
     *                       before latching a fault.
     *
     * Passing limitA <= 0 or minDurationMs == 0 disables over-current detection.
     */
    void configureOverCurrent(float limitA, uint32_t minDurationMs);

    /**
     * @brief Returns true if an over-current condition has been latched.
     *
     * This latch is only cleared by clearOverCurrentLatch() or configureOverCurrent().
     */
    bool isOverCurrentLatched() const;

    /**
     * @brief Clear the over-current latch and restart detection.
     */
    void clearOverCurrentLatch();


private:
    // Convert ADC reading to millivolts.
    float analogToMillivolts(int adcValue) const;

    // Take a single, immediate ADC-based current sample (no averaging).
    float sampleOnce();

    // Mutex helpers.
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // Internal helper to append into capture buffer (no extra ADC).
    inline bool pushCaptureSample(float currentA, uint32_t tsMs);

    // ---------------------------------------------------------------------
    // Members
    // ---------------------------------------------------------------------

    SemaphoreHandle_t _mutex;

    // Last known current value (for cheap readCurrent()/getLastCurrent()).
    volatile float _lastCurrentA;

    // ---- Continuous history sampling -----------------------------------

    // 10s ring buffer of samples:
    Sample   _history[HISTORY_SAMPLES];
    uint32_t _historyHead;       ///< Next write index (monotonic, modded).
    uint32_t _historySeq;        ///< Monotonic sequence number of samples.
    bool     _continuousRunning; ///< True while sampling task should run.
    uint32_t _samplePeriodMs;    ///< Sampling period in ms.

    TaskHandle_t _samplingTaskHandle; ///< FreeRTOS task handle for sampler.

    static void _samplingTaskThunk(void* arg);
    void        _samplingTaskLoop();

    // ---- Explicit capture state (legacy behavior) ----------------------

    bool     _capturing;
    Sample*  _captureBuf;
    size_t   _captureCapacity;   ///< Allocated slots.
    size_t   _captureCount;      ///< Used slots.
    // ---------------------------------------------------------------------
    // Over-current detection state
    // ---------------------------------------------------------------------
    float    _ocLimitA        = 0.0f;   ///< Threshold (|I|) in amperes; 0 = disabled
    uint32_t _ocMinDurationMs = 0;      ///< Required duration above threshold
    bool     _ocLatched       = false;  ///< Latched over-current fault
    uint32_t _ocOverStartMs   = 0;      ///< First timestamp above threshold (0 = none)

    /**
     * @brief Update over-current state with a new sample.
     *
     * Must be called with the internal mutex already held.
     */
    void _updateOverCurrentStateLocked(float currentA, uint32_t nowMs);

};

#endif // CURRENT_SENSOR_H
