#include "sensing/NtcSensor.h"
#include <math.h>

NtcSensor* NtcSensor::s_instance = nullptr;

void NtcSensor::Init() {
    if (!s_instance) {
        s_instance = new NtcSensor();
    }
}

NtcSensor* NtcSensor::Get() {
    if (!s_instance) {
        s_instance = new NtcSensor();
    }
    return s_instance;
}

void NtcSensor::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT);

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }

    float beta = DEFAULT_NTC_BETA;
    float r0   = DEFAULT_NTC_R0_OHMS;
    float rFixed = DEFAULT_NTC_FIXED_RES_OHMS;
    float pressMv = DEFAULT_NTC_PRESS_MV;
    float releaseMv = DEFAULT_NTC_RELEASE_MV;
    float minC = DEFAULT_NTC_MIN_C;
    float maxC = DEFAULT_NTC_MAX_C;
    int samples = DEFAULT_NTC_SAMPLES;
    int debounceMs = DEFAULT_NTC_DEBOUNCE_MS;
    if (CONF) {
        beta = CONF->GetFloat(NTC_BETA_KEY, DEFAULT_NTC_BETA);
        r0   = CONF->GetFloat(NTC_R0_KEY, DEFAULT_NTC_R0_OHMS);
        rFixed   = CONF->GetFloat(NTC_FIXED_RES_KEY, DEFAULT_NTC_FIXED_RES_OHMS);
        pressMv  = CONF->GetFloat(NTC_PRESS_MV_KEY, DEFAULT_NTC_PRESS_MV);
        releaseMv= CONF->GetFloat(NTC_RELEASE_MV_KEY, DEFAULT_NTC_RELEASE_MV);
        minC     = CONF->GetFloat(NTC_MIN_C_KEY, DEFAULT_NTC_MIN_C);
        maxC     = CONF->GetFloat(NTC_MAX_C_KEY, DEFAULT_NTC_MAX_C);
        samples  = CONF->GetInt(NTC_SAMPLES_KEY, DEFAULT_NTC_SAMPLES);
        debounceMs = CONF->GetInt(NTC_DEBOUNCE_MS_KEY, DEFAULT_NTC_DEBOUNCE_MS);
    }
    if (!isfinite(beta) || beta <= 0.0f) beta = DEFAULT_NTC_BETA;
    if (!isfinite(r0)   || r0   <= 0.0f) r0   = DEFAULT_NTC_R0_OHMS;
    if (!isfinite(rFixed) || rFixed <= 0.0f) rFixed = DEFAULT_NTC_FIXED_RES_OHMS;
    if (!isfinite(pressMv) || pressMv < 0.0f) pressMv = DEFAULT_NTC_PRESS_MV;
    if (!isfinite(releaseMv) || releaseMv < pressMv) releaseMv = pressMv;
    if (!isfinite(minC)) minC = DEFAULT_NTC_MIN_C;
    if (!isfinite(maxC)) maxC = DEFAULT_NTC_MAX_C;
    if (minC >= maxC) {
        minC = DEFAULT_NTC_MIN_C;
        maxC = DEFAULT_NTC_MAX_C;
    }
    if (samples <= 0) samples = DEFAULT_NTC_SAMPLES;
    if (debounceMs < 0) debounceMs = DEFAULT_NTC_DEBOUNCE_MS;

    if (lock()) {
        _beta  = beta;
        _r0Ohm = r0;
        _t0K   = DEFAULT_NTC_T0_C + 273.15f;
        _minTempC = minC;
        _maxTempC = maxC;
        _rFixedOhm = rFixed;
        _samples = static_cast<uint8_t>(samples);

        _pressV   = pressMv / 1000.0f;
        _releaseV = releaseMv / 1000.0f;
        if (_releaseV < _pressV) _releaseV = _pressV;
        _debounceMs = static_cast<uint32_t>(debounceMs);

        _pressed = false;
        _candidate = false;
        _candidateMs = 0;

        _last = Sample{};
        _lastValidTempC = NAN;
        _lastValid = false;
        unlock();
    }

    _started = true;
    update();
}

void NtcSensor::update() {
    if (!_started) return;

    if (!lock()) return;

    const uint16_t adc = sampleAdcAveraged();
    const uint32_t nowMs = millis();
    const float volts = adcToVolts(adc);

    updateButtonState(volts, nowMs);
    const bool pressed = _pressed;

    float rNtc = NAN;
    float tempC = NAN;
    bool valid = false;

    if (!pressed) {
        rNtc = computeResistance(volts);
        if (isfinite(rNtc) && rNtc > 0.0f) {
            tempC = computeTempC(rNtc);
            if (isfinite(tempC) && tempC >= _minTempC && tempC <= _maxTempC) {
                valid = true;
                _lastValidTempC = tempC;
                _lastValid = true;
            }
        }
    }

    _last.timestampMs = nowMs;
    _last.adcRaw      = adc;
    _last.volts       = volts;
    _last.rNtcOhm     = rNtc;
    _last.tempC       = tempC;
    _last.valid       = valid;
    _last.pressed     = pressed;

    unlock();
}

NtcSensor::Sample NtcSensor::getLastSample() const {
    Sample out{};
    if (const_cast<NtcSensor*>(this)->lock()) {
        out = _last;
        const_cast<NtcSensor*>(this)->unlock();
    }
    return out;
}

float NtcSensor::getLastTempC() const {
    float t = NAN;
    if (const_cast<NtcSensor*>(this)->lock()) {
        if (_lastValid) {
            t = _lastValidTempC;
        }
        const_cast<NtcSensor*>(this)->unlock();
    }
    return t;
}

bool NtcSensor::isPressed() const {
    bool pressed = false;
    if (const_cast<NtcSensor*>(this)->lock()) {
        pressed = _pressed;
        const_cast<NtcSensor*>(this)->unlock();
    }
    return pressed;
}

void NtcSensor::setBeta(float beta, bool persist) {
    if (!isfinite(beta) || beta <= 0.0f) return;
    if (lock()) {
        _beta = beta;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutFloat(NTC_BETA_KEY, beta);
    }
}

void NtcSensor::setR0(float r0Ohm, bool persist) {
    if (!isfinite(r0Ohm) || r0Ohm <= 0.0f) return;
    if (lock()) {
        _r0Ohm = r0Ohm;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutFloat(NTC_R0_KEY, r0Ohm);
    }
}

void NtcSensor::setFixedRes(float rFixedOhm, bool persist) {
    if (!isfinite(rFixedOhm) || rFixedOhm <= 0.0f) return;
    if (lock()) {
        _rFixedOhm = rFixedOhm;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutFloat(NTC_FIXED_RES_KEY, rFixedOhm);
    }
}

void NtcSensor::setSampleCount(uint8_t samples, bool persist) {
    if (samples == 0) samples = 1;
    if (lock()) {
        _samples = samples;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutInt(NTC_SAMPLES_KEY, samples);
    }
}

void NtcSensor::setTempLimits(float minC, float maxC, bool persist) {
    if (!isfinite(minC) || !isfinite(maxC) || minC >= maxC) return;
    if (lock()) {
        _minTempC = minC;
        _maxTempC = maxC;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutFloat(NTC_MIN_C_KEY, minC);
        CONF->PutFloat(NTC_MAX_C_KEY, maxC);
    }
}

void NtcSensor::setButtonThresholdsMv(float pressMv, float releaseMv, uint32_t debounceMs, bool persist) {
    if (pressMv < 0.0f) pressMv = 0.0f;
    if (releaseMv < pressMv) releaseMv = pressMv;
    if (lock()) {
        _pressV = pressMv / 1000.0f;
        _releaseV = releaseMv / 1000.0f;
        _debounceMs = debounceMs;
        unlock();
    }
    if (persist && CONF) {
        CONF->PutFloat(NTC_PRESS_MV_KEY, pressMv);
        CONF->PutFloat(NTC_RELEASE_MV_KEY, releaseMv);
        CONF->PutInt(NTC_DEBOUNCE_MS_KEY, static_cast<int>(debounceMs));
    }
}

bool NtcSensor::calibrateAtTempC(float refTempC) {
    if (!isfinite(refTempC)) return false;
    update();

    float volts = NAN;
    float beta = DEFAULT_NTC_BETA;
    float t0K = DEFAULT_NTC_T0_C + 273.15f;
    bool pressed = false;

    if (!lock()) return false;
    volts   = _last.volts;
    pressed = _last.pressed;
    beta    = _beta;
    t0K     = _t0K;
    unlock();

    if (pressed) return false;

    const float rNtc = computeResistance(volts);
    if (!isfinite(rNtc) || rNtc <= 0.0f || !isfinite(beta) || beta <= 0.0f) {
        return false;
    }

    const float tRefK = refTempC + 273.15f;
    if (tRefK <= 0.0f) return false;

    const float r0 = rNtc / expf(beta * (1.0f / tRefK - 1.0f / t0K));
    if (!isfinite(r0) || r0 <= 0.0f) return false;

    setR0(r0, true);
    return true;
}

float NtcSensor::getBeta() const {
    float v = DEFAULT_NTC_BETA;
    if (const_cast<NtcSensor*>(this)->lock()) {
        v = _beta;
        const_cast<NtcSensor*>(this)->unlock();
    }
    return v;
}

float NtcSensor::getR0() const {
    float v = DEFAULT_NTC_R0_OHMS;
    if (const_cast<NtcSensor*>(this)->lock()) {
        v = _r0Ohm;
        const_cast<NtcSensor*>(this)->unlock();
    }
    return v;
}

float NtcSensor::getFixedRes() const {
    float v = DEFAULT_NTC_FIXED_RES_OHMS;
    if (const_cast<NtcSensor*>(this)->lock()) {
        v = _rFixedOhm;
        const_cast<NtcSensor*>(this)->unlock();
    }
    return v;
}

uint16_t NtcSensor::sampleAdcAveraged() const {
    uint8_t samples = _samples;
    if (samples == 0) samples = 1;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; ++i) {
        sum += analogRead(_pin);
        if (samples > 1) {
            delayMicroseconds(80);
        }
    }
    return static_cast<uint16_t>(sum / samples);
}

float NtcSensor::adcToVolts(uint16_t adc) const {
    if (adc > _adcMax) {
        adc = static_cast<uint16_t>(_adcMax);
    }
    if (_adcMax <= 0.0f) return NAN;
    return (static_cast<float>(adc) / _adcMax) * _vRef;
}

float NtcSensor::computeResistance(float volts) const {
    if (!isfinite(volts) || volts <= 0.0f || volts >= _vRef) return NAN;
    const float denom = (_vRef - volts);
    if (denom <= 0.0f) return NAN;
    return (_rFixedOhm * volts) / denom;
}

float NtcSensor::computeTempC(float rNtcOhm) const {
    if (!isfinite(rNtcOhm) || rNtcOhm <= 0.0f) return NAN;
    if (!isfinite(_r0Ohm) || _r0Ohm <= 0.0f) return NAN;
    if (!isfinite(_beta) || _beta <= 0.0f) return NAN;

    const float lnRatio = logf(rNtcOhm / _r0Ohm);
    const float invT = (1.0f / _t0K) + (lnRatio / _beta);
    if (invT <= 0.0f) return NAN;
    const float tempK = 1.0f / invT;
    return tempK - 273.15f;
}

void NtcSensor::updateButtonState(float volts, uint32_t nowMs) {
    bool next = _pressed;

    if (!_pressed) {
        if (volts <= _pressV) {
            next = true;
        }
    } else {
        if (volts >= _releaseV) {
            next = false;
        }
    }

    if (next == _pressed) {
        _candidate = _pressed;
        _candidateMs = 0;
        return;
    }

    if (_candidate != next) {
        _candidate = next;
        _candidateMs = nowMs;
        return;
    }

    if (_debounceMs == 0 || (nowMs - _candidateMs) >= _debounceMs) {
        _pressed = next;
        _candidateMs = 0;
    }
}
