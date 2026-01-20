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
constexpr uint32_t kProbeBaselineMs = 20;
constexpr uint32_t kProbeSampleMs = 200;
constexpr uint32_t kProbeSampleDelayMs = 10;
constexpr float kMinDropFloor = 5.0f;
constexpr float kMinDropCeil = 100.0f;

float resolvePresenceMinDropV() {
    float v = DEFAULT_PRESENCE_MIN_DROP_V;
    if (CONF) {
        v = CONF->GetFloat(PRESENCE_MIN_DROP_V_KEY, DEFAULT_PRESENCE_MIN_DROP_V);
    }
    if (!isfinite(v) || v <= 0.0f) {
        v = DEFAULT_PRESENCE_MIN_DROP_V;
    }
    if (v < kMinDropFloor) v = kMinDropFloor;
    if (v > kMinDropCeil) v = kMinDropCeil;
    return v;
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
                                   CpDischg* discharger) {
    if (!discharger) {
        return false;
    }

    resetFailures();

    const uint16_t prevMask = heater.getOutputMask();
    heater.disableAll();

    const float minDropV = resolvePresenceMinDropV();
    const uint32_t settleMs = kProbeSettleMs;

    for (uint8_t i = 1; i <= kWireCount; ++i) {
        if (!cfg.getAccessFlag(i)) {
            setWirePresent_(heater, state, i, false);
            continue;
        }

        const float v0 = sampleVoltageAverage(discharger, kProbeBaselineMs);

        heater.setOutput(i, true);
        if (settleMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(settleMs));
        }

        const float v1 = sampleVoltageAverage(discharger, kProbeSampleMs);

        heater.setOutput(i, false);

        if (!isfinite(v0) || !isfinite(v1)) {
            setWirePresent_(heater, state, i, false);
            continue;
        }

        float drop = v0 - v1;
        if (drop < 0.0f) drop = 0.0f;
        const bool present = isfinite(drop) && (drop >= minDropV);
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
                                                 float busVoltageStart,
                                                 float busVoltage) {
    if (mask == 0) return false;
    if (!isfinite(busVoltageStart) || busVoltageStart <= 0.0f) return false;
    if (!isfinite(busVoltage) || busVoltage <= 0.0f) return false;

    const float minDropV = resolvePresenceMinDropV();
    const uint8_t failLimit = resolvePresenceFailCount();
    uint16_t eligibleMask = 0;

    for (uint8_t i = 0; i < kWireCount; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(mask & bit)) continue;

        const WireRuntimeState& ws = state.wire(i + 1);
        if (!ws.present) continue;
        if (ws.overTemp) continue;

        eligibleMask |= bit;
    }

    if (eligibleMask == 0) return false;

    float drop = busVoltageStart - busVoltage;
    if (drop < 0.0f) drop = 0.0f;
    if (!isfinite(drop)) return false;

    const bool dropFail = drop < minDropV;
    bool changed = false;

    for (uint8_t i = 0; i < kWireCount; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(eligibleMask & bit)) continue;

        WireRuntimeState& ws = state.wire(i + 1);
        if (!ws.present) continue;

        if (dropFail) {
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
