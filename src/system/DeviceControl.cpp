#include "system/Device.h"
#include "services/ThermalPiControllers.h"
#include "sensing/NtcSensor.h"
#include <math.h>

#ifndef CONTROL_TASK_STACK_SIZE
#define CONTROL_TASK_STACK_SIZE 4096
#endif

#ifndef CONTROL_TASK_PRIORITY
#define CONTROL_TASK_PRIORITY 3
#endif

#ifndef CONTROL_TASK_CORE
#define CONTROL_TASK_CORE 1
#endif

static constexpr uint32_t PI_CTRL_PERIOD_MS = 333; // 3 Hz

static float materialBaseC(int matCode) {
    switch (matCode) {
        case FLOOR_MAT_WOOD:     return 28.0f;
        case FLOOR_MAT_EPOXY:    return 29.0f;
        case FLOOR_MAT_CONCRETE: return 30.5f;
        case FLOOR_MAT_SLATE:    return 31.5f;
        case FLOOR_MAT_MARBLE:   return 32.5f;
        case FLOOR_MAT_GRANITE:  return 33.0f;
        default:                 return 28.0f;
    }
}

static uint8_t resolveWireIndex(uint8_t requested) {
    int idx = static_cast<int>(requested);
    if (idx <= 0 && CONF) {
        idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    }
    if (idx < 1) idx = 1;
    if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
    return static_cast<uint8_t>(idx);
}

static float resolveFloorTargetC() {
    if (!CONF) return NAN;

    int mat = CONF->GetInt(FLOOR_MATERIAL_KEY, DEFAULT_FLOOR_MATERIAL);
    if (mat < FLOOR_MAT_WOOD || mat > FLOOR_MAT_GRANITE) {
        mat = DEFAULT_FLOOR_MATERIAL;
    }
    float floorMax = CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C);
    float thickness = CONF->GetFloat(FLOOR_THICKNESS_MM_KEY, DEFAULT_FLOOR_THICKNESS_MM);

    if (!isfinite(floorMax) || floorMax <= 0.0f) return NAN;
    if (!isfinite(thickness) || thickness <= 0.0f) return NAN;

    if (floorMax > DEFAULT_FLOOR_MAX_C) floorMax = DEFAULT_FLOOR_MAX_C;

    float base = materialBaseC(mat);
    float gain = 0.0f;
    if (thickness > FLOOR_THICKNESS_MIN_MM) {
        const float span = FLOOR_THICKNESS_MAX_MM - FLOOR_THICKNESS_MIN_MM;
        const float norm = (span > 0.0f) ? ((thickness - FLOOR_THICKNESS_MIN_MM) / span) : 0.0f;
        gain = 2.5f * norm; // up to +2.5C for thicker slabs
    }
    float target = base + gain;
    if (target > floorMax) target = floorMax;
    if (target < 0.0f) target = 0.0f;
    return target;
}

void Device::startControlTask() {
    if (controlTaskHandle != nullptr) {
        return;
    }

    BaseType_t ok = xTaskCreate(
        Device::controlTaskWrapper,
        "PiCtrlTask",
        CONTROL_TASK_STACK_SIZE,
        this,
        CONTROL_TASK_PRIORITY,
        &controlTaskHandle
    );

    if (ok != pdPASS) {
        controlTaskHandle = nullptr;
        DEBUG_PRINTLN("[Control] Failed to create PiCtrlTask");
    } else {
        DEBUG_PRINTLN("[Control] PiCtrlTask started");
    }
}

void Device::controlTaskWrapper(void* param) {
    Device* self = static_cast<Device*>(param);
    if (self) {
        self->controlTask();
    }
    vTaskDelete(nullptr);
}

bool Device::startWireTargetTest(float targetC, uint8_t wireIndex) {
    if (!isfinite(targetC) || targetC <= 0.0f) return false;
    if (getState() != DeviceState::Idle) return false;
    if (!WIRE) return false;

    if (controlMtx == nullptr) {
        controlMtx = xSemaphoreCreateMutex();
    }

    uint8_t idx = resolveWireIndex(wireIndex);

    if (CONF) {
        if (!wireConfigStore.getAccessFlag(idx)) {
            return false;
        }
    }

    WireInfo wi = WIRE->getWireInfo(idx);
    if (!wi.connected) {
        return false;
    }

    float maxWireC = WIRE_T_MAX_C;
    if (CONF) {
        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                 DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(v) && v > 0.0f) maxWireC = v;
    }
    if (targetC > maxWireC) targetC = maxWireC;

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (wireTargetStatus.active || calibPwmStatus.active) {
            xSemaphoreGive(controlMtx);
            return false;
        }
        wireTargetStatus.active = true;
        wireTargetStatus.wireIndex = idx;
        wireTargetStatus.targetC = targetC;
        wireTargetStatus.duty = 0.0f;
        wireTargetStatus.onMs = 0;
        wireTargetStatus.offMs = 0;
        wireTargetStatus.updatedMs = millis();
        xSemaphoreGive(controlMtx);
    }

    if (THERMAL_PI) {
        THERMAL_PI->wire().reset(0.0f, 0.0f);
    }

    WIRE->disableAll();
    return true;
}

void Device::stopWireTargetTest() {
    uint8_t idx = 0;
    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        idx = wireTargetStatus.wireIndex;
        wireTargetStatus.active = false;
        wireTargetStatus.wireIndex = 0;
        wireTargetStatus.targetC = NAN;
        wireTargetStatus.tempC = NAN;
        wireTargetStatus.duty = 0.0f;
        wireTargetStatus.onMs = 0;
        wireTargetStatus.offMs = 0;
        wireTargetStatus.updatedMs = millis();
        xSemaphoreGive(controlMtx);
    }

    if (idx > 0 && WIRE) {
        WIRE->setOutput(idx, false);
    }
}

Device::WireTargetStatus Device::getWireTargetStatus() const {
    WireTargetStatus out{};
    if (const_cast<Device*>(this)->controlMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->controlMtx, pdMS_TO_TICKS(25)) == pdTRUE)
    {
        out = wireTargetStatus;
        xSemaphoreGive(const_cast<Device*>(this)->controlMtx);
    } else {
        out = wireTargetStatus;
    }
    return out;
}

Device::FloorControlStatus Device::getFloorControlStatus() const {
    FloorControlStatus out{};
    if (const_cast<Device*>(this)->controlMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->controlMtx, pdMS_TO_TICKS(25)) == pdTRUE)
    {
        out = floorControlStatus;
        xSemaphoreGive(const_cast<Device*>(this)->controlMtx);
    } else {
        out = floorControlStatus;
    }
    return out;
}

bool Device::startCalibrationPwm(uint8_t wireIndex, uint32_t onMs, uint32_t offMs) {
    if (getState() != DeviceState::Idle) return false;
    if (!WIRE) return false;

    if (onMs == 0 && offMs == 0) return false;
    if (onMs > 2000) onMs = 2000;
    if (offMs > 2000) offMs = 2000;

    if (controlMtx == nullptr) {
        controlMtx = xSemaphoreCreateMutex();
    }

    uint8_t idx = resolveWireIndex(wireIndex);

    if (CONF) {
        if (!wireConfigStore.getAccessFlag(idx)) {
            return false;
        }
    }

    WireInfo wi = WIRE->getWireInfo(idx);
    if (!wi.connected) {
        return false;
    }

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (wireTargetStatus.active || calibPwmStatus.active) {
            xSemaphoreGive(controlMtx);
            return false;
        }
        calibPwmStatus.active = true;
        calibPwmStatus.wireIndex = idx;
        calibPwmStatus.onMs = onMs;
        calibPwmStatus.offMs = offMs;
        xSemaphoreGive(controlMtx);
    }

    WIRE->disableAll();

    if (calibPwmTaskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            [](void* param) {
                Device* self = static_cast<Device*>(param);
                if (!self) {
                    vTaskDelete(nullptr);
                    return;
                }

                while (true) {
                    bool running = false;
                    uint8_t idx = 0;
                    uint32_t onMs = 0;
                    uint32_t offMs = 0;

                    if (self->controlMtx &&
                        xSemaphoreTake(self->controlMtx, pdMS_TO_TICKS(25)) == pdTRUE)
                    {
                        running = self->calibPwmStatus.active;
                        idx = self->calibPwmStatus.wireIndex;
                        onMs = self->calibPwmStatus.onMs;
                        offMs = self->calibPwmStatus.offMs;
                        xSemaphoreGive(self->controlMtx);
                    }

                    if (!running || idx == 0 || !WIRE) {
                        break;
                    }

                    if (self->getState() == DeviceState::Running) {
                        break;
                    }

                    if (onMs > 0) {
                        WIRE->setOutput(idx, true);
                        if (!self->delayWithPowerWatch(onMs)) {
                            WIRE->setOutput(idx, false);
                            break;
                        }
                    }

                    WIRE->setOutput(idx, false);
                    if (offMs > 0) {
                        if (!self->delayWithPowerWatch(offMs)) {
                            break;
                        }
                    }
                }

                if (WIRE) {
                    WIRE->disableAll();
                }

                if (self->controlMtx &&
                    xSemaphoreTake(self->controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    self->calibPwmStatus.active = false;
                    self->calibPwmStatus.wireIndex = 0;
                    self->calibPwmStatus.onMs = 0;
                    self->calibPwmStatus.offMs = 0;
                    xSemaphoreGive(self->controlMtx);
                }

                self->calibPwmTaskHandle = nullptr;
                vTaskDelete(nullptr);
            },
            "CalibPwm",
            4096,
            this,
            2,
            &calibPwmTaskHandle
        );

        if (ok != pdPASS) {
            calibPwmTaskHandle = nullptr;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                calibPwmStatus.active = false;
                calibPwmStatus.wireIndex = 0;
                calibPwmStatus.onMs = 0;
                calibPwmStatus.offMs = 0;
                xSemaphoreGive(controlMtx);
            }
            return false;
        }
    }

    return true;
}

void Device::stopCalibrationPwm() {
    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        calibPwmStatus.active = false;
        calibPwmStatus.wireIndex = 0;
        calibPwmStatus.onMs = 0;
        calibPwmStatus.offMs = 0;
        xSemaphoreGive(controlMtx);
    }

    if (WIRE) {
        WIRE->disableAll();
    }
}

Device::CalibPwmStatus Device::getCalibrationPwmStatus() const {
    CalibPwmStatus out{};
    if (const_cast<Device*>(this)->controlMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->controlMtx, pdMS_TO_TICKS(25)) == pdTRUE)
    {
        out = calibPwmStatus;
        xSemaphoreGive(const_cast<Device*>(this)->controlMtx);
    } else {
        out = calibPwmStatus;
    }
    return out;
}

void Device::controlTask() {
    if (controlMtx == nullptr) {
        controlMtx = xSemaphoreCreateMutex();
    }

    const TickType_t periodTicks = pdMS_TO_TICKS(PI_CTRL_PERIOD_MS);
    uint32_t lastUpdateMs = millis();

    if (THERMAL_PI) {
        THERMAL_PI->wire().setOutputLimits(0.0f, 1.0f);
        THERMAL_PI->wire().setIntegralLimits(0.0f, 1.0f);
    }

    bool floorWasActive = false;

    for (;;) {
        const uint32_t nowMs = millis();
        float dtSec = (nowMs >= lastUpdateMs)
                          ? (nowMs - lastUpdateMs) * 0.001f
                          : (PI_CTRL_PERIOD_MS * 0.001f);
        if (!isfinite(dtSec) || dtSec <= 0.0f) {
            dtSec = PI_CTRL_PERIOD_MS * 0.001f;
        } else if (dtSec > 2.0f) {
            dtSec = PI_CTRL_PERIOD_MS * 0.001f;
        }
        lastUpdateMs = nowMs;

        bool wireActive = false;
        uint8_t wireIndex = 0;
        float wireTargetC = NAN;
        bool calibActive = false;

        if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
            wireActive = wireTargetStatus.active;
            wireIndex = wireTargetStatus.wireIndex;
            wireTargetC = wireTargetStatus.targetC;
            calibActive = calibPwmStatus.active;
            xSemaphoreGive(controlMtx);
        }

        if (getState() != DeviceState::Idle) {
            if (wireActive) {
                stopWireTargetTest();
            }
            vTaskDelay(periodTicks);
            continue;
        }

        const float floorTargetC = resolveFloorTargetC();
        const bool floorActive = isfinite(floorTargetC);
        float floorTempC = NAN;
        float floorWireTargetC = NAN;

        if (floorActive && !floorWasActive && THERMAL_PI) {
            THERMAL_PI->floor().reset(0.0f, 0.0f);
        }
        floorWasActive = floorActive;

        if (floorActive && tempSensor) {
            floorTempC = tempSensor->getHeatsinkTemp();
            if (isfinite(floorTempC) && THERMAL_PI) {
                float maxWireC = WIRE_T_MAX_C;
                if (CONF) {
                    float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                             DEFAULT_NICHROME_FINAL_TEMP_C);
                    if (isfinite(v) && v > 0.0f) maxWireC = v;
                }
                THERMAL_PI->floor().setOutputLimits(0.0f, maxWireC);
                THERMAL_PI->floor().setIntegralLimits(0.0f, maxWireC);
                floorWireTargetC = THERMAL_PI->floor().update(floorTargetC - floorTempC, dtSec);
            }
        }

        if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
            floorControlStatus.active = floorActive;
            floorControlStatus.targetC = floorTargetC;
            floorControlStatus.tempC = floorTempC;
            floorControlStatus.wireTargetC = floorWireTargetC;
            floorControlStatus.updatedMs = nowMs;
            xSemaphoreGive(controlMtx);
        }

        if (!wireActive || calibActive || !WIRE) {
            vTaskDelay(periodTicks);
            continue;
        }

        wireIndex = resolveWireIndex(wireIndex);

        if (NTC) {
            NTC->update();
        }
        NtcSensor::Sample ntcSample{};
        if (NTC) {
            ntcSample = NTC->getLastSample();
        }

        float wireTempC = NAN;
        bool wireTempValid = false;
        if (ntcSample.valid && !ntcSample.pressed) {
            wireTempC = ntcSample.tempC;
            wireTempValid = isfinite(wireTempC);
        }

        float duty = 0.0f;
        uint32_t onMs = 0;
        uint32_t offMs = PI_CTRL_PERIOD_MS;

        if (wireTempValid && THERMAL_PI) {
            float maxWireC = WIRE_T_MAX_C;
            if (CONF) {
                float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                         DEFAULT_NICHROME_FINAL_TEMP_C);
                if (isfinite(v) && v > 0.0f) maxWireC = v;
            }
            if (!isfinite(wireTargetC) || wireTargetC <= 0.0f) {
                wireTargetC = maxWireC;
            }
            if (wireTargetC > maxWireC) wireTargetC = maxWireC;
            if (wireTargetC < 0.0f) wireTargetC = 0.0f;

            duty = THERMAL_PI->wire().update(wireTargetC - wireTempC, dtSec);
            if (!isfinite(duty)) duty = 0.0f;
            if (duty < 0.0f) duty = 0.0f;
            if (duty > 1.0f) duty = 1.0f;

            onMs = static_cast<uint32_t>(lroundf(duty * PI_CTRL_PERIOD_MS));
            if (onMs > PI_CTRL_PERIOD_MS) onMs = PI_CTRL_PERIOD_MS;
            offMs = PI_CTRL_PERIOD_MS - onMs;
        } else {
            duty = 0.0f;
            onMs = 0;
            offMs = PI_CTRL_PERIOD_MS;
        }

        if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
            wireTargetStatus.targetC = wireTargetC;
            wireTargetStatus.tempC = wireTempC;
            wireTargetStatus.duty = duty;
            wireTargetStatus.onMs = onMs;
            wireTargetStatus.offMs = offMs;
            wireTargetStatus.updatedMs = nowMs;
            xSemaphoreGive(controlMtx);
        }

        if (onMs > 0) {
            WIRE->setOutput(wireIndex, true);
            if (!delayWithPowerWatch(onMs)) {
                WIRE->setOutput(wireIndex, false);
                stopWireTargetTest();
                continue;
            }
        }

        WIRE->setOutput(wireIndex, false);

        if (offMs > 0) {
            if (!delayWithPowerWatch(offMs)) {
                stopWireTargetTest();
                continue;
            }
        }
    }
}
