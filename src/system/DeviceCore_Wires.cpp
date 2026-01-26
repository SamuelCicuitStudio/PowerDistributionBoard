#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

// Map of output keys (0-indexed for outputs 1 to 10)
const char* outputKeys[10] = {
  OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
  OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
};

void Device::syncWireRuntimeFromHeater() {
  const uint32_t nowMs = millis();

  for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
    WireRuntimeState& ws = wireStateModel.wire(i);
    ws.allowedByAccess = wireConfigStore.getAccessFlag(i);

    if (WIRE) {
      WireInfo wi = WIRE->getWireInfo(i);
      ws.tempC = wi.temperatureC;
      ws.present = wi.connected;
      ws.lastUpdateMs = nowMs;
    }

    ws.overTemp = isfinite(ws.tempC) && ws.tempC >= WIRE_T_MAX_C;
  }

  if (WIRE) {
    wireStateModel.setLastMask(WIRE->getOutputMask());
  }
}

void Device::checkAllowedOutputs() {
  DEBUG_PRINTLN("[Device] Checking allowed outputs from preferences");

  syncWireRuntimeFromHeater();
  const uint16_t overrideMask = allowedOverrideMask;

  for (uint8_t i = 0; i < 10; ++i) {
    WireRuntimeState& ws = wireStateModel.wire(i + 1);

    const bool overrideActive = (overrideMask != 0);
    const bool overrideAllowed =
      overrideActive && ((overrideMask & (1u << i)) != 0);
    if (overrideActive) {
      ws.allowedByAccess = overrideAllowed;
    } else {
      ws.allowedByAccess = wireConfigStore.getAccessFlag(i + 1);
    }
    const bool cfgAllowed = ws.allowedByAccess;

    const bool thermLocked = ws.locked ||
                             ws.overTemp ||
                             (isfinite(ws.tempC) && ws.tempC >= WIRE_T_MAX_C);

    const bool presentOk = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0) ? true : ws.present;

    allowedOutputs[i] = cfgAllowed && presentOk && !thermLocked;

    /*DEBUG_PRINTF(
      "[Device] OUT%02u => %s (cfg=%s, thermal=%s)\n",
      i + 1,
      allowedOutputs[i] ? "ENABLED" : "DISABLED",
      cfgAllowed  ? "ON" : "OFF",
      thermLocked ? "LOCKED" : "OK"
    );*/
  }

  if (overrideMask != 0) {
    for (uint8_t i = 0; i < 10; ++i) {
      if (allowedOutputs[i] && !(overrideMask & (1u << i))) {
        allowedOutputs[i] = false;
      }
    }
  }
}

bool Device::probeWirePresence() {
  if (!WIRE || !discharger || !currentSensor) return false;
  if (getState() != DeviceState::Idle) return false;

  if (indicator) indicator->clearAll();
  WIRE->disableAll();
  if (relayControl) {
    relayControl->turnOn();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
      vTaskDelay(pdMS_TO_TICKS(300));
    } else {
      delay(300);
    }
  }

  const bool ok = wirePresenceManager.probeAll(*WIRE,
                                               wireStateModel,
                                               wireConfigStore,
                                               discharger,
                                               currentSensor);
  checkAllowedOutputs();
  return ok;
}

void Device::loadRuntimeSettings() {
  if (CONF) {
    capBankCapF = CONF->GetFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
  }

  if (!isfinite(capBankCapF) || capBankCapF < 0.0f) {
    capBankCapF = DEFAULT_CAP_BANK_CAP_F;
  }

  applyWireModelParamsFromNvs();
}

void Device::applyWireModelParamsFromNvs() {
  if (!CONF) return;

  static const char* kTauKeys[HeaterManager::kWireCount] = {
    W1TAU_KEY, W2TAU_KEY, W3TAU_KEY, W4TAU_KEY, W5TAU_KEY,
    W6TAU_KEY, W7TAU_KEY, W8TAU_KEY, W9TAU_KEY, W10TAU_KEY
  };
  static const char* kKKeys[HeaterManager::kWireCount] = {
    W1KLS_KEY, W2KLS_KEY, W3KLS_KEY, W4KLS_KEY, W5KLS_KEY,
    W6KLS_KEY, W7KLS_KEY, W8KLS_KEY, W9KLS_KEY, W10KLS_KEY
  };
  static const char* kCKeys[HeaterManager::kWireCount] = {
    W1CAP_KEY, W2CAP_KEY, W3CAP_KEY, W4CAP_KEY, W5CAP_KEY,
    W6CAP_KEY, W7CAP_KEY, W8CAP_KEY, W9CAP_KEY, W10CAP_KEY
  };

  for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
    double tau = CONF->GetDouble(kTauKeys[i], DEFAULT_WIRE_MODEL_TAU);
    double k   = CONF->GetDouble(kKKeys[i], DEFAULT_WIRE_MODEL_K);
    double c   = CONF->GetDouble(kCKeys[i], DEFAULT_WIRE_MODEL_C);
    wireThermalModel.setWireThermalParams(i + 1, tau, k, c);
  }
}
