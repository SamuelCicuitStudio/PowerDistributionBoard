#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

void Device::startTemperatureMonitor() {
  if (tempMonitorTaskHandle == nullptr) {
    xTaskCreate(
      Device::monitorTemperatureTask,
      "TempMonitorTask",
      TEMP_MONITOR_TASK_STACK_SIZE,
      this,
      TEMP_MONITOR_TASK_PRIORITY,
      &tempMonitorTaskHandle
    );
    DEBUG_PRINTLN("[Device] Temperature monitor started ");
  }
}

void Device::monitorTemperatureTask(void* param) {
  Device* self = static_cast<Device*>(param);
  const uint8_t sensorCount = self->tempSensor->getSensorCount();

  if (sensorCount == 0) {
    DEBUG_PRINTLN("[Device] No temperature sensors found! Skipping monitoring");
    vTaskDelete(nullptr);
    return;
  }

  self->tempSensor->startTemperatureTask(2500);
  DEBUG_PRINTF("[Device] Monitoring %u temperature sensors every 2s\n", sensorCount);

  while (true) {
    float tripC = DEFAULT_TEMP_THRESHOLD;
    float warnC = DEFAULT_TEMP_WARN_C;
    if (CONF) {
      tripC = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
      warnC = CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C);
    }
    if (!isfinite(tripC) || tripC <= 0.0f) tripC = DEFAULT_TEMP_THRESHOLD;
    if (!isfinite(warnC) || warnC < 0.0f) warnC = 0.0f;
    if (warnC > 0.0f && warnC >= tripC) warnC = tripC - 1.0f;

    bool anyWarn = false;
    float warnMax = -INFINITY;
    int warnIdx = -1;

    for (uint8_t i = 0; i < sensorCount; ++i) {
      const float temp = self->tempSensor->getTemperature(i);
      // DEBUG_PRINTF("[Device] TempSensor[%u] = %.2f??C\n", i, temp);

      if (warnC > 0.0f && temp >= warnC) {
        anyWarn = true;
        if (temp > warnMax) {
          warnMax = temp;
          warnIdx = i;
        }
      }

      if (temp >= tripC) {
        DEBUG_PRINTF("[Device] Overtemperature Detected! Sensor[%u] = %.2f??C\n", i, temp);
        BUZZ->bipOverTemperature();

        // Visual: critical temperature overlay + fault background
        RGB->postOverlay(OverlayEvent::TEMP_CRIT);
        RGB->setFault();
        RGB->showError(ErrorCategory::THERMAL, 1);

        char reason[96] = {0};
        snprintf(reason, sizeof(reason),
                 "Overtemp trip sensor[%u]=%.1fC (trip %.1fC)",
                 static_cast<unsigned>(i),
                 static_cast<double>(temp),
                 static_cast<double>(tripC));
        self->setLastErrorReason(reason);

        self->setState(DeviceState::Error);
        WIRE->disableAll();
        self->indicator->clearAll();

        vTaskDelete(nullptr);
      }
    }

    if (anyWarn && self->getState() != DeviceState::Error) {
      RGB->postOverlay(OverlayEvent::TEMP_WARN);
      if (!self->tempWarnLatched && warnIdx >= 0 && isfinite(warnMax)) {
        char warnReason[96] = {0};
        snprintf(warnReason, sizeof(warnReason),
                 "Temp warning sensor[%u]=%.1fC (warn %.1fC)",
                 static_cast<unsigned>(warnIdx),
                 static_cast<double>(warnMax),
                 static_cast<double>(warnC));
        self->addWarningReason(warnReason);
        self->tempWarnLatched = true;
      }
    } else if (!anyWarn) {
      self->tempWarnLatched = false;
    }

    vTaskDelay(pdMS_TO_TICKS(TEMP_MONITOR_TASK_DELAY_MS));
  }
}

void Device::stopTemperatureMonitor() {
  if (tempSensor) {
    tempSensor->stopTemperatureTask();
  }
  if (tempMonitorTaskHandle != nullptr) {
    DEBUG_PRINTLN("[Device] Stopping Temperature Monitor Task ");
    vTaskDelete(tempMonitorTaskHandle);
    tempMonitorTaskHandle = nullptr;
  }
}

void Device::LedUpdateTask(void* param) {
  Device* device = static_cast<Device*>(param);
  const TickType_t delayTicks = pdMS_TO_TICKS(LED_UPDATE_TASK_DELAY_MS);

  while (true) {
    if (CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK)) {
      for (uint8_t i = 1; i <= 10; i++) {
        const bool state = WIRE->getOutputState(i);
        device->indicator->setLED(i, state);
      }
    }
    vTaskDelay(delayTicks);
  }
}

void Device::updateLed() {
  if (CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK)) {
    for (uint8_t i = 1; i <= 10; i++) {
      const bool state = WIRE->getOutputState(i);
      indicator->setLED(i, state);
    }
  }
}

// === Power-loss helpers =====================================================
// ------------------- Fan control helpers -------------------

static inline uint8_t _mapTempToPct(float T, float Ton, float Tfull, float Toff, uint8_t lastPct) {
  if (!isfinite(T)) {
    // Sensor missing? Keep previous command; don't slam fans.
    return lastPct;
  }

  // Hysteresis: below Toff -> demand 0; between Toff and Ton -> hold last
  if (T <= Toff) return 0;
  if (T < Ton)   return lastPct;

  if (T >= Tfull) return 100;

  // Linear ramp Ton..Tfull -> 0..100
  float pct = ((T - Ton) / (Tfull - Ton)) * 100.0f;
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  // Guarantee a minimum spin when non-zero
  if (pct > 0.0f && pct < FAN_MIN_RUN_PCT) pct = FAN_MIN_RUN_PCT;
  return (uint8_t)(pct + 0.5f);
}

// ------------------- Fan control task RTOS API -------------------

void Device::startFanControlTask() {
  if (fanTaskHandle) return;

  BaseType_t ok = xTaskCreate(
    Device::fanControlTask,
    "FanCtrlTask",
    3072,
    this,
    2,
    &fanTaskHandle
  );
  if (ok != pdPASS) {
    fanTaskHandle = nullptr;
    DEBUG_PRINTLN("[Device] Failed to start FanCtrlTask ");
  } else {
    DEBUG_PRINTLN("[Device] FanCtrlTask started ");
  }
}

void Device::stopFanControlTask() {
  if (fanTaskHandle) {
    vTaskDelete(fanTaskHandle);
    fanTaskHandle = nullptr;
    DEBUG_PRINTLN("[Device] FanCtrlTask stopped ");
  }
}

void Device::fanControlTask(void* param) {
  Device* self = static_cast<Device*>(param);
  const TickType_t period = pdMS_TO_TICKS(FAN_CTRL_PERIOD_MS);

  // Ensure FanManager is alive
  FAN->begin();

  for (;;) {
    // If 12V path is gone, shut fans off gracefully.
    if (!self->is12VPresent()) { // uses your existing helper
      if (self->lastCapFanPct) { FAN->stopCap();      self->lastCapFanPct = 0; }
      if (self->lastHsFanPct)  { FAN->stopHeatsink(); self->lastHsFanPct  = 0; }
      vTaskDelay(period);
      continue;
    }

    // Read temperatures via semantic roles
    float tHS = NAN;
    float tB0 = NAN, tB1 = NAN, tCAP = NAN;

    if (self->tempSensor) {
      tHS = self->tempSensor->getHeatsinkTemp(); // role-based (Heatsink)
      tB0 = self->tempSensor->getBoardTemp(0);   // Board0
      tB1 = self->tempSensor->getBoardTemp(1);   // Board1
    }

    // Capacitor/board fan uses the hotter of the two board sensors
    if (isfinite(tB0) && isfinite(tB1))      tCAP = max(tB0, tB1);
    else if (isfinite(tB0))                  tCAP = tB0;
    else if (isfinite(tB1))                  tCAP = tB1; // else stays NAN

    // Compute targets with hysteresis & min-run
    uint8_t capPct = _mapTempToPct(tCAP, CAP_FAN_ON_C, CAP_FAN_FULL_C,
                                   CAP_FAN_OFF_C, self->lastCapFanPct);

    uint8_t hsPct  = _mapTempToPct(tHS, HS_FAN_ON_C, HS_FAN_FULL_C,
                                   HS_FAN_OFF_C, self->lastHsFanPct);

    // Apply only if changed by > deadband
    if ((capPct == 0 && self->lastCapFanPct != 0) ||
        (capPct > 0 && abs((int)capPct - (int)self->lastCapFanPct) >= FAN_CMD_DEADBAND_PCT)) {
      if (capPct == 0) FAN->stopCap();
      else             FAN->setCapSpeedPercent(capPct);
      self->lastCapFanPct = capPct;
    }

    if ((hsPct == 0 && self->lastHsFanPct != 0) ||
        (hsPct > 0 && abs((int)hsPct - (int)self->lastHsFanPct) >= FAN_CMD_DEADBAND_PCT)) {
      if (hsPct == 0) FAN->stopHeatsink();
      else            FAN->setHeatsinkSpeedPercent(hsPct);
      self->lastHsFanPct = hsPct;
    }

    vTaskDelay(period);
  }
}
