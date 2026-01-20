#include <CalibrationRecorder.hpp>
#include <BusSampler.hpp>
#include <NtcSensor.hpp>
#include <Device.hpp>
#include <HeaterManager.hpp>
#include <NVSManager.hpp>
#include <RTCManager.hpp>
#include <CborStream.hpp>
#include <math.h>
#include <stdio.h>
#include <FS.h>
#include <SPIFFS.h>

#ifdef ESP32
#include "esp_heap_caps.h"
#endif

CalibrationRecorder* CalibrationRecorder::s_instance = nullptr;

// Static constexpr definitions (needed for this toolchain/linker)
constexpr uint32_t CalibrationRecorder::kDefaultIntervalMs;
constexpr uint16_t CalibrationRecorder::kDefaultMaxSamples;
constexpr uint16_t CalibrationRecorder::kAbsoluteMaxSamples;

void CalibrationRecorder::Init() {
    if (!s_instance) {
        s_instance = new CalibrationRecorder();
    }
}

CalibrationRecorder* CalibrationRecorder::Get() {
    if (!s_instance) {
        s_instance = new CalibrationRecorder();
    }
    return s_instance;
}

bool CalibrationRecorder::start(Mode mode,
                                uint32_t intervalMs,
                                uint16_t maxSamples,
                                float targetTempC,
                                uint8_t wireIndex)
{
    if (mode == Mode::None) return false;
    if (!BUS_SAMPLER) return false;
    BUS_SAMPLER->attachNtc(NTC);

    if (intervalMs < 50) intervalMs = 50;
    if (intervalMs > 5000) intervalMs = 5000;

    if (maxSamples == 0) maxSamples = kDefaultMaxSamples;
    if (maxSamples > kAbsoluteMaxSamples) maxSamples = kAbsoluteMaxSamples;
    if (wireIndex == 0) {
        int idx = DEFAULT_NTC_GATE_INDEX;
        if (idx < 1) idx = 1;
        if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
        wireIndex = static_cast<uint8_t>(idx);
    }

    if (!lock()) return false;
    if (_running) {
        unlock();
        return false;
    }

    freeBufferLocked();

#ifdef ESP32
    _buf = static_cast<Sample*>(
        heap_caps_malloc(maxSamples * sizeof(Sample),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (_buf == nullptr) {
        _buf = static_cast<Sample*>(
            heap_caps_malloc(maxSamples * sizeof(Sample),
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
#else
    _buf = static_cast<Sample*>(malloc(maxSamples * sizeof(Sample)));
#endif

    if (_buf == nullptr) {
        unlock();
        return false;
    }

    _capacity    = maxSamples;
    _count       = 0;
    _mode        = mode;
    _running     = true;
    _saveOnStop  = false;
    _lastSaveOk  = false;
    _lastSaveMs  = 0;
    _lastSaveEpoch = 0;
    _startMs     = millis();
    _startEpoch  = 0;
    if (RTC) {
        _startEpoch = static_cast<uint32_t>(RTC->getUnixTime());
    }
    _intervalMs  = intervalMs;
    _targetTempC = targetTempC;
    _wireIndex   = wireIndex;

    unlock();

    if (_taskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            CalibrationRecorder::taskThunk,
            "CalibRec",
            4096,
            this,
            1,
            &_taskHandle
        );
        if (ok != pdPASS) {
            if (lock()) {
                _running = false;
                freeBufferLocked();
                unlock();
            }
            _taskHandle = nullptr;
            return false;
        }
    }

    return true;
}

void CalibrationRecorder::stop() {
    if (!lock()) return;
    _running = false;
    _saveOnStop = false;
    unlock();
}

bool CalibrationRecorder::stopAndSave(uint32_t timeoutMs) {
    if (!lock()) return false;
    _running = false;
    _saveOnStop = true;
    unlock();

    const uint32_t start = millis();
    while (true) {
        bool done = false;
        if (lock()) {
            done = (_taskHandle == nullptr);
            unlock();
        }
        if (done) break;
        if ((millis() - start) >= timeoutMs) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bool ok = false;
    bool hasTask = false;
    if (lock()) {
        ok = _lastSaveOk;
        hasTask = (_taskHandle != nullptr);
        unlock();
    }
    if (!hasTask && !ok) {
        ok = saveToHistoryFiles();
    }
    return ok;
}

void CalibrationRecorder::clear() {
    if (!lock()) return;
    if (_running) {
        unlock();
        return;
    }
    freeBufferLocked();
    _mode = Mode::None;
    _targetTempC = NAN;
    _wireIndex = 0;
    _startMs = 0;
    _startEpoch = 0;
    _intervalMs = kDefaultIntervalMs;
    _saveOnStop = false;
    _lastSaveOk = false;
    _lastSaveMs = 0;
    _lastSaveEpoch = 0;
    unlock();
}

bool CalibrationRecorder::isRunning() const {
    bool r = false;
    if (const_cast<CalibrationRecorder*>(this)->lock()) {
        r = _running;
        const_cast<CalibrationRecorder*>(this)->unlock();
    }
    return r;
}

uint16_t CalibrationRecorder::getSampleCount() const {
    uint16_t n = 0;
    if (const_cast<CalibrationRecorder*>(this)->lock()) {
        n = _count;
        const_cast<CalibrationRecorder*>(this)->unlock();
    }
    return n;
}

CalibrationRecorder::Meta CalibrationRecorder::getMeta() const {
    Meta m{};
    if (const_cast<CalibrationRecorder*>(this)->lock()) {
        m.mode        = _mode;
        m.running     = _running;
        m.startMs     = _startMs;
        m.startEpoch  = _startEpoch;
        m.intervalMs  = _intervalMs;
        m.count       = _count;
        m.capacity    = _capacity;
        m.targetTempC = _targetTempC;
        m.wireIndex   = _wireIndex;
        m.saved       = _lastSaveOk;
        m.savedMs     = _lastSaveMs;
        m.savedEpoch  = _lastSaveEpoch;
        const_cast<CalibrationRecorder*>(this)->unlock();
    }
    return m;
}

size_t CalibrationRecorder::copySamples(uint16_t offset, Sample* out, size_t maxOut) const {
    if (!out || maxOut == 0) return 0;
    if (!const_cast<CalibrationRecorder*>(this)->lock()) return 0;

    if (offset >= _count || _buf == nullptr) {
        const_cast<CalibrationRecorder*>(this)->unlock();
        return 0;
    }

    const uint16_t available = _count - offset;
    size_t n = (available < maxOut) ? available : maxOut;
    for (size_t i = 0; i < n; ++i) {
        out[i] = _buf[offset + i];
    }

    const_cast<CalibrationRecorder*>(this)->unlock();
    return n;
}

bool CalibrationRecorder::saveToFile(const char* path) {
    if (!path || !path[0]) return false;
    if (!SPIFFS.begin(false)) return false;

    if (!lock()) return false;
    if (_buf == nullptr || _count == 0) {
        unlock();
        return false;
    }

    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) {
        unlock();
        return false;
    }

    const char* modeStr =
        (_mode == Mode::Ntc)   ? "ntc" :
        (_mode == Mode::Model) ? "model" :
        (_mode == Mode::Floor) ? "floor" :
        "none";

    const bool saveOk = true;

    uint8_t metaCount = 8;
    if (_startEpoch > 0) metaCount++;
    if (isfinite(_targetTempC)) metaCount++;
    if (_wireIndex > 0) metaCount++;
    if (_lastSaveEpoch > 0) metaCount++;

    bool ok = true;
    ok = ok && CborStream::writeMapHeader(f, 2);
    ok = ok && CborStream::writeText(f, "meta");
    ok = ok && CborStream::writeMapHeader(f, metaCount);
    ok = ok && CborStream::writeText(f, "mode");
    ok = ok && CborStream::writeText(f, modeStr);
    ok = ok && CborStream::writeText(f, "running");
    ok = ok && CborStream::writeBool(f, _running);
    ok = ok && CborStream::writeText(f, "count");
    ok = ok && CborStream::writeUInt(f, _count);
    ok = ok && CborStream::writeText(f, "capacity");
    ok = ok && CborStream::writeUInt(f, _capacity);
    ok = ok && CborStream::writeText(f, "interval_ms");
    ok = ok && CborStream::writeUInt(f, _intervalMs);
    ok = ok && CborStream::writeText(f, "start_ms");
    ok = ok && CborStream::writeUInt(f, _startMs);
    if (_startEpoch > 0) {
        ok = ok && CborStream::writeText(f, "start_epoch");
        ok = ok && CborStream::writeUInt(f, _startEpoch);
    }
    if (isfinite(_targetTempC)) {
        ok = ok && CborStream::writeText(f, "target_c");
        ok = ok && CborStream::writeDouble(f, _targetTempC);
    }
    if (_wireIndex > 0) {
        ok = ok && CborStream::writeText(f, "wire_index");
        ok = ok && CborStream::writeUInt(f, _wireIndex);
    }
    ok = ok && CborStream::writeText(f, "saved");
    ok = ok && CborStream::writeBool(f, saveOk);
    ok = ok && CborStream::writeText(f, "saved_ms");
    ok = ok && CborStream::writeUInt(f, _lastSaveMs);
    if (_lastSaveEpoch > 0) {
        ok = ok && CborStream::writeText(f, "saved_epoch");
        ok = ok && CborStream::writeUInt(f, _lastSaveEpoch);
    }

    ok = ok && CborStream::writeText(f, "samples");
    ok = ok && CborStream::writeArrayHeader(f, _count);

    for (uint16_t i = 0; i < _count && ok; ++i) {
        const Sample& s = _buf[i];
        ok = ok && CborStream::writeMapHeader(f, 10);
        ok = ok && CborStream::writeText(f, "t_ms");
        ok = ok && CborStream::writeUInt(f, s.tMs);
        ok = ok && CborStream::writeText(f, "v");
        ok = ok && CborStream::writeFloatOrNull(f, s.voltageV);
        ok = ok && CborStream::writeText(f, "i");
        ok = ok && CborStream::writeFloatOrNull(f, s.currentA);
        ok = ok && CborStream::writeText(f, "temp_c");
        ok = ok && CborStream::writeFloatOrNull(f, s.tempC);
        ok = ok && CborStream::writeText(f, "room_c");
        ok = ok && CborStream::writeFloatOrNull(f, s.roomTempC);
        ok = ok && CborStream::writeText(f, "ntc_v");
        ok = ok && CborStream::writeFloatOrNull(f, s.ntcVolts);
        ok = ok && CborStream::writeText(f, "ntc_ohm");
        ok = ok && CborStream::writeFloatOrNull(f, s.ntcOhm);
        ok = ok && CborStream::writeText(f, "ntc_adc");
        ok = ok && CborStream::writeUInt(f, s.ntcAdc);
        ok = ok && CborStream::writeText(f, "ntc_ok");
        ok = ok && CborStream::writeBool(f, s.ntcValid);
        ok = ok && CborStream::writeText(f, "pressed");
        ok = ok && CborStream::writeBool(f, s.pressed);
    }

    f.close();
    if (!ok) {
        SPIFFS.remove(path);
        unlock();
        return false;
    }

    unlock();
    return saveOk;
}

void CalibrationRecorder::taskThunk(void* param) {
    auto* self = static_cast<CalibrationRecorder*>(param);
    if (self) {
        self->taskLoop();
    }
    vTaskDelete(nullptr);
}

void CalibrationRecorder::taskLoop() {
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        bool running = false;
        bool saveOnStop = false;
        uint32_t startMs = 0;
        if (lock()) {
            running = _running;
            saveOnStop = _saveOnStop;
            startMs = _startMs;
            unlock();
        }

        if (!running) {
            if (saveOnStop) {
                saveToHistoryFiles();
                if (lock()) {
                    _saveOnStop = false;
                    unlock();
                }
            }
            break;
        }

        BusSampler::SyncSample s{};
        if (!BUS_SAMPLER || !BUS_SAMPLER->sampleNow(s)) {
            continue;
        }

        float roomC = NAN;
        if (DEVICE && DEVICE->tempSensor) {
            roomC = DEVICE->tempSensor->getHeatsinkTemp();
        }

        if (!lock()) {
            continue;
        }

        if (!_running || _buf == nullptr || _count >= _capacity) {
            _running = false;
            if (_count >= _capacity) {
                _saveOnStop = true;
            }
            unlock();
            break;
        }

        Sample& dst = _buf[_count++];
        const uint32_t nowMs = s.timestampMs;
        dst.tMs      = (nowMs >= startMs) ? (nowMs - startMs) : 0;
        dst.voltageV = s.voltageV;
        dst.currentA = s.currentA;
        dst.tempC    = s.tempC;
        dst.roomTempC = roomC;
        dst.ntcVolts = s.ntcVolts;
        dst.ntcOhm   = s.ntcOhm;
        dst.ntcAdc   = s.ntcAdc;
        dst.ntcValid = s.ntcValid;
        dst.pressed  = s.pressed;

        unlock();

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(_intervalMs));
    }

    if (lock()) {
        _taskHandle = nullptr;
        unlock();
    }
}

bool CalibrationRecorder::saveToHistoryFiles() {
    uint32_t saveEpoch = 0;
    if (RTC) {
        saveEpoch = static_cast<uint32_t>(RTC->getUnixTime());
    }
    const uint32_t saveMs = millis();
    uint32_t startEpoch = _startEpoch;

    if (lock()) {
        if (saveEpoch > 0) {
            const uint32_t elapsedMs = (saveMs >= _startMs) ? (saveMs - _startMs) : 0;
            const uint32_t elapsedSec = elapsedMs / 1000;
            if (saveEpoch > elapsedSec) {
                _startEpoch = saveEpoch - elapsedSec;
            }
            _lastSaveEpoch = saveEpoch;
        }
        _lastSaveMs = saveMs;
        startEpoch = _startEpoch;
        unlock();
    }

    bool okLatest = saveToFile(CALIB_MODEL_CBOR_FILE);
    bool okHistory = true;

    if (startEpoch > 0) {
        if (SPIFFS.begin(false)) {
            SPIFFS.mkdir(CALIB_HISTORY_DIR);
        }
        char path[64];
        snprintf(path, sizeof(path), "%s%lu%s",
                 CALIB_HISTORY_PREFIX,
                 static_cast<unsigned long>(startEpoch),
                 CALIB_HISTORY_EXT);
        okHistory = saveToFile(path);
    }

    const bool okAll = okLatest && okHistory;
    if (lock()) {
        _lastSaveOk = okAll;
        unlock();
    }
    return okAll;
}

void CalibrationRecorder::freeBufferLocked() {
    if (_buf) {
#ifdef ESP32
        heap_caps_free(_buf);
#else
        free(_buf);
#endif
        _buf = nullptr;
    }
    _capacity = 0;
    _count = 0;
}
