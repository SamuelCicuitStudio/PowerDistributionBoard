#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

// ===== Singleton storage & accessors =====
Device* Device::instance = nullptr;

void Device::Init(TempSensor* temp,CurrentSensor* current,Relay* relay,CpDischg* discharger,Indicator* ledIndicator) {
  if (!instance) {
    instance = new Device(temp, current, relay, discharger, ledIndicator);
  }
}

Device* Device::Get() {
  return instance; // nullptr until Init(), or set in begin() below if constructed manually
}

void Device::prepareForDeepSleep() {
  DEBUG_PRINTLN("[Device] Preparing for deep sleep (power down paths)");
  stopWireTargetTest();
  stopTemperatureMonitor();
  stopFanControlTask();

  if (FAN) {
    FAN->stopCap();
    FAN->stopHeatsink();
    FAN->setSpeedPercent(0);
  }

  if (WIRE) WIRE->disableAll();
  if (indicator) indicator->clearAll();
  if (relayControl) relayControl->turnOff();
  if (discharger) discharger->setBypassRelayGate(false);

  RGB->setOff();
  setState(DeviceState::Shutdown);
}

void Device::begin() {
  // Adopt stack/static construction if user didn't call Init()
  if (!instance) instance = this;

  if (!gStateMtx) gStateMtx = xSemaphoreCreateMutex();
  if (!gEvt)      gEvt      = xEventGroupCreate();

  if (!stateEvtQueue) stateEvtQueue = xQueueCreate(8, sizeof(StateSnapshot));
  if (!eventEvtQueue) eventEvtQueue = xQueueCreate(8, sizeof(EventNotice));
  if (!cmdQueue)  cmdQueue  = xQueueCreate(12, sizeof(DevCommand));
  if (!ackQueue)  ackQueue  = xQueueCreate(12, sizeof(DevCommandAck));
  if (!controlMtx) controlMtx = xSemaphoreCreateMutex();

  setState(DeviceState::Shutdown);        // OFF at boot
  wifiStatus = WiFiStatus::NotConnected;
  RGB->setOff();                          // LEDs off at boot

  DEBUGGSTART();
  DEBUG_PRINTLN("###########################################################");
  DEBUG_PRINTLN("#                 Starting Device Manager               #");
  DEBUG_PRINTLN("###########################################################");
  DEBUGGSTOP();

  pinMode(DETECT_12V_PIN, INPUT);

  // Boot cues (background + overlay + sound)
  BUZZ->bipStartupSequence();
  RGB->postOverlay(OverlayEvent::WAKE_FLASH);

  wireConfigStore.loadFromNvs();
  checkAllowedOutputs();
  loadRuntimeSettings();

  // Per-channel LED feedback maintainer
  xTaskCreate(
    Device::LedUpdateTask,
    "LedUpdateTask",
    LED_UPDATE_TASK_STACK_SIZE,
    this,
    LED_UPDATE_TASK_PRIORITY,
    &ledTaskHandle
  );

  // Initialize persistent power/session statistics
  POWER_TRACKER->begin();

  // Start fans (dual-channel) and the closed-loop control task
  startFanControlTask(); // runs continuously; reads DS18B20 roles

  // Start external command handler
  startCommandTask();

  // Start bus sampler (synchronized voltage+current history)
  busSampler = BUS_SAMPLER;
  if (busSampler && discharger) {
    busSampler->begin(currentSensor, discharger, 5);
    busSampler->attachNtc(NTC);
  }

  // Current sensor stays idle unless explicitly needed (wire presence probing).
  // Apply persisted over-current limit (default to hardware safe limit)
  if (currentSensor && CONF) {
    float limitA = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
    if (limitA < 0.0f) limitA = 0.0f;
    currentSensor->configureOverCurrent(limitA, CURRENT_TIME);
  }

  startThermalTask();
  startControlTask();

  DEBUG_PRINTLN("[Device] Configuring system I/O pins");
}

void Device::shutdown() {
  DEBUGGSTART();
  DEBUG_PRINTLN("-----------------------------------------------------------");
  DEBUG_PRINTLN("[Device] Initiating Shutdown Sequence ");
  DEBUG_PRINTLN("-----------------------------------------------------------");
  DEBUG_PRINTLN("[Device] Main loop finished, proceeding to shutdown");
  DEBUGGSTOP();

  BUZZ->bipSystemShutdown();
  stopWireTargetTest();
  stopTemperatureMonitor();

  DEBUG_PRINTLN("[Device] Turning OFF Main Relay");
  RGB->postOverlay(OverlayEvent::RELAY_OFF);
  relayControl->turnOn(); // original behavior kept

  DEBUG_PRINTLN("[Device] Starting Capacitor Discharge");
  // discharger->discharge();

  DEBUG_PRINTLN("[Device] Updating Status LEDs");
  RGB->setOff();  // final visual
  stopFanControlTask();
  FAN->stopCap();
  FAN->stopHeatsink();

  DEBUGGSTART();
  DEBUG_PRINTLN("[Device] Shutdown Complete System is Now OFF ");
  DEBUG_PRINTLN("-----------------------------------------------------------");
  DEBUGGSTOP();
}

void Device::stopLoopTask() {
  if (loopTaskHandle != nullptr) {
    DEBUG_PRINTLN("[Device] Stopping Device Loop Task ");
    vTaskDelete(loopTaskHandle);
    loopTaskHandle = nullptr;
  } else {
    DEBUG_PRINTLN("[Device] Loop Task not running no action taken ");
  }
}
