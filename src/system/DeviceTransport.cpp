#include "system/DeviceTransport.h"

DeviceTransport* DeviceTransport::s_inst = nullptr;
static TaskHandle_t s_calTaskHandle = nullptr;

DeviceTransport* DeviceTransport::Get() {
  if (!s_inst) s_inst = new DeviceTransport();
  return s_inst;
}

Device::StateSnapshot DeviceTransport::getStateSnapshot() const {
  if (!DEVICE) {
    Device::StateSnapshot snap{};
    snap.state = DeviceState::Shutdown;
    snap.seq = 0;
    snap.sinceMs = 0;
    return snap;
  }
  return DEVICE->getStateSnapshot();
}

bool DeviceTransport::isManualMode() const {
  return DEVICE ? DEVICE->manualMode : false;
}

bool DeviceTransport::waitForStateEvent(Device::StateSnapshot& out, TickType_t toTicks) {
  if (!DEVICE) {
    vTaskDelay(toTicks);
    return false;
  }
  return DEVICE->waitForStateEvent(out, toTicks);
}

bool DeviceTransport::requestRun() {
  if (!DEVICE || !gEvt) return false;
  DEVICE->stopWireTargetTest();
  ensureLoopTask();
  xEventGroupSetBits(gEvt, EVT_WAKE_REQ | EVT_RUN_REQ);
  return true;
}

bool DeviceTransport::requestStop() {
  if (!DEVICE || !gEvt) return false;
  DEVICE->stopWireTargetTest();
  DEVICE->setLastStopReason("Stop requested");
  xEventGroupSetBits(gEvt, EVT_STOP_REQ);
  return true;
}

bool DeviceTransport::requestWake() {
  if (!DEVICE || !gEvt) return false;
  ensureLoopTask();
  xEventGroupSetBits(gEvt, EVT_WAKE_REQ);
  return true;
}

bool DeviceTransport::ensureLoopTask() {
  if (!DEVICE) return false;
  DEVICE->startLoopTask();
  return true;
}

bool DeviceTransport::requestIdle() {
  if (!DEVICE || !gEvt) return false;
  DEVICE->stopWireTargetTest();
  DEVICE->setLastStopReason("Idle requested");
  xEventGroupSetBits(gEvt, EVT_STOP_REQ);
  return true;
}

bool DeviceTransport::getTelemetry(StatusSnapshot& out) const {
  if (!DEVICE) return false;

  // Minimal telemetry used by WiFiManager snapshot task
  out.capVoltage = (DEVICE->discharger ? DEVICE->discharger->readCapVoltage() : 0.0f);
  out.capAdcScaled = (DEVICE->discharger ? DEVICE->discharger->readCapAdcScaled() : 0.0f);
  out.current    = (DEVICE->currentSensor ? DEVICE->currentSensor->readCurrent() : 0.0f);

  uint8_t n = 0;
  if (DEVICE->tempSensor) n = DEVICE->tempSensor->getSensorCount();
  if (n > MAX_TEMP_SENSORS) n = MAX_TEMP_SENSORS;
  for (uint8_t i = 0; i < n; ++i) {
    const float t = DEVICE->tempSensor->getTemperature(i);
    out.temps[i] = isfinite(t) ? t : -127.0f;
  }
  for (uint8_t i = n; i < MAX_TEMP_SENSORS; ++i) {
    out.temps[i] = -127.0f;
  }

  // Wire-level telemetry: sync WireStateModel from HeaterManager + config,
  // then use WireTelemetryAdapter to fill the snapshot.
  WireConfigStore& cfg  = DEVICE->getWireConfigStore();
  WireStateModel&  wst  = DEVICE->getWireStateModel();

  for (uint8_t i = 1; i <= HeaterManager::kWireCount; ++i) {
    WireRuntimeState& ws = wst.wire(i);
    if (WIRE) {
      WireInfo wi = WIRE->getWireInfo(i);
      ws.tempC           = wi.temperatureC;
      ws.present         = wi.connected;
      ws.lastUpdateMs    = millis();
    }
    ws.allowedByAccess = cfg.getAccessFlag(i);
  }
  if (WIRE) {
    wst.setLastMask(WIRE->getOutputMask());
  }

  DEVICE->getWireTelemetryAdapter().fillSnapshot(out, cfg, wst);

  out.acPresent = (digitalRead(DETECT_12V_PIN) == HIGH);
  out.relayOn   = (DEVICE->relayControl ? DEVICE->relayControl->isOn() : false);
  out.updatedMs = millis();
  return true;
}

bool DeviceTransport::setRelay(bool on, bool waitAck) {
  if (waitAck) {
    return sendCommandAndWait(Device::DevCmdType::SET_RELAY, 0, 0.0f, on);
  }
  return sendCommandNoWait(Device::DevCmdType::SET_RELAY, 0, 0.0f, on);
}

bool DeviceTransport::setOutput(uint8_t idx, bool on, bool allowUser, bool waitAck) {
  if (idx < 1 || idx > HeaterManager::kWireCount) return false;
  // allowUser currently unused; Device decides safety
  if (waitAck) {
    return sendCommandAndWait(Device::DevCmdType::SET_OUTPUT, idx, 0.0f, on);
  }
  return sendCommandNoWait(Device::DevCmdType::SET_OUTPUT, idx, 0.0f, on);
}

bool DeviceTransport::setFanSpeedPercent(int pct, bool waitAck) {
  pct = constrain(pct, 0, 100);
  if (waitAck) {
    return sendCommandAndWait(Device::DevCmdType::SET_FAN_SPEED, pct);
  }
  return sendCommandNoWait(Device::DevCmdType::SET_FAN_SPEED, pct);
}

// -------------------- Config setters --------------------
bool DeviceTransport::setLedFeedback(bool on)           { return sendCommandAndWait(Device::DevCmdType::SET_LED_FEEDBACK, 0, 0.0f, on); }
bool DeviceTransport::setAcFrequency(int v)             { return sendCommandAndWait(Device::DevCmdType::SET_AC_FREQ, v); }
bool DeviceTransport::setChargeResistor(float v)        { return sendCommandAndWait(Device::DevCmdType::SET_CHARGE_RES, 0, v); }
bool DeviceTransport::setAccessFlag(uint8_t idx, bool on) {
  return sendCommandAndWait(Device::DevCmdType::SET_ACCESS_FLAG, idx, 0.0f, on);
}
bool DeviceTransport::setWireRes(uint8_t idx, float ohms) {
  return sendCommandAndWait(Device::DevCmdType::SET_WIRE_RES, idx, ohms);
}
bool DeviceTransport::setWireOhmPerM(float ohmsPerM)    { return sendCommandAndWait(Device::DevCmdType::SET_WIRE_OHM_PER_M, 0, ohmsPerM); }
bool DeviceTransport::setWireGaugeAwg(int awg) {
  awg = constrain(awg, 1, 60);
  return sendCommandAndWait(Device::DevCmdType::SET_WIRE_GAUGE, awg);
}
bool DeviceTransport::setBuzzerMute(bool on)            { return sendCommandAndWait(Device::DevCmdType::SET_BUZZER_MUTE, 0, 0.0f, on); }
bool DeviceTransport::setManualMode(bool manual)        { return sendCommandAndWait(Device::DevCmdType::SET_MANUAL_MODE, 0, 0.0f, manual); }
bool DeviceTransport::setCurrentLimitA(float limitA)    { return sendCommandAndWait(Device::DevCmdType::SET_CURR_LIMIT, 0, limitA); }
bool DeviceTransport::requestResetFlagAndRestart() {
  return sendCommandAndWait(Device::DevCmdType::REQUEST_RESET);
}

bool DeviceTransport::startCalibrationTask(uint32_t timeoutMs) {
  if (!DEVICE) return false;
  if (s_calTaskHandle != nullptr) return false;
  if (DEVICE->getState() == DeviceState::Running) return false;

  const BaseType_t ok = xTaskCreate(
      [](void* pv) {
        const uint32_t toMs =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pv));
        if (DEVICE) {
          DEVICE->runCalibrationsStandalone(toMs);
        }
        s_calTaskHandle = nullptr;
        vTaskDelete(nullptr);
      },
      "CalibTask",
      4096,
      reinterpret_cast<void*>(static_cast<uintptr_t>(timeoutMs)),
      1,
      &s_calTaskHandle);

  if (ok != pdPASS) {
    s_calTaskHandle = nullptr;
    return false;
  }
  return true;
}

bool DeviceTransport::startWireTargetTest(float targetC, uint8_t wireIndex) {
  if (!DEVICE) return false;
  return DEVICE->startWireTargetTest(targetC, wireIndex);
}

void DeviceTransport::stopWireTargetTest() {
  if (DEVICE) {
    DEVICE->stopWireTargetTest();
  }
}

bool DeviceTransport::getWireTargetStatus(Device::WireTargetStatus& out) const {
  if (!DEVICE) return false;
  out = DEVICE->getWireTargetStatus();
  return true;
}

bool DeviceTransport::getFloorControlStatus(Device::FloorControlStatus& out) const {
  if (!DEVICE) return false;
  out = DEVICE->getFloorControlStatus();
  return true;
}

bool DeviceTransport::startEnergyCalibration(float targetC, uint8_t wireIndex, Device::EnergyRunPurpose purpose) {
  if (!DEVICE) return false;
  return DEVICE->startEnergyCalibration(targetC, wireIndex, purpose);
}

bool DeviceTransport::sendCommandAndWait(Device::DevCmdType t, int32_t i1, float f1, bool b1, TickType_t to) {
  if (!DEVICE) return false;
  Device::DevCommand cmd{};
  cmd.type = t;
  cmd.i1   = i1;
  cmd.f1   = f1;
  cmd.b1   = b1;
  DEBUG_PRINTF("[Transport] Cmd enqueue type=%d i1=%ld f1=%.3f b1=%d\n",
               static_cast<int>(t), static_cast<long>(i1), static_cast<double>(f1), b1 ? 1 : 0);
  if (!DEVICE->submitCommand(cmd)) {
    DEBUG_PRINTLN("[Transport] enqueue failed");
    return false;
  }
  Device::DevCommandAck ack{};
  if (!DEVICE->waitForCommandAck(ack, to)) {
    DEBUG_PRINTLN("[Transport] ack wait timeout");
    return false;
  }
  if (ack.type != t || ack.id != cmd.id) {
    DEBUG_PRINTF("[Transport] ack mismatch type=%d id=%lu (expected type=%d id=%lu)\n",
                 static_cast<int>(ack.type), static_cast<unsigned long>(ack.id),
                 static_cast<int>(t), static_cast<unsigned long>(cmd.id));
    return false;
  }
  DEBUG_PRINTF("[Transport] ack type=%d id=%lu success=%d\n",
               static_cast<int>(ack.type), static_cast<unsigned long>(ack.id),
               ack.success ? 1 : 0);
  return ack.success;
}

bool DeviceTransport::sendCommandNoWait(Device::DevCmdType t, int32_t i1, float f1, bool b1) {
  if (!DEVICE) return false;
  Device::DevCommand cmd{};
  cmd.type = t;
  cmd.i1   = i1;
  cmd.f1   = f1;
  cmd.b1   = b1;
  DEBUG_PRINTF("[Transport] Cmd enqueue (no-wait) type=%d i1=%ld f1=%.3f b1=%d\n",
               static_cast<int>(t), static_cast<long>(i1), static_cast<double>(f1), b1 ? 1 : 0);
  return DEVICE->submitCommand(cmd);
}
