#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

bool Device::is12VPresent() const {
  // HIGH means 12V detected; LOW/disconnected triggers shutdown
  return digitalRead(DETECT_12V_PIN) == HIGH;
}

void Device::handle12VDrop() {
  DEBUG_PRINTLN("[Device] 12V lost during RUN  Emergency stop");

  {
    float vcap = NAN;
    float curA = NAN;
    if (discharger) vcap = discharger->readCapVoltage();
    if (isfinite(vcap)) {
      int src = DEFAULT_CURRENT_SOURCE;
      if (CONF) {
        src = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
      }
      if (src == CURRENT_SRC_ACS && currentSensor) {
        const float i = currentSensor->readCurrent();
        if (isfinite(i)) {
          curA = i;
        }
      }
      if (!isfinite(curA) && WIRE) {
        curA = WIRE->estimateCurrentFromVoltage(vcap, WIRE->getOutputMask());
      }
    }
    char reason[96] = {0};
    if (isfinite(vcap) && isfinite(curA)) {
      snprintf(reason, sizeof(reason),
               "12V lost (Vcap=%.1fV I=%.2fA)",
               static_cast<double>(vcap),
               static_cast<double>(curA));
      setLastErrorReason(reason);
    } else if (isfinite(vcap)) {
      snprintf(reason, sizeof(reason), "12V lost (Vcap=%.1fV)",
               static_cast<double>(vcap));
      setLastErrorReason(reason);
    } else if (isfinite(curA)) {
      snprintf(reason, sizeof(reason), "12V lost (I=%.2fA)",
               static_cast<double>(curA));
      setLastErrorReason(reason);
    } else {
      setLastErrorReason("12V supply lost during run");
    }
  }

  // Visual + audible
  RGB->postOverlay(OverlayEvent::RELAY_OFF);
  RGB->setFault();
  RGB->showError(ErrorCategory::POWER, 3);
  BUZZ->bip();

  // Cut power paths & loads immediately
  WIRE->disableAll();
  indicator->clearAll();
  relayControl->turnOff();

  // Flip state so StartLoop() will unwind
  setState(DeviceState::Error);
}

/**
 * @brief Sleep for ms, but wake early if 12V disappears (or STOP requested).
 * @return true if full sleep elapsed, false if aborted.
 */
bool Device::delayWithPowerWatch(uint32_t ms) {
  const TickType_t start = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(10); // or whatever granularity you used

  while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < ms) {
    vTaskDelay(period);

    // 1) Check 12V presence (existing behavior)
    if (!is12VPresent()) {
      DEBUG_PRINTLN("[Device] 12V lost during wait abort");
      handle12VDrop();
      return false;
    }

    // 2) Check STOP request (existing behavior)
    if (gEvt) {
      EventBits_t bits = xEventGroupGetBits(gEvt);
      if (bits & EVT_STOP_REQ) {
        DEBUG_PRINTLN("[Device] STOP requested during wait abort");
        xEventGroupClearBits(gEvt, EVT_STOP_REQ);
        setLastStopReason("Stop requested");
        setState(DeviceState::Shutdown);
        return false;
      }
    }

    // 3) NEW: Check over-current latch
    /*if (currentSensor && currentSensor->isOverCurrentLatched()) {
      DEBUG_PRINTLN("[Device] Over-current latch set during wait ???????? abort");
      handleOverCurrentFault();
      return false;
    }*/
  }

  return true;
}

bool Device::dischargeCapBank(float thresholdV, uint8_t maxRounds) {
  if (!discharger || !relayControl || !WIRE) return false;

  relayControl->turnOff();
  vTaskDelay(pdMS_TO_TICKS(20));

  for (uint8_t round = 0; round < maxRounds; ++round) {
    float v = discharger->readCapVoltage();
    if (isfinite(v) && v <= thresholdV) break;

    for (uint8_t idx = 1; idx <= HeaterManager::kWireCount; ++idx) {
      if (!wireConfigStore.getAccessFlag(idx)) continue;
      WIRE->setOutput(idx, true);
      delayWithPowerWatch(1000);
      WIRE->setOutput(idx, false);

      v = discharger->readCapVoltage();
      if (isfinite(v) && v <= thresholdV) break;
    }
  }

  WIRE->disableAll();
  const float vfinal = discharger->readCapVoltage();
  return isfinite(vfinal) && vfinal <= thresholdV;
}

bool Device::calibrateCapacitance() {
  if (!discharger || !relayControl || !WIRE) return false;

  uint16_t dischargeMask = 0;
  double gTot = 0.0;
  for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
    if (!wireConfigStore.getAccessFlag(i)) continue;
    WireInfo wi = WIRE->getWireInfo(i);
    if (!wi.connected) continue;
    if (!isfinite(wi.resistanceOhm) || wi.resistanceOhm <= 0.01f) {
      continue;
    }
    gTot += 1.0 / static_cast<double>(wi.resistanceOhm);
    dischargeMask |= static_cast<uint16_t>(1u << (i - 1));
  }
  if (!(gTot > 0.0) || dischargeMask == 0) {
    DEBUG_PRINTLN("[Device] Cap calibration skipped (no connected discharge wire)");
    return false;
  }

  const double rLoad = 1.0 / gTot;
  const bool relayWasOn = relayControl->isOn();

  auto restore = [&]() {
    if (WIRE) WIRE->disableAll();
    if (relayControl) {
      if (relayWasOn) relayControl->turnOn();
      else relayControl->turnOff();
    }
  };

  if (WIRE) WIRE->disableAll();
  relayControl->turnOff();
  if (!delayWithPowerWatch(20)) {
    restore();
    return false;
  }

  const float v0 = discharger->sampleVoltageNow();
  if (!isfinite(v0) || v0 <= 0.0f) {
    restore();
    return false;
  }

  double capGuess = capBankCapF;
  if (!isfinite(capGuess) || capGuess <= 0.0) {
    capGuess = DEFAULT_CAP_BANK_CAP_F;
  }
  double dtS = 0.2;
  const double tau = rLoad * capGuess;
  if (isfinite(tau) && tau > 0.0) {
    dtS = tau * 0.35;
  }
  if (dtS < 0.05) dtS = 0.05;
  if (dtS > 0.6) dtS = 0.6;
  uint32_t dischargeMs = static_cast<uint32_t>(dtS * 1000.0);
  if (dischargeMs < 20) dischargeMs = 20;

  for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
    if (dischargeMask & (1u << (i - 1))) {
      WIRE->setOutput(i, true);
    }
  }

  if (!delayWithPowerWatch(dischargeMs)) {
    restore();
    return false;
  }

  const float v1 = discharger->sampleVoltageNow();
  if (WIRE) WIRE->disableAll();

  if (!isfinite(v1) || v1 <= 0.0f || v1 >= v0) {
    restore();
    return false;
  }

  const double ratio = static_cast<double>(v1) / static_cast<double>(v0);
  if (!isfinite(ratio) || ratio <= 0.05 || ratio >= 0.98) {
    restore();
    return false;
  }

  const double lnRatio = log(ratio);
  if (!isfinite(lnRatio) || lnRatio >= 0.0) {
    restore();
    return false;
  }

  const double capF = -dtS / (rLoad * lnRatio);
  if (!isfinite(capF) || capF <= 0.0) {
    restore();
    return false;
  }

  capBankCapF = static_cast<float>(capF);
  if (CONF) {
    CONF->PutFloat(CAP_BANK_CAP_F_KEY, capBankCapF);
  }

  DEBUG_PRINTF("[Device] Capacitance calibrated: V0=%.2fV V1=%.2fV dt=%.3fs R=%.2f ohm C=%.6fF\n",
               (double)v0,
               (double)v1,
               (double)dtS,
               (double)rLoad,
               (double)capBankCapF);

  restore();
  return true;
}

bool Device::runCalibrationsStandalone(uint32_t timeoutMs) {
  if (getState() == DeviceState::Running) {
    DEBUG_PRINTLN("[Device] Calibration skipped (already running)");
    return false;
  }

  if (!relayControl || !discharger) {
    DEBUG_PRINTLN("[Device] Calibration skipped (missing relay/discharger)");
    return false;
  }

  const DeviceState startState = getState();
  const TickType_t start = xTaskGetTickCount();

  auto timedOut = [&]() -> bool {
    return (xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeoutMs;
  };

  auto failSafe = [&](const char* msg) -> bool {
    if (msg) DEBUG_PRINTLN(msg);
    if (WIRE) WIRE->disableAll();
    if (indicator) indicator->clearAll();
    relayControl->turnOff();
    setLastStopReason(msg ? msg : "Calibration aborted");
    if (getState() != DeviceState::Error) {
      setState(DeviceState::Shutdown);
    }
    if (startState == DeviceState::Idle && gEvt) {
      xEventGroupSetBits(gEvt, EVT_STOP_REQ);
    }
    return false;
  };

  DEBUG_PRINTLN("[Device] Manual calibration sequence starting");

  if (WIRE) WIRE->disableAll();
  if (indicator) indicator->clearAll();

  // Pre-discharge to a safe baseline before calibrations.
  dischargeCapBank(5.0f, 3);

  // 1) Charge caps to threshold
  relayControl->turnOn();

  TickType_t lastChargePost = 0;
  while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
    if (timedOut()) return failSafe("[Device] Calibration timeout (charging caps)");

    TickType_t now = xTaskGetTickCount();
    if ((now - lastChargePost) * portTICK_PERIOD_MS >= 1000) {
      if (RGB) RGB->postOverlay(OverlayEvent::PWR_CHARGING);
      lastChargePost = now;
    }

    if (!delayWithPowerWatch(200)) {
      return failSafe("[Device] Calibration aborted (power/watch stop)");
    }
  }

  // 2) Capacitance calibration (relay cycled inside)
  if (!calibrateCapacitance()) {
    return failSafe("[Device] Capacitance calibration failed");
  }

  if (CONF) {
    CONF->PutBool(CALIB_CAP_DONE_KEY, true);
  }

  if (currentSensor) {
    if (WIRE) WIRE->disableAll();
    if (relayControl) relayControl->turnOff();
    if (!delayWithPowerWatch(50)) {
      return failSafe("[Device] Calibration aborted (power/watch stop)");
    }
    currentSensor->calibrateZeroCurrent();
    if (timedOut()) return failSafe("[Device] Calibration timeout (current sensor)");
    if (relayControl) relayControl->turnOn();
  }

  if (timedOut()) return failSafe("[Device] Calibration timeout (capacitance)");

  // 3) Recharge after discharge
  TickType_t lastRechargePost = 0;
  while (discharger->readCapVoltage() < GO_THRESHOLD_RATIO) {
    if (timedOut()) return failSafe("[Device] Calibration timeout (recharge)");

    TickType_t now = xTaskGetTickCount();
    if ((now - lastRechargePost) * portTICK_PERIOD_MS >= 1000) {
      if (RGB) RGB->postOverlay(OverlayEvent::PWR_CHARGING);
      lastRechargePost = now;
    }

    if (!delayWithPowerWatch(200)) {
      return failSafe("[Device] Calibration aborted (power/watch stop)");
    }
  }

  DEBUG_PRINTLN("[Device] Manual calibration sequence completed");

  if (WIRE) WIRE->disableAll();
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
  if (startState == DeviceState::Idle && gEvt) {
    xEventGroupSetBits(gEvt, EVT_STOP_REQ);
  }

  return true;
}

void Device::handleOverCurrentFault() {
  DEBUG_PRINTLN("[Device] Over-current detected EMERGENCY SHUTDOWN");
  {
    float curA = NAN;
    float limitA = DEFAULT_CURR_LIMIT_A;
    if (discharger) {
      const float vcap = discharger->readCapVoltage();
      if (isfinite(vcap)) {
        int src = DEFAULT_CURRENT_SOURCE;
        if (CONF) {
          src = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
        }
        if (src == CURRENT_SRC_ACS && currentSensor) {
          const float i = currentSensor->readCurrent();
          if (isfinite(i)) {
            curA = i;
          }
        }
        if (!isfinite(curA) && WIRE) {
          curA = WIRE->estimateCurrentFromVoltage(vcap, WIRE->getOutputMask());
        }
      }
    }
    if (CONF) limitA = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
    if (!isfinite(limitA) || limitA <= 0.0f) limitA = DEFAULT_CURR_LIMIT_A;

    char reason[96] = {0};
    if (isfinite(curA)) {
      snprintf(reason, sizeof(reason), "Over-current trip (I=%.2fA lim=%.1fA)",
               static_cast<double>(curA), static_cast<double>(limitA));
      setLastErrorReason(reason);
    } else {
      setLastErrorReason("Over-current trip");
    }
  }

  // 1) Latch global state to FAULT
  setState(DeviceState::Error);

  // 2) Immediately disable all loads and power paths
  if (WIRE)         WIRE->disableAll();
  if (indicator)    indicator->clearAll();
  if (relayControl) relayControl->turnOff();

  // 3) Feedback: critical current trip
  if (RGB) {
    RGB->setDeviceState(DevState::FAULT);      // red strobe background
    RGB->postOverlay(OverlayEvent::CURR_TRIP); // short critical burst
    RGB->showError(ErrorCategory::POWER, 1);
  }

  if (BUZZ) {
    BUZZ->bipFault(); // reuse your existing FAULT pattern
  }
}
