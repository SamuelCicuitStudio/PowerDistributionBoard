#include <DeviceTransport.hpp>

DeviceTransport* DeviceTransport::s_inst = nullptr;
static TaskHandle_t s_calTaskHandle = nullptr;

namespace {
static const char* kWireAccessKeys[HeaterManager::kWireCount] = {
  OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
  OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
};
static const char* kWireResKeys[HeaterManager::kWireCount] = {
  R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
  R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
};
static const char* kWireCalibDoneKeys[HeaterManager::kWireCount] = {
  CALIB_W1_DONE_KEY, CALIB_W2_DONE_KEY, CALIB_W3_DONE_KEY, CALIB_W4_DONE_KEY, CALIB_W5_DONE_KEY,
  CALIB_W6_DONE_KEY, CALIB_W7_DONE_KEY, CALIB_W8_DONE_KEY, CALIB_W9_DONE_KEY, CALIB_W10_DONE_KEY
};

static bool setupConfigOk() {
  if (!CONF) return false;

  const String devId = CONF->GetString(DEV_ID_KEY, "");
  const String adminId = CONF->GetString(ADMIN_ID_KEY, "");
  const String adminPass = CONF->GetString(ADMIN_PASS_KEY, "");
  const String staSsid = CONF->GetString(STA_SSID_KEY, "");
  const String staPass = CONF->GetString(STA_PASS_KEY, "");
  const String apName = CONF->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, "");
  const String apPass = CONF->GetString(DEVICE_AP_AUTH_PASS_KEY, "");
  if (devId.isEmpty() || adminId.isEmpty() || adminPass.isEmpty() ||
      staSsid.isEmpty() || staPass.isEmpty() || apName.isEmpty() || apPass.isEmpty()) {
    return false;
  }

  const float tempTrip = CONF->GetFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
  const float tempWarn = CONF->GetFloat(TEMP_WARN_KEY, DEFAULT_TEMP_WARN_C);
  const float floorMax = CONF->GetFloat(FLOOR_MAX_C_KEY, DEFAULT_FLOOR_MAX_C);
  const float nichromeMax =
      CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY, DEFAULT_NICHROME_FINAL_TEMP_C);
  const float floorMargin =
      CONF->GetFloat(FLOOR_SWITCH_MARGIN_C_KEY, DEFAULT_FLOOR_SWITCH_MARGIN_C);
  const float currLimit = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
  if (!isfinite(tempTrip) || tempTrip <= 0.0f ||
      !isfinite(tempWarn) || tempWarn <= 0.0f ||
      !isfinite(floorMax) || floorMax <= 0.0f ||
      !isfinite(nichromeMax) || nichromeMax <= 0.0f ||
      !isfinite(floorMargin) || floorMargin <= 0.0f ||
      !isfinite(currLimit) || currLimit < 0.0f) {
    return false;
  }

  const int currentSource = CONF->GetInt(CURRENT_SOURCE_KEY, DEFAULT_CURRENT_SOURCE);
  if (currentSource != CURRENT_SRC_ACS && currentSource != CURRENT_SRC_ESTIMATE) {
    return false;
  }

  const int acFreq = CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
  const float acVolt = CONF->GetFloat(AC_VOLTAGE_KEY, DEFAULT_AC_VOLTAGE);
  const float chargeRes = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
  if (acFreq <= 0 || !isfinite(acVolt) || acVolt <= 0.0f ||
      !isfinite(chargeRes) || chargeRes <= 0.0f) {
    return false;
  }

  const float ohmPerM = CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M);
  const int gauge = CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE);
  if (!isfinite(ohmPerM) || ohmPerM <= 0.0f || gauge <= 0) {
    return false;
  }

  const int ntcGate = CONF->GetInt(NTC_GATE_INDEX_KEY, DEFAULT_NTC_GATE_INDEX);
  if (ntcGate < 1 || ntcGate > HeaterManager::kWireCount) {
    return false;
  }

  const float ntcBeta = CONF->GetFloat(NTC_BETA_KEY, DEFAULT_NTC_BETA);
  if (!isfinite(ntcBeta) || ntcBeta <= 0.0f) {
    return false;
  }
  const float ntcT0C = CONF->GetFloat(NTC_T0_C_KEY, DEFAULT_NTC_T0_C);
  if (!isfinite(ntcT0C)) {
    return false;
  }
  const float ntcR0 = CONF->GetFloat(NTC_R0_KEY, DEFAULT_NTC_R0_OHMS);
  if (!isfinite(ntcR0) || ntcR0 <= 0.0f) {
    return false;
  }
  const float ntcFixed = CONF->GetFloat(NTC_FIXED_RES_KEY, DEFAULT_NTC_FIXED_RES_OHMS);
  if (!isfinite(ntcFixed) || ntcFixed <= 0.0f) {
    return false;
  }

  bool anyEnabled = false;
  for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
    const bool allowed = CONF->GetBool(kWireAccessKeys[i], false);
    if (!allowed) continue;
    anyEnabled = true;
    const float r = CONF->GetFloat(kWireResKeys[i], DEFAULT_WIRE_RES_OHMS);
    if (!isfinite(r) || r <= 0.01f) {
      return false;
    }
  }
  if (!anyEnabled) return false;

  return true;
}

static bool setupCalibOk() {
  if (!CONF) return false;
  if (!CONF->GetBool(CALIB_CAP_DONE_KEY, DEFAULT_CALIB_CAP_DONE)) return false;
  const float capF = CONF->GetFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
  if (!isfinite(capF) || capF <= 0.0f) return false;

  for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
    const bool allowed = CONF->GetBool(kWireAccessKeys[i], false);
    if (!allowed) continue;
    if (!CONF->GetBool(kWireCalibDoneKeys[i], DEFAULT_CALIB_W_DONE)) {
      return false;
    }
  }
  if (!CONF->GetBool(CALIB_PRESENCE_DONE_KEY, DEFAULT_CALIB_PRESENCE_DONE)) return false;
  if (!CONF->GetBool(CALIB_FLOOR_DONE_KEY, DEFAULT_CALIB_FLOOR_DONE)) return false;

  return true;
}

static bool setupRunAllowed() {
  if (!CONF) return false;
  const bool setupDone = CONF->GetBool(SETUP_DONE_KEY, DEFAULT_SETUP_DONE);
  if (!setupDone) return false;
  return setupConfigOk() && setupCalibOk();
}
} // namespace

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

bool DeviceTransport::waitForStateEvent(Device::StateSnapshot& out, TickType_t toTicks) {
  if (!DEVICE) {
    vTaskDelay(toTicks);
    return false;
  }
  return DEVICE->waitForStateEvent(out, toTicks);
}

bool DeviceTransport::requestRun() {
  if (!DEVICE || !gEvt) return false;
  if (!setupRunAllowed()) {
    DEVICE->setLastStopReason("Setup incomplete");
    return false;
  }
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

bool DeviceTransport::startEnergyCalibration(float targetC,
                                             uint8_t wireIndex,
                                             Device::EnergyRunPurpose purpose,
                                             float dutyFrac) {
  if (!DEVICE) return false;
  return DEVICE->startEnergyCalibration(targetC, wireIndex, purpose, dutyFrac);
}

bool DeviceTransport::probeWirePresence() {
  if (!DEVICE) return false;
  return DEVICE->probeWirePresence();
}

bool DeviceTransport::confirmWiresCool() {
  if (!DEVICE) return false;
  return DEVICE->confirmWiresCool();
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
