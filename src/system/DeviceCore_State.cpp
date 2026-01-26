#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

// Single, shared instances (linked once)
SemaphoreHandle_t gStateMtx = nullptr;
EventGroupHandle_t gEvt     = nullptr;

static const char* deviceStateName(DeviceState s) {
  switch (s) {
    case DeviceState::Idle:     return "Idle";
    case DeviceState::Running:  return "Running";
    case DeviceState::Error:    return "Error";
    case DeviceState::Shutdown: return "Shutdown";
    default:                    return "?";
  }
}

Device::StateSnapshot Device::getStateSnapshot() const {
  StateSnapshot snap{};
  if (gStateMtx && xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE) {
    snap.state   = currentState;
    snap.sinceMs = stateSinceMs;
    snap.seq     = stateSeq;
    xSemaphoreGive(gStateMtx);
  } else {
    snap.state   = currentState;
    snap.sinceMs = stateSinceMs;
    snap.seq     = stateSeq;
  }
  return snap;
}

DeviceState Device::getState() const {
  return currentState;
}

bool Device::waitForStateEvent(StateSnapshot& out, TickType_t toTicks) {
  if (!stateEvtQueue) {
    // queue not ready yet; wait and report false
    vTaskDelay(toTicks);
    return false;
  }
  return xQueueReceive(stateEvtQueue, &out, toTicks) == pdTRUE;
}

void Device::setState(DeviceState next) {
  DeviceState prev;

  if (gStateMtx &&
      xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE) {
    prev = currentState;
    if (prev == next) {
      xSemaphoreGive(gStateMtx);
      return;
    }
    currentState = next;
    stateSeq++;
    stateSinceMs = millis();
    xSemaphoreGive(gStateMtx);
  } else {
    prev = currentState;
    if (prev == next) return;
    currentState = next;
    stateSeq++;
    stateSinceMs = millis();
  }

  StateSnapshot snap{};
  snap.state   = next;
  snap.sinceMs = stateSinceMs;
  snap.seq     = stateSeq;
  pushStateEvent(snap);

  onStateChanged(prev, next);
}

void Device::onStateChanged(DeviceState prev, DeviceState next) {
  DEBUG_PRINTF("[Device] State changed: %s -> %s\n",
               deviceStateName(prev),
               deviceStateName(next));
}

bool Device::pushStateEvent(const StateSnapshot& snap) {
  if (!stateEvtQueue) return false;
  if (xQueueSendToBack(stateEvtQueue, &snap, 0) == pdTRUE) return true;

  StateSnapshot dump{};
  xQueueReceive(stateEvtQueue, &dump, 0); // drop oldest
  return xQueueSendToBack(stateEvtQueue, &snap, 0) == pdTRUE;
}
