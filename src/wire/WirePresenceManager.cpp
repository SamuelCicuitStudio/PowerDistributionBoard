#include <WirePresenceManager.hpp>

#include <HeaterManager.hpp>
#include <WireSubsystem.hpp>
#include <CpDischg.hpp>
#include <NVSManager.hpp>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t kProbeSettleMs = 20;
constexpr uint32_t kProbeSampleDelayMs = 10;
constexpr float kMinBusVoltage = 5.0f;
constexpr float kMinRatioFloor = 0.10f;
constexpr float kMinRatioCeil = 1.00f;
constexpr uint32_t kMinWindowMs = 20;
constexpr uint32_t kMaxWindowMs = 2000;

float resolvePresenceMinRatio() {
    float v = DEFAULT_PRESENCE_MIN_RATIO;
    if (CONF) {
        v = CONF->GetFloat(PRESENCE_MIN_RATIO_KEY, DEFAULT_PRESENCE_MIN_RATIO);
    }
    if (!isfinite(v) || v <= 0.0f) {
        v = DEFAULT_PRESENCE_MIN_RATIO;
    }
    if (v < kMinRatioFloor) v = kMinRatioFloor;
    if (v > kMinRatioCeil) v = kMinRatioCeil;
    return v;
}

uint32_t resolvePresenceWindowMs() {
    int v = DEFAULT_PRESENCE_WINDOW_MS;
    if (CONF) {
        v = CONF->GetInt(PRESENCE_WINDOW_MS_KEY, DEFAULT_PRESENCE_WINDOW_MS);
    }
    if (v < static_cast<int>(kMinWindowMs)) v = static_cast<int>(kMinWindowMs);
    if (v > static_cast<int>(kMaxWindowMs)) v = static_cast<int>(kMaxWindowMs);
    return static_cast<uint32_t>(v);
}

uint8_t resolvePresenceFailCount() {
    int v = DEFAULT_PRESENCE_FAIL_COUNT;
    if (CONF) {
        v = CONF->GetInt(PRESENCE_FAIL_COUNT_KEY, DEFAULT_PRESENCE_FAIL_COUNT);
    }
    if (v < 1) v = 1;
    if (v > 20) v = 20;
    return static_cast<uint8_t>(v);
}

float resolveChargeResOhms() {
    float r = DEFAULT_CHARGE_RESISTOR_OHMS;
    if (CONF) {
        r = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
    }
    if (!isfinite(r) || r <= 0.0f) {
        r = DEFAULT_CHARGE_RESISTOR_OHMS;
    }
    return r;
}

float computeLeakCurrent(float busVoltage) {
    if (!isfinite(busVoltage) || busVoltage <= 0.0f) return 0.0f;
    const float rCharge = resolveChargeResOhms();
    const float rTot = DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS + rCharge;
    if (!isfinite(rTot) || rTot <= 0.0f) return 0.0f;
    return busVoltage / rTot;
}

float sampleVoltageAverage(CpDischg* discharger, uint32_t windowMs) {
    if (!discharger) return NAN;
    if (windowMs == 0) return discharger->sampleVoltageNow();
    float sum = 0.0f;
    uint8_t count = 0;
    const uint32_t startMs = millis();
    while ((millis() - startMs) < windowMs) {
        const float v = discharger->sampleVoltageNow();
        if (isfinite(v)) {
            sum += v;
            ++count;
        }
        if (kProbeSampleDelayMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(kProbeSampleDelayMs));
        }
    }
    if (count == 0) return NAN;
    return sum / static_cast<float>(count);
}

float sampleCurrentAverage(CurrentSensor* current, uint32_t windowMs) {
    if (!current) return NAN;
    if (windowMs == 0) return current->readCurrent();
    float sum = 0.0f;
    uint8_t count = 0;
    const uint32_t startMs = millis();
    while ((millis() - startMs) < windowMs) {
        const float i = current->readCurrent();
        if (isfinite(i)) {
            sum += i;
            ++count;
        }
        if (kProbeSampleDelayMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(kProbeSampleDelayMs));
        }
    }
    if (count == 0) return NAN;
    return sum / static_cast<float>(count);
}
} // namespace

void WirePresenceManager::resetFailures() {
    for (uint8_t i = 0; i < kWireCount; ++i) {
        _failCount[i] = 0;
    }
}

void WirePresenceManager::setWirePresent_(HeaterManager& heater,
                                          WireStateModel& state,
                                          uint8_t index,
                                          bool present) {
    if (index == 0 || index > kWireCount) {
        return;
    }
    WireRuntimeState& ws = state.wire(index);
    ws.present = present;
    ws.lastUpdateMs = millis();
    heater.setWirePresence(index, present);
    if (present) {
        _failCount[index - 1] = 0;
    }
}

bool WirePresenceManager::probeAll(HeaterManager& heater,
                                   WireStateModel& state,
                                   const WireConfigStore& cfg,
                                   CpDischg* discharger,
                                   CurrentSensor* current) {
    if (!discharger || !current) {
        return false;
    }

    resetFailures();

    const uint16_t prevMask = heater.getOutputMask();
    heater.disableAll();

    const float minRatio = resolvePresenceMinRatio();
    const uint32_t settleMs = kProbeSettleMs;
    const uint32_t windowMs = resolvePresenceWindowMs();

    for (uint8_t i = 1; i <= kWireCount; ++i) {
        if (!cfg.getAccessFlag(i)) {
            setWirePresent_(heater, state, i, false);
            continue;
        }

        heater.setOutput(i, true);
        if (settleMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(settleMs));
        }

        const float vBus = sampleVoltageAverage(discharger, windowMs);
        const float iBus = sampleCurrentAverage(current, windowMs);

        heater.setOutput(i, false);

        if (!isfinite(vBus) || vBus <= kMinBusVoltage || !isfinite(iBus)) {
            setWirePresent_(heater, state, i, false);
            continue;
        }

        float rWire = cfg.getWireResistance(i);
        if (!isfinite(rWire) || rWire <= 0.01f) rWire = DEFAULT_WIRE_RES_OHMS;
        const float iExpected = vBus / rWire;
        if (!isfinite(iExpected) || iExpected <= 0.0f) {
            setWirePresent_(heater, state, i, false);
            continue;
        }

        const float iLeak = computeLeakCurrent(vBus);
        float iWire = fabsf(iBus) - iLeak;
        if (iWire < 0.0f) iWire = 0.0f;
        const float ratio = iWire / iExpected;
        const bool present = isfinite(ratio) && (ratio >= minRatio);
        setWirePresent_(heater, state, i, present);
    }

    if (prevMask != 0) {
        for (uint8_t i = 1; i <= kWireCount; ++i) {
            const bool on = (prevMask & (1u << (i - 1))) != 0;
            heater.setOutput(i, on);
        }
    }
    state.setLastMask(prevMask);
    return true;
}

bool WirePresenceManager::updatePresenceFromMask(HeaterManager& heater,
                                                 WireStateModel& state,
                                                 uint16_t mask,
                                                 float busVoltage,
                                                 float currentA) {
    if (mask == 0) return false;
    if (!isfinite(busVoltage) || busVoltage <= kMinBusVoltage) return false;
    if (!isfinite(currentA)) return false;

    const float minRatio = resolvePresenceMinRatio();
    const uint8_t failLimit = resolvePresenceFailCount();
    uint16_t eligibleMask = 0;
    double gTot = 0.0;

    for (uint8_t i = 0; i < kWireCount; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(mask & bit)) continue;

        const WireRuntimeState& ws = state.wire(i + 1);
        if (!ws.present) continue;
        if (ws.overTemp) continue;

        eligibleMask |= bit;
        WireInfo wi = heater.getWireInfo(i + 1);
        float r = wi.resistanceOhm;
        if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;
        gTot += 1.0 / r;
    }

    if (eligibleMask == 0) return false;
    if (!(gTot > 0.0)) return false;

    const float iExpected = static_cast<float>(busVoltage * gTot);
    if (!isfinite(iExpected) || iExpected <= 0.0f) return false;

    const float iLeak = computeLeakCurrent(busVoltage);
    float iWire = fabsf(currentA) - iLeak;
    if (iWire < 0.0f) iWire = 0.0f;
    const float ratio = iWire / iExpected;
    const bool ratioFail = !isfinite(ratio) || ratio < minRatio;
    bool changed = false;

    for (uint8_t i = 0; i < kWireCount; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(eligibleMask & bit)) continue;

        WireRuntimeState& ws = state.wire(i + 1);
        if (!ws.present) continue;

        if (ratioFail) {
            if (_failCount[i] < 255) _failCount[i]++;
            if (_failCount[i] >= failLimit) {
                setWirePresent_(heater, state, i + 1, false);
                changed = true;
            }
        } else {
            _failCount[i] = 0;
        }
    }

    return changed;
}

bool WirePresenceManager::hasAnyConnected(const WireStateModel& state) const {
    for (uint8_t i = 1; i <= kWireCount; ++i) {
        if (state.wire(i).present) {
            return true;
        }
    }
    return false;
}
