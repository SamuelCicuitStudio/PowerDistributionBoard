#include <NtcSensor.hpp>
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
    float shA = DEFAULT_NTC_SH_A;
    float shB = DEFAULT_NTC_SH_B;
    float shC = DEFAULT_NTC_SH_C;
    int model = static_cast<int>(Model::Beta);
    float pressMv = DEFAULT_NTC_PRESS_MV;
    float releaseMv = DEFAULT_NTC_RELEASE_MV;
    float minC = DEFAULT_NTC_MIN_C;
    float maxC = DEFAULT_NTC_MAX_C;
    int samples = DEFAULT_NTC_SAMPLES;
    int debounceMs = DEFAULT_NTC_DEBOUNCE_MS;
    if (!isfinite(beta) || beta <= 0.0f) beta = DEFAULT_NTC_BETA;
    if (!isfinite(r0)   || r0   <= 0.0f) r0   = DEFAULT_NTC_R0_OHMS;
    if (!isfinite(rFixed) || rFixed <= 0.0f) rFixed = DEFAULT_NTC_FIXED_RES_OHMS;
    if (!isfinite(shA)) shA = DEFAULT_NTC_SH_A;
    if (!isfinite(shB)) shB = DEFAULT_NTC_SH_B;
    if (!isfinite(shC)) shC = DEFAULT_NTC_SH_C;
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
    model = static_cast<int>(Model::Beta);

    if (lock()) {
        _beta  = beta;
        _r0Ohm = r0;
        _t0K   = DEFAULT_NTC_T0_C + 273.15f;
        _minTempC = minC;
        _maxTempC = maxC;
        _rFixedOhm = rFixed;
        _samples = static_cast<uint8_t>(samples);
        _shA = shA;
        _shB = shB;
        _shC = shC;
        _shValid = false;
        _model = static_cast<Model>(model);

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
        _emaTempC = NAN;
        _emaValid = false;
        unlock();
    }

    _started = true;
    update();
}

void NtcSensor::update() {
    if (!_started) return;

    if (!lock()) return;

    const uint16_t adc = sampleAdcMedian9();
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
            const float rawTempC = computeTempC(rNtc);
            if (isfinite(rawTempC) && rawTempC >= _minTempC && rawTempC <= _maxTempC) {
                if (!_emaValid) {
                    _emaTempC = rawTempC;
                    _emaValid = true;
                } else {
                    _emaTempC = (_emaAlpha * rawTempC) + ((1.0f - _emaAlpha) * _emaTempC);
                }
                tempC = _emaTempC;
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
}

void NtcSensor::setR0(float r0Ohm, bool persist) {
    if (!isfinite(r0Ohm) || r0Ohm <= 0.0f) return;
    if (lock()) {
        _r0Ohm = r0Ohm;
        unlock();
    }
}

void NtcSensor::setFixedRes(float rFixedOhm, bool persist) {
    if (!isfinite(rFixedOhm) || rFixedOhm <= 0.0f) return;
    if (lock()) {
        _rFixedOhm = rFixedOhm;
        unlock();
    }
}

void NtcSensor::setSampleCount(uint8_t samples, bool persist) {
    if (samples == 0) samples = 1;
    if (lock()) {
        _samples = samples;
        unlock();
    }
}

void NtcSensor::setTempLimits(float minC, float maxC, bool persist) {
    if (!isfinite(minC) || !isfinite(maxC) || minC >= maxC) return;
    if (lock()) {
        _minTempC = minC;
        _maxTempC = maxC;
        unlock();
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
}

bool NtcSensor::setSteinhartCoefficients(float a, float b, float c, bool persist) {
    (void)a;
    (void)b;
    (void)c;
    (void)persist;
    return false;
}

void NtcSensor::setModel(Model model, bool persist) {
    (void)model;
    (void)persist;
    if (lock()) {
        _model = Model::Beta;
        unlock();
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

NtcSensor::Model NtcSensor::getModel() const {
    return Model::Beta;
}

bool NtcSensor::getSteinhartCoefficients(float& a, float& b, float& c) const {
    bool valid = false;
    if (const_cast<NtcSensor*>(this)->lock()) {
        a = _shA;
        b = _shB;
        c = _shC;
        valid = _shValid;
        const_cast<NtcSensor*>(this)->unlock();
    } else {
        a = _shA;
        b = _shB;
        c = _shC;
        valid = _shValid;
    }
    return valid;
}

bool NtcSensor::hasSteinhartCoefficients() const {
    bool valid = false;
    if (const_cast<NtcSensor*>(this)->lock()) {
        valid = _shValid;
        const_cast<NtcSensor*>(this)->unlock();
    } else {
        valid = _shValid;
    }
    return valid;
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

uint16_t NtcSensor::sampleAdcMedian9() const {
    constexpr uint8_t kSamples = 9;
    uint16_t buf[kSamples] = {};

    for (uint8_t i = 0; i < kSamples; ++i) {
        buf[i] = analogRead(_pin);
        delayMicroseconds(80);
    }

    for (uint8_t i = 1; i < kSamples; ++i) {
        uint16_t key = buf[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && buf[j] > key) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }

    return buf[kSamples / 2];
}

float NtcSensor::adcToVolts(uint16_t adc) const {
    if (adc > _adcMax) {
        adc = static_cast<uint16_t>(_adcMax);
    }
    if (_adcMax <= 0.0f) return NAN;
    return (static_cast<float>(adc) / _adcMax) * _vRef;
}

float NtcSensor::computeResistance(float volts) const {
    if (!isfinite(volts)) return NAN;
    if (!isfinite(_vRef) || _vRef <= 0.0f) return NAN;
    if (!isfinite(_rFixedOhm) || _rFixedOhm <= 0.0f) return NAN;
    if (volts <= 0.0f || volts >= _vRef) return NAN;

    // Divider: 3.3V -> Rfixed -> ADC -> NTC -> GND.
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

bool NtcSensor::isSteinhartValid(float a, float b, float c) const {
    if (!isfinite(a) || !isfinite(b) || !isfinite(c)) return false;
    const float sum = fabsf(a) + fabsf(b) + fabsf(c);
    return sum > 0.0f;
}
