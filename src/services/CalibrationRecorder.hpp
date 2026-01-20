/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef CALIBRATION_RECORDER_H
#define CALIBRATION_RECORDER_H

#include <Arduino.h>
#include <Config.hpp>
#include <BusSampler.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class CalibrationRecorder {
public:
    enum class Mode : uint8_t { None = 0, Ntc = 1, Model = 2, Floor = 3 };

    struct Sample {
        uint32_t tMs      = 0;   // time since start
        float    voltageV = NAN;
        float    currentA = NAN;
        float    tempC    = NAN; // NTC-based temperature
        float    roomTempC = NAN; // Heatsink/ambient temperature
        float    ntcVolts = NAN;
        float    ntcOhm   = NAN;
        uint16_t ntcAdc   = 0;
        bool     ntcValid = false;
        bool     pressed  = false;
    };

    struct Meta {
        Mode     mode        = Mode::None;
        bool     running     = false;
        uint32_t startMs     = 0;
        uint32_t startEpoch  = 0;
        uint32_t intervalMs  = 0;
        uint16_t count       = 0;
        uint16_t capacity    = 0;
        float    targetTempC = NAN;
        uint8_t  wireIndex   = 0;
        bool     saved       = false;
        uint32_t savedMs     = 0;
        uint32_t savedEpoch  = 0;
    };

    static void Init();
    static CalibrationRecorder* Get();

    bool start(Mode mode,
               uint32_t intervalMs,
               uint16_t maxSamples,
               float targetTempC,
               uint8_t wireIndex);
    void stop();
    bool stopAndSave(uint32_t timeoutMs = 1500);
    bool saveToFile(const char* path = CALIB_MODEL_CBOR_FILE);
    void clear();

    bool isRunning() const;
    uint16_t getSampleCount() const;
    Meta getMeta() const;
    size_t copySamples(uint16_t offset, Sample* out, size_t maxOut) const;

    static constexpr uint32_t kDefaultIntervalMs = 500;
    static constexpr uint16_t kDefaultMaxSamples = 1200;
    static constexpr uint16_t kAbsoluteMaxSamples = 2048;

private:
    CalibrationRecorder() = default;

    static void taskThunk(void* param);
    void taskLoop();

    inline bool lock(TickType_t timeoutTicks = portMAX_DELAY) const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    void freeBufferLocked();
    bool saveToHistoryFiles();

    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;

    Sample*  _buf      = nullptr;
    uint16_t _capacity = 0;
    uint16_t _count    = 0;

    Mode     _mode        = Mode::None;
    bool     _running     = false;
    uint32_t _startMs     = 0;
    uint32_t _startEpoch  = 0;
    uint32_t _intervalMs  = kDefaultIntervalMs;
    float    _targetTempC = NAN;
    uint8_t  _wireIndex   = 0;
    bool     _saveOnStop  = false;
    bool     _lastSaveOk  = false;
    uint32_t _lastSaveMs  = 0;
    uint32_t _lastSaveEpoch = 0;

    static CalibrationRecorder* s_instance;
};

#define CALREC CalibrationRecorder::Get()

#endif // CALIBRATION_RECORDER_H
