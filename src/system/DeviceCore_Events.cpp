#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>

bool Device::waitForEventNotice(EventNotice& out, TickType_t toTicks) {
  if (!eventEvtQueue) {
    vTaskDelay(toTicks);
    return false;
  }
  return xQueueReceive(eventEvtQueue, &out, toTicks) == pdTRUE;
}

void Device::pushEventUnlocked(Device* self,
                               Device::EventKind kind,
                               const char* reason,
                               uint32_t nowMs,
                               uint32_t epoch) {
  if (!self || !reason || !reason[0]) return;

  Device::EventEntry& e = self->eventHistory[self->eventHead];
  e.kind = kind;
  e.ms = nowMs;
  e.epoch = epoch;
  strncpy(e.reason, reason, sizeof(e.reason) - 1);
  e.reason[sizeof(e.reason) - 1] = '\0';

  self->eventHead = (self->eventHead + 1) % self->kEventHistorySize;
  if (self->eventCount < self->kEventHistorySize) {
    self->eventCount++;
  }

  if (kind == Device::EventKind::Warning) {
    Device::EventEntry& w = self->warnHistory[self->warnHistoryHead];
    w = e;
    self->warnHistoryHead =
      (self->warnHistoryHead + 1) % self->kEventHistorySize;
    if (self->warnHistoryCount < self->kEventHistorySize) {
      self->warnHistoryCount++;
    }
  } else if (kind == Device::EventKind::Error) {
    Device::EventEntry& r = self->errorHistory[self->errorHistoryHead];
    r = e;
    self->errorHistoryHead =
      (self->errorHistoryHead + 1) % self->kEventHistorySize;
    if (self->errorHistoryCount < self->kEventHistorySize) {
      self->errorHistoryCount++;
    }
  }

  if (kind == Device::EventKind::Warning) {
    if (self->unreadWarn < self->kEventHistorySize) self->unreadWarn++;
  } else if (kind == Device::EventKind::Error) {
    if (self->unreadErr < self->kEventHistorySize) self->unreadErr++;
  }

  Device::EventNotice note{};
  note.kind = kind;
  note.ms = nowMs;
  note.epoch = epoch;
  note.unreadWarn = self->unreadWarn;
  note.unreadErr = self->unreadErr;
  strncpy(note.reason, reason, sizeof(note.reason) - 1);
  note.reason[sizeof(note.reason) - 1] = '\0';
  self->pushEventNotice(note);
}

void Device::setLastErrorReason(const char* reason) {
  if (!reason || !reason[0]) return;
  if (eventMtx == nullptr) {
    eventMtx = xSemaphoreCreateMutex();
  }
  const uint32_t nowMs = millis();
  uint32_t epoch = 0;
  if (RTC) {
    epoch = static_cast<uint32_t>(RTC->getUnixTime());
  }
  if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    strncpy(lastErrorReason, reason, sizeof(lastErrorReason) - 1);
    lastErrorReason[sizeof(lastErrorReason) - 1] = '\0';
    lastErrorMs = nowMs;
    lastErrorEpoch = epoch;
    pushEventUnlocked(this, EventKind::Error, reason, nowMs, epoch);
    xSemaphoreGive(eventMtx);
  } else {
    strncpy(lastErrorReason, reason, sizeof(lastErrorReason) - 1);
    lastErrorReason[sizeof(lastErrorReason) - 1] = '\0';
    lastErrorMs = nowMs;
    lastErrorEpoch = epoch;
    pushEventUnlocked(this, EventKind::Error, reason, nowMs, epoch);
  }
}

void Device::addWarningReason(const char* reason) {
  if (!reason || !reason[0]) return;
  if (eventMtx == nullptr) {
    eventMtx = xSemaphoreCreateMutex();
  }
  const uint32_t nowMs = millis();
  uint32_t epoch = 0;
  if (RTC) {
    epoch = static_cast<uint32_t>(RTC->getUnixTime());
  }
  if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    pushEventUnlocked(this, EventKind::Warning, reason, nowMs, epoch);
    xSemaphoreGive(eventMtx);
  } else {
    pushEventUnlocked(this, EventKind::Warning, reason, nowMs, epoch);
  }
}

void Device::setLastStopReason(const char* reason) {
  if (!reason || !reason[0]) return;
  if (eventMtx == nullptr) {
    eventMtx = xSemaphoreCreateMutex();
  }
  const uint32_t nowMs = millis();
  uint32_t epoch = 0;
  if (RTC) {
    epoch = static_cast<uint32_t>(RTC->getUnixTime());
  }
  if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    strncpy(lastStopReason, reason, sizeof(lastStopReason) - 1);
    lastStopReason[sizeof(lastStopReason) - 1] = '\0';
    lastStopMs = nowMs;
    lastStopEpoch = epoch;
    xSemaphoreGive(eventMtx);
  } else {
    strncpy(lastStopReason, reason, sizeof(lastStopReason) - 1);
    lastStopReason[sizeof(lastStopReason) - 1] = '\0';
    lastStopMs = nowMs;
    lastStopEpoch = epoch;
  }
}

Device::LastEventInfo Device::getLastEventInfo() const {
  LastEventInfo out{};
  if (const_cast<Device*>(this)->eventMtx &&
      xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (lastErrorReason[0]) {
      out.hasError = true;
      out.errorMs = lastErrorMs;
      out.errorEpoch = lastErrorEpoch;
      strncpy(out.errorReason, lastErrorReason, sizeof(out.errorReason) - 1);
    }
    if (lastStopReason[0]) {
      out.hasStop = true;
      out.stopMs = lastStopMs;
      out.stopEpoch = lastStopEpoch;
      strncpy(out.stopReason, lastStopReason, sizeof(out.stopReason) - 1);
    }
    xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
  } else {
    if (lastErrorReason[0]) {
      out.hasError = true;
      out.errorMs = lastErrorMs;
      out.errorEpoch = lastErrorEpoch;
      strncpy(out.errorReason, lastErrorReason, sizeof(out.errorReason) - 1);
    }
    if (lastStopReason[0]) {
      out.hasStop = true;
      out.stopMs = lastStopMs;
      out.stopEpoch = lastStopEpoch;
      strncpy(out.stopReason, lastStopReason, sizeof(out.stopReason) - 1);
    }
  }
  out.errorReason[sizeof(out.errorReason) - 1] = '\0';
  out.stopReason[sizeof(out.stopReason) - 1] = '\0';
  return out;
}

size_t Device::getEventHistory(EventEntry* out, size_t maxOut) const {
  if (!out || maxOut == 0) return 0;
  size_t count = 0;
  auto copyOut = [&](size_t n) {
    const size_t total = n;
    for (size_t i = 0; i < total && count < maxOut; ++i) {
      const size_t idx =
        (eventHead + kEventHistorySize - 1 - i) % kEventHistorySize;
      out[count++] = eventHistory[idx];
    }
  };

  if (const_cast<Device*>(this)->eventMtx &&
      xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    copyOut(eventCount);
    xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
  } else {
    copyOut(eventCount);
  }
  return count;
}

size_t Device::getErrorHistory(EventEntry* out, size_t maxOut) const {
  if (!out || maxOut == 0) return 0;
  size_t count = 0;
  auto copyOut = [&](size_t n) {
    const size_t total = n;
    for (size_t i = 0; i < total && count < maxOut; ++i) {
      const size_t idx =
        (errorHistoryHead + kEventHistorySize - 1 - i) %
        kEventHistorySize;
      out[count++] = errorHistory[idx];
    }
  };

  if (const_cast<Device*>(this)->eventMtx &&
      xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    copyOut(errorHistoryCount);
    xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
  } else {
    copyOut(errorHistoryCount);
  }
  return count;
}

size_t Device::getWarningHistory(EventEntry* out, size_t maxOut) const {
  if (!out || maxOut == 0) return 0;
  size_t count = 0;
  auto copyOut = [&](size_t n) {
    const size_t total = n;
    for (size_t i = 0; i < total && count < maxOut; ++i) {
      const size_t idx =
        (warnHistoryHead + kEventHistorySize - 1 - i) %
        kEventHistorySize;
      out[count++] = warnHistory[idx];
    }
  };

  if (const_cast<Device*>(this)->eventMtx &&
      xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    copyOut(warnHistoryCount);
    xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
  } else {
    copyOut(warnHistoryCount);
  }
  return count;
}

void Device::getUnreadEventCounts(uint8_t& warnCount, uint8_t& errCount) const {
  warnCount = 0;
  errCount = 0;
  if (const_cast<Device*>(this)->eventMtx &&
      xSemaphoreTake(const_cast<Device*>(this)->eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    warnCount = unreadWarn;
    errCount = unreadErr;
    xSemaphoreGive(const_cast<Device*>(this)->eventMtx);
  } else {
    warnCount = unreadWarn;
    errCount = unreadErr;
  }
}

void Device::markEventHistoryRead() {
  if (eventMtx == nullptr) {
    eventMtx = xSemaphoreCreateMutex();
  }
  if (eventMtx && xSemaphoreTake(eventMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    unreadWarn = 0;
    unreadErr = 0;
    xSemaphoreGive(eventMtx);
  } else {
    unreadWarn = 0;
    unreadErr = 0;
  }
}

bool Device::pushEventNotice(const EventNotice& note) {
  if (!eventEvtQueue) return false;
  if (xQueueSendToBack(eventEvtQueue, &note, 0) == pdTRUE) return true;

  EventNotice dump{};
  xQueueReceive(eventEvtQueue, &dump, 0); // drop oldest
  return xQueueSendToBack(eventEvtQueue, &note, 0) == pdTRUE;
}

Device::Device(TempSensor* temp,
               CurrentSensor* current,
               Relay* relay,
               CpDischg* discharger,
               Indicator* ledIndicator)
  : tempSensor(temp),
    currentSensor(current),
    relayControl(relay),
    discharger(discharger),
    indicator(ledIndicator) {}
