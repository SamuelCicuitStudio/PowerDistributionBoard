#include <Device.hpp>
#include <Utils.hpp>
#include <RGBLed.hpp>    // keep
#include <Buzzer.hpp>    // BUZZ macro
#include <NtcSensor.hpp>
#include <RTCManager.hpp>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

bool Device::waitForCommandAck(DevCommandAck& ack, TickType_t toTicks) {
  if (!ackQueue) {
    vTaskDelay(toTicks);
    return false;
  }
  return xQueueReceive(ackQueue, &ack, toTicks) == pdTRUE;
}

bool Device::submitCommand(DevCommand& cmd) {
  if (!cmdQueue) return false;
  DevCommand c = cmd;
  // assign id
  if (gStateMtx && xSemaphoreTake(gStateMtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    cmdSeq++;
    c.id = cmdSeq;
    xSemaphoreGive(gStateMtx);
  } else {
    cmdSeq++;
    c.id = cmdSeq;
  }
  cmd.id = c.id;
  return xQueueSendToBack(cmdQueue, &c, 0) == pdTRUE;
}

void Device::startCommandTask() {
  if (cmdTaskHandle) return;
  xTaskCreate(
    Device::commandTask,
    "DevCmdTask",
    4096,
    this,
    1,
    &cmdTaskHandle
  );
}

void Device::commandTask(void* param) {
  Device* self = static_cast<Device*>(param);
  for (;;) {
    DevCommand cmd{};
    if (self->cmdQueue &&
        xQueueReceive(self->cmdQueue, &cmd, portMAX_DELAY) == pdTRUE) {
      self->handleCommand(cmd);
    }
  }
}

void Device::handleCommand(const DevCommand& cmd) {
  auto sendAck = [&](bool ok) {
    if (!ackQueue) return;
    DevCommandAck ack{cmd.type, cmd.id, ok};
    if (xQueueSendToBack(ackQueue, &ack, 0) != pdTRUE) {
      DevCommandAck dump{};
      xQueueReceive(ackQueue, &dump, 0);
      xQueueSendToBack(ackQueue, &ack, 0);
    }
  };

  auto requiresSafe = [&](DevCmdType t) {
    switch (t) {
      case DevCmdType::SET_BUZZER_MUTE:
      case DevCmdType::SET_RELAY:
      case DevCmdType::SET_OUTPUT:
      case DevCmdType::SET_FAN_SPEED:
        return false;
      default:
        return true;
    }
  };

  if (requiresSafe(cmd.type)) {
    while (getState() == DeviceState::Running) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }

  auto floatEq = [](float a, float b) {
    return fabsf(a - b) <= 1e-3f;
  };

  bool ok = true;
  switch (cmd.type) {
    case DevCmdType::SET_LED_FEEDBACK:
      if (CONF->GetBool(LED_FEEDBACK_KEY, false) != cmd.b1) {
        CONF->PutBool(LED_FEEDBACK_KEY, cmd.b1);
      }
      break;

    case DevCmdType::SET_AC_FREQ: {
      int hz = cmd.i1;
      if (hz < 50) hz = 50;
      if (hz > 500) hz = 500;
      if (CONF->GetInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY) != hz) {
        CONF->PutInt(AC_FREQUENCY_KEY, hz);
      }
      if (currentSensor && currentSensor->isContinuousRunning()) {
        const uint32_t periodMs = (hz > 0) ? std::max(2, static_cast<int>(lroundf(1000.0f / hz))) : 2;
        currentSensor->startContinuous(periodMs);
      }
    } break;

    case DevCmdType::SET_CHARGE_RES:
      if (!floatEq(CONF->GetFloat(CHARGE_RESISTOR_KEY, 0.0f), cmd.f1)) {
        CONF->PutFloat(CHARGE_RESISTOR_KEY, cmd.f1);
      }
      break;

    case DevCmdType::SET_ACCESS_FLAG: {
      const char* accessKeys[10] = {
        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
        OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
        OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
        OUT10_ACCESS_KEY
      };
      if (cmd.i1 >= 1 && cmd.i1 <= 10) {
        if (CONF->GetBool(accessKeys[cmd.i1 - 1], false) != cmd.b1) {
          CONF->PutBool(accessKeys[cmd.i1 - 1], cmd.b1);
        }
        wireConfigStore.setAccessFlag(cmd.i1, cmd.b1);
      } else {
        ok = false;
      }
      break;
    }

    case DevCmdType::SET_WIRE_RES: {
      const char* rkeys[10] = {
        R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
        R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
      };
      if (cmd.i1 >= 1 && cmd.i1 <= 10) {
        if (!floatEq(CONF->GetFloat(rkeys[cmd.i1 - 1], DEFAULT_WIRE_RES_OHMS), cmd.f1)) {
          CONF->PutFloat(rkeys[cmd.i1 - 1], cmd.f1);
        }
        wireConfigStore.setWireResistance(cmd.i1, cmd.f1);
        if (WIRE) {
          WIRE->setWireResistance(cmd.i1, cmd.f1);
        }
      } else {
        ok = false;
      }
      break;
    }

    case DevCmdType::SET_WIRE_OHM_PER_M:
      if (!floatEq(CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M), cmd.f1)) {
        CONF->PutFloat(WIRE_OHM_PER_M_KEY, cmd.f1);
      }
      wireConfigStore.setWireOhmPerM(cmd.f1);
      break;

    case DevCmdType::SET_WIRE_GAUGE: {
      int gauge = cmd.i1;
      if (gauge <= 0 || gauge > 60) {
        gauge = DEFAULT_WIRE_GAUGE;
      }
      if (CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE) != gauge) {
        CONF->PutInt(WIRE_GAUGE_KEY, gauge);
      }
      wireConfigStore.setWireGaugeAwg(gauge);
      if (WIRE) {
        WIRE->setWireGaugeAwg(gauge);
      }
      break;
    }

    case DevCmdType::SET_CURR_LIMIT: {
      float limitA = cmd.f1;
      if (limitA < 0.0f) limitA = 0.0f;
      if (CONF && !floatEq(CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A), limitA)) {
        CONF->PutFloat(CURR_LIMIT_KEY, limitA);
      }
      if (currentSensor) {
        currentSensor->configureOverCurrent(limitA, CURRENT_TIME);
      }
      break;
    }

    case DevCmdType::SET_BUZZER_MUTE:
      BUZZ->setMuted(cmd.b1);
      break;

    case DevCmdType::SET_FAN_SPEED: {
      int pct = constrain(cmd.i1, 0, 100);
      FAN->setSpeedPercent(pct);
      break;
    }

    case DevCmdType::SET_RELAY:
      if (relayControl) {
        if (cmd.b1) relayControl->turnOn();
        else        relayControl->turnOff();
      }
      break;

    case DevCmdType::SET_OUTPUT:
      if (cmd.i1 >= 1 && cmd.i1 <= HeaterManager::kWireCount && WIRE) {
        WIRE->setOutput(cmd.i1, cmd.b1);
        if (indicator) indicator->setLED(cmd.i1, cmd.b1);
      } else {
        ok = false;
      }
      break;

    case DevCmdType::REQUEST_RESET:
      if (WIRE) WIRE->disableAll();
      if (indicator) indicator->clearAll();
      if (relayControl) relayControl->turnOff();
      setState(DeviceState::Shutdown);
      CONF->PutBool(RESET_FLAG, true);
      CONF->RestartSysDelayDown(3000);
      break;

    default:
      ok = false;
      break;
  }

  sendAck(ok);
}
