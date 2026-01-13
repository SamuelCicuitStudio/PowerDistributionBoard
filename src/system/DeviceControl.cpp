#include <Device.hpp>
#include <NtcSensor.hpp>
#include <math.h>
#include <string.h>

#ifndef CONTROL_TASK_STACK_SIZE
#define CONTROL_TASK_STACK_SIZE 4096
#endif

#ifndef CONTROL_TASK_PRIORITY
#define CONTROL_TASK_PRIORITY 3
#endif

#ifndef CONTROL_TASK_CORE
#define CONTROL_TASK_CORE 1
#endif

static constexpr uint32_t CONTROL_TASK_PERIOD_MS = 333; // status update cadence

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

static uint8_t resolveNtcGateIndex() {
    int idx = DEFAULT_NTC_GATE_INDEX;
    if (CONF) {
        idx = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
    }
    if (idx < 1) idx = 1;
    if (idx > HeaterManager::kWireCount) idx = HeaterManager::kWireCount;
    return static_cast<uint8_t>(idx);
}

static uint8_t resolveWireIndex(uint8_t requested) {
    int idx = static_cast<int>(requested);
    if (idx <= 0) {
        idx = resolveNtcGateIndex();
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
        "CtrlTask",
        CONTROL_TASK_STACK_SIZE,
        this,
        CONTROL_TASK_PRIORITY,
        &controlTaskHandle
    );

    if (ok != pdPASS) {
        controlTaskHandle = nullptr;
        DEBUG_PRINTLN("[Control] Failed to create CtrlTask");
    } else {
        DEBUG_PRINTLN("[Control] CtrlTask started");
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

    uint8_t idx = resolveNtcGateIndex();
    (void)wireIndex;

    if (idx < 1 || idx > HeaterManager::kWireCount) {
        return false;
    }
    allowedOverrideMask = static_cast<uint16_t>(1u << (idx - 1));

    float maxWireC = WIRE_T_MAX_C;
    if (CONF) {
        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                 DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(v) && v > 0.0f) maxWireC = v;
    }
    if (targetC > maxWireC) targetC = maxWireC;

    checkAllowedOutputs();
    if (!allowedOutputs[idx - 1]) {
        allowedOverrideMask = 0;
        return false;
    }
    WireInfo wi = WIRE->getWireInfo(idx);
    if (!wi.connected) {
        allowedOverrideMask = 0;
        return false;
    }

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (wireTargetStatus.active) {
            xSemaphoreGive(controlMtx);
            allowedOverrideMask = 0;
            return false;
        }
        wireTargetStatus.active = true;
        wireTargetStatus.purpose = EnergyRunPurpose::WireTest;
        wireTargetStatus.targetC = targetC;
        wireTargetStatus.ntcTempC = NAN;
        wireTargetStatus.activeTempC = NAN;
        wireTargetStatus.activeWire = idx;
        wireTargetStatus.packetMs = 0;
        wireTargetStatus.frameMs = 0;
        wireTargetStatus.updatedMs = millis();
        xSemaphoreGive(controlMtx);
    }

    if (wireTestTaskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            [](void* param) {
                Device* self = static_cast<Device*>(param);
                if (!self) {
                    vTaskDelete(nullptr);
                    return;
                }

                // Ensure thermal integration keeps up while testing.
                if (self->thermalTaskHandle == nullptr) {
                    self->startThermalTask();
                }
                self->startTemperatureMonitor();

                self->loadRuntimeSettings();
                self->setState(DeviceState::Running);

                self->StartLoop();

                self->stopTemperatureMonitor();
                if (WIRE) {
                    WIRE->disableAll();
                }
                if (self->indicator) {
                    self->indicator->clearAll();
                }
                if (self->relayControl) {
                    self->relayControl->turnOff();
                    if (RGB) {
                        RGB->postOverlay(OverlayEvent::RELAY_OFF);
                        RGB->setOff();
                    }
                }
                if (self->getState() != DeviceState::Error) {
                    self->setState(DeviceState::Shutdown);
                }

                if (self->controlMtx &&
                    xSemaphoreTake(self->controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    self->wireTargetStatus.active = false;
                    self->wireTargetStatus.purpose = EnergyRunPurpose::None;
                    self->wireTargetStatus.activeWire = 0;
                    self->wireTargetStatus.packetMs = 0;
                    self->wireTargetStatus.frameMs = 0;
                    self->wireTargetStatus.updatedMs = millis();
                    xSemaphoreGive(self->controlMtx);
                }
                self->allowedOverrideMask = 0;

                self->wireTestTaskHandle = nullptr;
                vTaskDelete(nullptr);
            },
            "WireTest",
            6144,
            this,
            2,
            &wireTestTaskHandle
        );

        if (ok != pdPASS) {
            wireTestTaskHandle = nullptr;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                wireTargetStatus.active = false;
                wireTargetStatus.purpose = EnergyRunPurpose::None;
                wireTargetStatus.activeWire = 0;
                wireTargetStatus.packetMs = 0;
                wireTargetStatus.frameMs = 0;
                wireTargetStatus.updatedMs = millis();
                xSemaphoreGive(controlMtx);
            }
            allowedOverrideMask = 0;
            return false;
        }
    }

    WIRE->disableAll();
    return true;
}


bool Device::startEnergyCalibration(float targetC, uint8_t wireIndex, EnergyRunPurpose purpose) {
    if (purpose != EnergyRunPurpose::ModelCal &&
        purpose != EnergyRunPurpose::NtcCal) {
        return false;
    }
    if (!isfinite(targetC) || targetC <= 0.0f) return false;
    if (getState() != DeviceState::Idle) return false;
    if (!WIRE) return false;

    if (controlMtx == nullptr) {
        controlMtx = xSemaphoreCreateMutex();
    }

    uint8_t idx = resolveWireIndex(wireIndex);
    if (purpose == EnergyRunPurpose::ModelCal ||
        purpose == EnergyRunPurpose::NtcCal) {
        idx = resolveNtcGateIndex();
    }
    if (idx < 1 || idx > HeaterManager::kWireCount) {
        return false;
    }
    allowedOverrideMask = static_cast<uint16_t>(1u << (idx - 1));

    float maxWireC = WIRE_T_MAX_C;
    if (CONF) {
        float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                 DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(v) && v > 0.0f) maxWireC = v;
    }
    if (targetC > maxWireC) targetC = maxWireC;

    checkAllowedOutputs();
    if (!allowedOutputs[idx - 1]) {
        allowedOverrideMask = 0;
        return false;
    }
    WireInfo wi = WIRE->getWireInfo(idx);
    if (!wi.connected) {
        allowedOverrideMask = 0;
        return false;
    }

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (wireTargetStatus.active) {
            xSemaphoreGive(controlMtx);
            allowedOverrideMask = 0;
            return false;
        }
        wireTargetStatus.active = true;
        wireTargetStatus.purpose = purpose;
        wireTargetStatus.targetC = targetC;
        wireTargetStatus.ntcTempC = NAN;
        wireTargetStatus.activeTempC = NAN;
        wireTargetStatus.activeWire = idx;
        wireTargetStatus.packetMs = 0;
        wireTargetStatus.frameMs = 0;
        wireTargetStatus.updatedMs = millis();
        xSemaphoreGive(controlMtx);
    }

    if (wireTestTaskHandle == nullptr) {
        BaseType_t ok = xTaskCreate(
            [](void* param) {
                Device* self = static_cast<Device*>(param);
                if (!self) {
                    vTaskDelete(nullptr);
                    return;
                }

                // Ensure thermal integration keeps up while testing.
                if (self->thermalTaskHandle == nullptr) {
                    self->startThermalTask();
                }
                self->startTemperatureMonitor();

                self->loadRuntimeSettings();
                self->setState(DeviceState::Running);

                self->StartLoop();

                self->stopTemperatureMonitor();
                if (WIRE) {
                    WIRE->disableAll();
                }
                if (self->indicator) {
                    self->indicator->clearAll();
                }
                if (self->relayControl) {
                    self->relayControl->turnOff();
                    if (RGB) {
                        RGB->postOverlay(OverlayEvent::RELAY_OFF);
                        RGB->setOff();
                    }
                }
                if (self->getState() != DeviceState::Error) {
                    self->setState(DeviceState::Shutdown);
                }

                if (self->controlMtx &&
                    xSemaphoreTake(self->controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    self->wireTargetStatus.active = false;
                    self->wireTargetStatus.purpose = EnergyRunPurpose::None;
                    self->wireTargetStatus.activeWire = 0;
                    self->wireTargetStatus.packetMs = 0;
                    self->wireTargetStatus.frameMs = 0;
                    self->wireTargetStatus.updatedMs = millis();
                    xSemaphoreGive(self->controlMtx);
                }

                self->allowedOverrideMask = 0;
                self->wireTestTaskHandle = nullptr;
                vTaskDelete(nullptr);
            },
            "WireTest",
            6144,
            this,
            2,
            &wireTestTaskHandle
        );

        if (ok != pdPASS) {
            wireTestTaskHandle = nullptr;
            if (controlMtx &&
                xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                wireTargetStatus.active = false;
                wireTargetStatus.purpose = EnergyRunPurpose::None;
                wireTargetStatus.activeWire = 0;
                wireTargetStatus.packetMs = 0;
                wireTargetStatus.frameMs = 0;
                wireTargetStatus.updatedMs = millis();
                xSemaphoreGive(controlMtx);
            }
            allowedOverrideMask = 0;
            return false;
        }
    }

    WIRE->disableAll();
    return true;
}


void Device::stopWireTargetTest() {
    bool wasActive = false;
    EnergyRunPurpose purpose = EnergyRunPurpose::None;
    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        wasActive = wireTargetStatus.active;
        purpose = wireTargetStatus.purpose;
        wireTargetStatus.active = false;
        wireTargetStatus.purpose = EnergyRunPurpose::None;
        wireTargetStatus.targetC = NAN;
        wireTargetStatus.ntcTempC = NAN;
        wireTargetStatus.activeTempC = NAN;
        wireTargetStatus.activeWire = 0;
        wireTargetStatus.packetMs = 0;
        wireTargetStatus.frameMs = 0;
        wireTargetStatus.updatedMs = millis();
        xSemaphoreGive(controlMtx);
    }
    allowedOverrideMask = 0;

    if (WIRE) {
        WIRE->disableAll();
    }

    if (!wasActive) {
        return;
    }

    if (wasActive &&
        (purpose == EnergyRunPurpose::WireTest ||
         purpose == EnergyRunPurpose::ModelCal ||
         purpose == EnergyRunPurpose::NtcCal)) {
        if (getState() == DeviceState::Running) {
            setLastStopReason("Targeted run stopped");
            if (indicator) indicator->clearAll();
            if (relayControl) {
                relayControl->turnOff();
                if (RGB) {
                    RGB->postOverlay(OverlayEvent::RELAY_OFF);
                    RGB->setOff();
                }
            }
            if (getState() != DeviceState::Error) {
                setState(DeviceState::Shutdown);
            }
        }
        if (gEvt) {
            xEventGroupSetBits(gEvt, EVT_STOP_REQ);
        }
        return;
    }

    if (gEvt) {
        xEventGroupSetBits(gEvt, EVT_STOP_REQ);
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

Device::AmbientWaitStatus Device::getAmbientWaitStatus() const {
    AmbientWaitStatus out{};
    if (const_cast<Device*>(this)->controlMtx &&
        xSemaphoreTake(const_cast<Device*>(this)->controlMtx, pdMS_TO_TICKS(25)) == pdTRUE)
    {
        out = ambientWaitStatus;
        xSemaphoreGive(const_cast<Device*>(this)->controlMtx);
    } else {
        out = ambientWaitStatus;
    }
    return out;
}

void Device::setAmbientWaitStatus(bool active, float tolC, const char* reason) {
    AmbientWaitStatus next{};
    next.active = active;
    next.tolC = tolC;
    next.sinceMs = active ? millis() : 0;
    if (reason && reason[0]) {
        strncpy(next.reason, reason, sizeof(next.reason) - 1);
        next.reason[sizeof(next.reason) - 1] = '\0';
    } else {
        next.reason[0] = '\0';
    }

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
        ambientWaitStatus = next;
        xSemaphoreGive(controlMtx);
    } else {
        ambientWaitStatus = next;
    }
}

void Device::updateWireTestStatus(uint8_t wireIndex,
                                  uint32_t packetMs,
                                  uint32_t frameMs) {
    const float ntcTemp = (NTC) ? NTC->getLastTempC() : NAN;
    float activeTemp = NAN;
    if (wireIndex > 0 && WIRE) {
        activeTemp = WIRE->getWireEstimatedTemp(wireIndex);
    }
    const uint32_t nowMs = millis();

    if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
        if (wireTargetStatus.active) {
            wireTargetStatus.ntcTempC = ntcTemp;
            wireTargetStatus.activeTempC = activeTemp;
            wireTargetStatus.activeWire = wireIndex;
            wireTargetStatus.packetMs = packetMs;
            wireTargetStatus.frameMs = frameMs;
            wireTargetStatus.updatedMs = nowMs;
        }
        xSemaphoreGive(controlMtx);
    }
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

void Device::controlTask() {
    if (controlMtx == nullptr) {
        controlMtx = xSemaphoreCreateMutex();
    }

    const TickType_t periodTicks = pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS);

    for (;;) {
        if (getState() != DeviceState::Idle) {
            vTaskDelay(periodTicks);
            continue;
        }

        const uint32_t nowMs = millis();
        const float floorTargetC = resolveFloorTargetC();
        const bool floorActive = isfinite(floorTargetC);
        float floorTempC = NAN;
        float floorWireTargetC = NAN;

        if (floorActive && tempSensor) {
            floorTempC = tempSensor->getHeatsinkTemp();
        }

        if (floorActive) {
            float maxWireC = WIRE_T_MAX_C;
            if (CONF) {
                float v = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                         DEFAULT_NICHROME_FINAL_TEMP_C);
                if (isfinite(v) && v > 0.0f) maxWireC = v;
            }
            floorWireTargetC = floorTargetC;
            if (floorWireTargetC > maxWireC) floorWireTargetC = maxWireC;
            if (floorWireTargetC < 0.0f) floorWireTargetC = 0.0f;
        }

        if (controlMtx && xSemaphoreTake(controlMtx, pdMS_TO_TICKS(25)) == pdTRUE) {
            floorControlStatus.active = floorActive;
            floorControlStatus.targetC = floorTargetC;
            floorControlStatus.tempC = floorTempC;
            floorControlStatus.wireTargetC = floorWireTargetC;
            floorControlStatus.updatedMs = nowMs;
            xSemaphoreGive(controlMtx);
        }

        if (!WIRE) {
            vTaskDelay(periodTicks);
            continue;
        }
    }
}
