/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <Arduino.h>
#include <Config.hpp>
#include <NVSManager.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class NtcSensor {
public:
    enum class Model : uint8_t {
        Beta = 0,
        Steinhart = 1
    };

    struct Sample {
        uint32_t timestampMs = 0;
        uint16_t adcRaw      = 0;
        float    volts       = NAN;
        float    rNtcOhm      = NAN;
        float    tempC       = NAN;
        bool     valid       = false;
        bool     pressed     = false;
    };

    static void Init();
    static NtcSensor* Get();

    void begin(uint8_t pin = POWER_ON_SWITCH_PIN);

    // Sampling / state
    void   update();
    Sample getLastSample() const;
    float  getLastTempC() const;
    bool   isPressed() const;

    // Calibration / configuration
    void setBeta(float beta, bool persist = true);
    void setT0C(float t0C, bool persist = true);
    void setR0(float r0Ohm, bool persist = true);
    void setFixedRes(float rFixedOhm, bool persist = true);
    void setSampleCount(uint8_t samples, bool persist = true);
    void setTempLimits(float minC, float maxC, bool persist = true);
    void setButtonThresholdsMv(float pressMv, float releaseMv, uint32_t debounceMs, bool persist = true);
    bool setSteinhartCoefficients(float a, float b, float c, bool persist = true);
    void setModel(Model model, bool persist = true);
    bool calibrateAtTempC(float refTempC);

    float getBeta() const;
    float getT0C() const;
    float getR0() const;
    float getFixedRes() const;
    Model getModel() const;
    bool getSteinhartCoefficients(float& a, float& b, float& c) const;
    bool hasSteinhartCoefficients() const;

private:
    NtcSensor() = default;

    uint16_t sampleAdcAveraged() const;
    uint16_t sampleAdcMedian9() const;
    float    adcToVolts(uint16_t adc) const;
    float    computeResistance(float volts) const;
    float    computeTempC(float rNtcOhm) const;
    void     updateButtonState(float volts, uint32_t nowMs);
    bool     isSteinhartValid(float a, float b, float c) const;

    inline bool lock(TickType_t timeoutTicks = portMAX_DELAY) const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    SemaphoreHandle_t _mutex = nullptr;

    uint8_t _pin     = POWER_ON_SWITCH_PIN;
    bool    _started = false;

    float _vRef      = NTC_ADC_REF_VOLTAGE;
    float _adcMax    = NTC_ADC_MAX;
    float _rFixedOhm = DEFAULT_NTC_FIXED_RES_OHMS;
    float _r0Ohm     = DEFAULT_NTC_R0_OHMS;
    float _beta      = DEFAULT_NTC_BETA;
    float _shA       = DEFAULT_NTC_SH_A;
    float _shB       = DEFAULT_NTC_SH_B;
    float _shC       = DEFAULT_NTC_SH_C;
    bool  _shValid   = false;
    Model _model     = static_cast<Model>(DEFAULT_NTC_MODEL);
    float _t0K       = DEFAULT_NTC_T0_C + 273.15f;
    float _minTempC  = DEFAULT_NTC_MIN_C;
    float _maxTempC  = DEFAULT_NTC_MAX_C;
    uint8_t _samples = DEFAULT_NTC_SAMPLES;

    float    _pressV        = DEFAULT_NTC_PRESS_MV / 1000.0f;
    float    _releaseV      = DEFAULT_NTC_RELEASE_MV / 1000.0f;
    uint32_t _debounceMs    = DEFAULT_NTC_DEBOUNCE_MS;
    bool     _pressed       = false;
    bool     _candidate     = false;
    uint32_t _candidateMs   = 0;

    Sample _last{};
    float  _lastValidTempC = NAN;
    bool   _lastValid      = false;

    float  _emaTempC  = NAN;
    bool   _emaValid  = false;
    float  _emaAlpha  = 0.15f;

    static NtcSensor* s_instance;
};

#define NTC NtcSensor::Get()

#endif // NTC_SENSOR_H
