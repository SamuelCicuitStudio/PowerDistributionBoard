/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef DEVICE_TRANSPORT_H
#define DEVICE_TRANSPORT_H

#include "system/Device.h"
#include "system/StatusSnapshot.h"

/**
 * @brief Thin facade for WiFi/UI to interact with Device without touching internals.
 */
class DeviceTransport {
public:
  static DeviceTransport* Get();

  Device::StateSnapshot getStateSnapshot() const;
  bool waitForStateEvent(Device::StateSnapshot& out, TickType_t toTicks);

  // Requests (thin wrappers)
  bool requestRun();
  bool requestStop();
  bool requestIdle();
  bool requestWake();
  bool ensureLoopTask();

  // Telemetry snapshot reused by WiFiManager snapshot task
  bool getTelemetry(StatusSnapshot& out) const;

  // Output / relay helpers for UI control paths
  bool setRelay(bool on);
  bool setOutput(uint8_t idx, bool on, bool allowUser);
  bool setFanSpeedPercent(int pct);

  // Config/NVS setters (centralized)
  bool setLedFeedback(bool on);
  bool setOnTimeMs(int v);
  bool setOffTimeMs(int v);
  bool setDesiredVoltage(float v);
  bool setAcFrequency(int v);
  bool setChargeResistor(float v);
  bool setDcVoltage(float v);
  bool setAccessFlag(uint8_t idx, bool on);
  bool setWireRes(uint8_t idx, float ohms);
  bool setTargetRes(float ohms);
  bool setWireOhmPerM(float ohmsPerM);
  bool setWireGaugeAwg(int awg);
  bool setBuzzerMute(bool on);
  bool setManualMode(bool manual);
  bool setCoolingProfile(bool fast);
  bool setLoopMode(uint8_t mode);
  bool setCurrentLimitA(float limitA);
  bool requestResetFlagAndRestart();
  bool startCalibrationTask(uint32_t timeoutMs = 10000);

private:
  bool sendCommandAndWait(Device::DevCmdType t, int32_t i1 = 0, float f1 = 0.0f, bool b1 = false, TickType_t to = pdMS_TO_TICKS(500));

private:
  static DeviceTransport* s_inst;
  DeviceTransport() = default;
};

#define DEVTRAN DeviceTransport::Get()

#endif // DEVICE_TRANSPORT_H
