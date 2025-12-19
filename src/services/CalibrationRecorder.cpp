#include "services/CalibrationRecorder.h"
#include "sensing/BusSampler.h"
#include "sensing/NtcSensor.h"
#include "control/HeaterManager.h"
#include "services/NVSManager.h"
#include <math.h>
#include <limits.h>
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

static constexpr uint32_t CALIB_MAGIC = 0x31424C43; // "CLB1"
static constexpr uint16_t CALIB_VERSION = 1;
static constexpr float    TEMP_SCALE = 100.0f;
static constexpr float    VOLT_SCALE = 10.0f;
static constexpr float    CURR_SCALE = 100.0f;

#pragma pack(push, 1)
struct CalibBinHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t sampleCount;
    uint16_t sampleSize;
    uint16_t mode;
    uint32_t intervalMs;
    uint32_t startMs;
    float    targetTempC;
    uint8_t  wireIndex;
    uint8_t  reserved[3];
};

struct CalibBinSample {
    uint32_t tMs;
    int16_t  tempCx100;
    int16_t  voltVx10;
    int16_t  currAx100;
    uint8_t  flags;
    uint8_t  reserved;
};
#pragma pack(pop)

static inline int16_t clampToI16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return static_cast<int16_t>(lroundf(v));
}

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
    if (wireIndex == 0 && CONF) {
        int idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
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
    _startMs     = millis();
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
        ok = saveToFile();
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
    _intervalMs = kDefaultIntervalMs;
    _saveOnStop = false;
    _lastSaveOk = false;
    _lastSaveMs = 0;
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
        m.intervalMs  = _intervalMs;
        m.count       = _count;
        m.capacity    = _capacity;
        m.targetTempC = _targetTempC;
        m.wireIndex   = _wireIndex;
        m.saved       = _lastSaveOk;
        m.savedMs     = _lastSaveMs;
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

    CalibBinHeader hdr{};
    hdr.magic = CALIB_MAGIC;
    hdr.version = CALIB_VERSION;
    hdr.sampleCount = _count;
    hdr.sampleSize = sizeof(CalibBinSample);
    hdr.mode = static_cast<uint16_t>(_mode);
    hdr.intervalMs = _intervalMs;
    hdr.startMs = _startMs;
    hdr.targetTempC = _targetTempC;
    hdr.wireIndex = _wireIndex;

    bool ok = true;
    if (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        ok = false;
    }

    auto encode = [](float v, float scale) -> int16_t {
        if (!isfinite(v)) return INT16_MIN;
        return clampToI16(v * scale);
    };

    if (ok) {
        for (uint16_t i = 0; i < _count; ++i) {
            const Sample& s = _buf[i];
            CalibBinSample b{};
            b.tMs = s.tMs;
            b.tempCx100 = encode(s.tempC, TEMP_SCALE);
            b.voltVx10  = encode(s.voltageV, VOLT_SCALE);
            b.currAx100 = encode(s.currentA, CURR_SCALE);
            b.flags = 0;
            if (s.ntcValid) b.flags |= 0x01;
            if (s.pressed)  b.flags |= 0x02;

            if (f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b)) != sizeof(b)) {
                ok = false;
                break;
            }
        }
    }

    f.close();

    _lastSaveOk = ok;
    _lastSaveMs = millis();

    unlock();
    return ok;
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
                saveToFile();
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
