#include "RGBLed.h"

RGBLed* RGBLed::s_instance = nullptr;

void RGBLed::Init(int pinR, int pinG, int pinB, bool activeLow) {
  if (!s_instance) {
    s_instance = new RGBLed(pinR, pinG, pinB, activeLow);
  } else {
    s_instance->attachPins(pinR, pinG, pinB, activeLow);
  }
}

RGBLed* RGBLed::Get() {
  if (!s_instance) s_instance = new RGBLed();
  return s_instance;
}

RGBLed* RGBLed::TryGet() { return s_instance; }

bool RGBLed::begin() {
  if (_pinR < 0 || _pinG < 0 || _pinB < 0) return false;

  pinMode(_pinR, OUTPUT);
  pinMode(_pinG, OUTPUT);
  pinMode(_pinB, OUTPUT);

  if (ledcSetup(RGB_R_PWM_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION) == 0) return false;
  if (ledcSetup(RGB_G_PWM_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION) == 0) return false;
  if (ledcSetup(RGB_B_PWM_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION) == 0) return false;

  ledcAttachPin(_pinR, RGB_R_PWM_CHANNEL);
  ledcAttachPin(_pinG, RGB_G_PWM_CHANNEL);
  ledcAttachPin(_pinB, RGB_B_PWM_CHANNEL);

  writeColor(0, 0, 0);

  _queue = xQueueCreate(RGB_CMD_QUEUE_LEN, sizeof(Cmd));
  if (!_queue) return false;

  if (xTaskCreate(&RGBLed::taskThunk, "RGBLed", RGB_TASK_STACK, this,
                  RGB_TASK_PRIORITY, &_task) != pdPASS) return false;

  setDeviceState(DevState::START);
  return true;
}

void RGBLed::end() {
  if (!_queue) return;
  Cmd c{};
  c.type = CmdType::SHUTDOWN;
  sendCmd(c, portMAX_DELAY);
}

void RGBLed::attachPins(int pinR, int pinG, int pinB, bool activeLow) {
  _pinR = pinR;
  _pinG = pinG;
  _pinB = pinB;
  _activeLow = activeLow;
}

void RGBLed::setDeviceState(DevState s) {
  Cmd c{};
  c.type    = CmdType::SET_BACKGROUND;
  c.bgState = s;
  sendCmd(c, 0);
}

void RGBLed::off(uint8_t priority, bool preempt) {
  PatternOpts o{};
  o.color    = RGB_OFF;
  o.priority = priority;
  o.preempt  = preempt;
  playPattern(Pattern::OFF, o);
}

void RGBLed::solid(uint32_t color, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = 500;
  o.onMs       = 500;
  o.durationMs = durationMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::SOLID, o);
}

void RGBLed::blink(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = periodMs;
  o.onMs       = periodMs / 2;
  o.durationMs = durationMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::BLINK, o);
}

void RGBLed::breathe(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = periodMs;
  o.onMs       = 0;
  o.durationMs = durationMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::BREATHE, o);
}

void RGBLed::heartbeat(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = periodMs;
  o.onMs       = 90;
  o.durationMs = durationMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::HEARTBEAT2, o);
}

void RGBLed::flash(uint32_t color, uint16_t onMs, uint8_t priority, bool preempt) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = onMs * 2;
  o.onMs       = onMs;
  o.durationMs = o.periodMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::FLASH_ONCE, o);
}

void RGBLed::strobe(uint32_t color, uint16_t onMs, uint16_t offMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{};
  o.color      = color;
  o.periodMs   = onMs + offMs;
  o.onMs       = onMs;
  o.durationMs = durationMs;
  o.priority   = priority;
  o.preempt    = preempt;
  playPattern(Pattern::STROBE, o);
}

void RGBLed::playPattern(Pattern pat, const PatternOpts& opts) {
  Cmd c{};
  c.type    = CmdType::PLAY;
  c.pattern = pat;
  c.opts    = opts;
  sendCmd(c, 0);
}

void RGBLed::postOverlay(OverlayEvent e) {
  PatternOpts o{};
  Pattern pat = Pattern::FLASH_ONCE;

  switch (e) {
    // General
    case OverlayEvent::WAKE_FLASH:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WAKE_FLASH;
      o.onMs = 160; o.periodMs = 220; o.durationMs = 220; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::NET_RECOVER:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_NET_RECOVER;
      o.onMs = 140; o.periodMs = 200; o.durationMs = 220; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::RESET_TRIGGER:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_RESET_TRIGGER;
      o.onMs = 140; o.periodMs = 220; o.durationMs = 300; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::LOW_BATT:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_LOW_BATT;
      o.periodMs = 900; o.onMs = 300; o.durationMs = 0; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::CRITICAL_BATT:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_CRITICAL_BATT;
      o.onMs = 70; o.periodMs = 140; o.durationMs = 800; o.priority = PRIO_CRITICAL;
      break;

    // Wi-Fi + Web roles
    case OverlayEvent::WIFI_STATION:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WIFI_STA;
      o.onMs = 160; o.periodMs = 200; o.durationMs = 220; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::WIFI_AP_:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WIFI_AP;
      o.onMs = 160; o.periodMs = 200; o.durationMs = 220; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::WIFI_LOST:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WIFI_LOST;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::WEB_ADMIN_ACTIVE:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WEB_ADMIN;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::WEB_USER_ACTIVE:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_WEB_USER;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ACTION;
      break;

    // Fan / Relay
    case OverlayEvent::FAN_ON:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_FAN_ON;
      o.onMs = 160; o.periodMs = 220; o.durationMs = 260; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::FAN_OFF:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_FAN_OFF;
      o.onMs = 160; o.periodMs = 220; o.durationMs = 260; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::RELAY_ON:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_RELAY_ON;
      o.onMs = 160; o.periodMs = 220; o.durationMs = 260; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::RELAY_OFF:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_RELAY_OFF;
      o.onMs = 160; o.periodMs = 220; o.durationMs = 260; o.priority = PRIO_ACTION;
      break;

    // Temperature / Current
    case OverlayEvent::TEMP_WARN:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_TEMP_WARN;
      o.periodMs = 700; o.onMs = 250; o.durationMs = 1400; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::TEMP_CRIT:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_TEMP_CRIT;
      o.onMs = 70; o.periodMs = 140; o.durationMs = 1200; o.priority = PRIO_CRITICAL;
      break;
    case OverlayEvent::CURR_WARN:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_CURR_WARN;
      o.periodMs = 700; o.onMs = 250; o.durationMs = 1400; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::CURR_TRIP:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_CURR_TRIP;
      o.onMs = 70; o.periodMs = 140; o.durationMs = 1000; o.priority = PRIO_CRITICAL;
      break;

    // Output feedback (generic handling uses indexed helper)
    case OverlayEvent::OUTPUT_TOGGLED_ON:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_OUTPUT_ON;
      o.onMs = 120; o.periodMs = 200; o.durationMs = 200; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::OUTPUT_TOGGLED_OFF:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_OUTPUT_OFF;
      o.onMs = 120; o.periodMs = 200; o.durationMs = 200; o.priority = PRIO_ACTION;
      break;

    // Power-up sequence
    case OverlayEvent::PWR_WAIT_12V:
      pat = Pattern::BREATHE;
      o.color = RGB_OVR_PWR_WAIT_12V;
      o.periodMs = 1600; o.durationMs = 0; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::PWR_CHARGING:
      pat = Pattern::BREATHE;
      o.color = RGB_OVR_PWR_CHARGING;
      o.periodMs = 1400; o.durationMs = 0; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::PWR_THRESH_OK:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_PWR_THRESH_OK;
      o.onMs = 180; o.periodMs = 240; o.durationMs = 320; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::PWR_BYPASS_ON:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_PWR_BYPASS_ON;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::PWR_WAIT_BUTTON:
      pat = Pattern::HEARTBEAT2;
      o.color = RGB_OVR_PWR_WAIT_BUTTON;
      o.periodMs = 1400; o.onMs = 120; o.durationMs = 0; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::PWR_START:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_PWR_START;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ALERT;
      break;

    // Power & protection detail
    case OverlayEvent::PWR_12V_LOST:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_12V_LOST;
      o.onMs = 80; o.periodMs = 160; o.durationMs = 1200; o.priority = PRIO_CRITICAL;
      break;
    case OverlayEvent::PWR_DC_LOW:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_DC_LOW;
      o.periodMs = 800; o.onMs = 300; o.durationMs = 1600; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::FAULT_OVERCURRENT:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_OVERCURRENT;
      o.onMs = 70; o.periodMs = 140; o.durationMs = 1200; o.priority = PRIO_CRITICAL;
      break;
    case OverlayEvent::FAULT_THERMAL_GLOBAL:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_THERMAL_GLOBAL;
      o.onMs = 90; o.periodMs = 160; o.durationMs = 1400; o.priority = PRIO_CRITICAL;
      break;
    case OverlayEvent::FAULT_THERMAL_CH_LOCK:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_THERMAL_CH_LOCK;
      o.periodMs = 700; o.onMs = 250; o.durationMs = 1600; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::FAULT_SENSOR_MISSING:
      pat = Pattern::BLINK;
      o.color = RGB_OVR_SENSOR_MISSING;
      o.periodMs = 800; o.onMs = 280; o.durationMs = 2000; o.priority = PRIO_ALERT;
      break;
    case OverlayEvent::FAULT_CFG_ERROR:
      pat = Pattern::STROBE;
      o.color = RGB_OVR_CFG_ERROR;
      o.onMs = 90; o.periodMs = 170; o.durationMs = 1400; o.priority = PRIO_CRITICAL;
      break;
    case OverlayEvent::DISCHG_ACTIVE:
      pat = Pattern::BREATHE;
      o.color = RGB_OVR_DISCHG_ACTIVE;
      o.periodMs = 1200; o.durationMs = 0; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::DISCHG_DONE:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_DISCHG_DONE;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ACTION;
      break;
    case OverlayEvent::BYPASS_FORCED_OFF:
      pat = Pattern::FLASH_ONCE;
      o.color = RGB_OVR_BYPASS_FORCED_OFF;
      o.onMs = 200; o.periodMs = 260; o.durationMs = 320; o.priority = PRIO_ALERT;
      break;
  }

  playPattern(pat, o);
}

void RGBLed::postOutputEvent(uint8_t /*channelIndex*/, bool on, uint8_t priority) {
  PatternOpts o{};
  o.color    = on ? RGB_OVR_OUTPUT_ON : RGB_OVR_OUTPUT_OFF;
  o.onMs     = 120;
  o.periodMs = 200;
  o.durationMs = 200;
  o.priority = priority;
  playPattern(Pattern::FLASH_ONCE, o);
}

bool RGBLed::sendCmd(const Cmd& c, TickType_t to) {
  if (!_queue) return false;
  if (xQueueSend(_queue, &c, to) == pdTRUE) return true;
  Cmd dump{};
  xQueueReceive(_queue, &dump, 0);      // drop oldest
  return xQueueSend(_queue, &c, to) == pdTRUE;
}

void RGBLed::applyBackground() {
  applyBackground(_bgState);
}

void RGBLed::applyBackground(DevState s) {
  _bgState = s;

  if (_haveCurrent && _currentPrio > PRIO_BACKGROUND) {
    // Overlay running; keep it.
    return;
  }

  PatternOpts o{};
  Pattern pat = Pattern::OFF;
  o.priority = PRIO_BACKGROUND;
  o.preempt  = true;

  switch (s) {
    case DevState::BOOT:
    case DevState::INIT:
    case DevState::PAIRING:
      pat = Pattern::BREATHE;
      o.color = RGB_BG_BOOT_COLOR;
      o.periodMs = 1400;
      break;
    case DevState::READY_ONLINE:
    case DevState::READY_OFFLINE:
    case DevState::IDLE:
      pat = Pattern::HEARTBEAT2;
      o.color = RGB_BG_IDLE_COLOR;
      o.periodMs = 1400;
      o.onMs = 120;
      break;
    case DevState::START:
      pat = Pattern::HEARTBEAT2;
      o.color = RGB_BG_START_COLOR;
      o.periodMs = 900;
      o.onMs = 120;
      break;
    case DevState::RUN:
      pat = Pattern::HEARTBEAT2;
      o.color = RGB_BG_RUN_COLOR;
      o.periodMs = 900;
      o.onMs = 140;
      break;
    case DevState::WAIT:
      pat = Pattern::HEARTBEAT2;
      o.color = RGB_BG_WAIT_COLOR;
      o.periodMs = 1500;
      o.onMs = 120;
      break;
    case DevState::MAINT:
      pat = Pattern::BREATHE;
      o.color = RGB_BG_MAINT_COLOR;
      o.periodMs = 1800;
      break;
    case DevState::SLEEP:
    case DevState::OFF:
      pat = Pattern::OFF;
      o.color = RGB_BG_OFF_COLOR;
      break;
    case DevState::FAULT:
      pat = Pattern::STROBE;
      o.color = RGB_BG_FAULT_COLOR;
      o.onMs = RGB_FAULT_STROBE_ON_MS;
      o.periodMs = RGB_FAULT_STROBE_ON_MS + RGB_FAULT_STROBE_OFF_MS;
      o.priority = PRIO_BACKGROUND;
      break;
  }

  setActivePattern(pat, o);
}

void RGBLed::setActivePattern(Pattern pat, const PatternOpts& opts) {
  _currentPat     = pat;
  _currentOpts    = opts;
  _currentPrio    = opts.priority;
  _currentStartMs = millis();
  _haveCurrent    = true;
}

void RGBLed::taskThunk(void* arg) {
  static_cast<RGBLed*>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void RGBLed::taskLoop() {
  bool running = true;

  while (running) {
    Cmd c{};
    if (_queue && xQueueReceive(_queue, &c, pdMS_TO_TICKS(10)) == pdTRUE) {
      switch (c.type) {
        case CmdType::SET_BACKGROUND:
          applyBackground(c.bgState);
          break;
        case CmdType::PLAY: {
          bool expired = (_haveCurrent && _currentOpts.durationMs > 0 &&
                          (millis() - _currentStartMs) >= _currentOpts.durationMs);
          bool higherPriority = c.opts.priority > _currentPrio;
          bool equalAndPreempt = (c.opts.priority == _currentPrio) && c.opts.preempt;
          bool accept = !_haveCurrent || expired || higherPriority || equalAndPreempt;
          if (accept) setActivePattern(c.pattern, c.opts);
          break;
        }
        case CmdType::STOP:
          _haveCurrent = false;
          applyBackground();
          break;
        case CmdType::SHUTDOWN:
          running = false;
          break;
      }
    }

    if (!running) break;

    // Expire overlays with finite duration
    if (_haveCurrent && _currentOpts.durationMs > 0) {
      uint32_t elapsed = millis() - _currentStartMs;
      if (elapsed >= _currentOpts.durationMs) {
        _haveCurrent = false;
        applyBackground();
        continue;
      }
    }

    if (!_haveCurrent) {
      applyBackground();
      continue;
    }

    switch (_currentPat) {
      case Pattern::OFF:
        writeColor(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(25));
        break;
      case Pattern::SOLID:
        writeColor(RGB_R(_currentOpts.color), RGB_G(_currentOpts.color), RGB_B(_currentOpts.color));
        vTaskDelay(pdMS_TO_TICKS(30));
        break;
      case Pattern::BLINK:
        stepBlink(_currentOpts.color, _currentOpts.periodMs);
        break;
      case Pattern::BREATHE:
        stepBreathe(_currentOpts.color, _currentOpts.periodMs);
        break;
      case Pattern::HEARTBEAT2:
        doHeartbeat2(_currentOpts.color, _currentOpts.periodMs);
        break;
      case Pattern::FLASH_ONCE:
        doFlashOnce(_currentOpts.color, _currentOpts.onMs);
        break;
      case Pattern::STROBE:
        doStrobe(_currentOpts.color, _currentOpts.onMs ? _currentOpts.onMs : 60,
                 _currentOpts.periodMs > _currentOpts.onMs
                   ? (_currentOpts.periodMs - _currentOpts.onMs)
                   : 60);
        break;
    }
  }

  writeColor(0, 0, 0);
}

void RGBLed::writeColor(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t maxDuty = (1u << RGB_PWM_RESOLUTION) - 1u;
  auto scale = [&](uint8_t v) -> uint32_t {
    uint32_t duty = (static_cast<uint32_t>(v) * maxDuty) / 255u;
    return _activeLow ? (maxDuty - duty) : duty;
  };

  ledcWrite(RGB_R_PWM_CHANNEL, scale(r));
  ledcWrite(RGB_G_PWM_CHANNEL, scale(g));
  ledcWrite(RGB_B_PWM_CHANNEL, scale(b));
}

void RGBLed::stepBlink(uint32_t color, uint16_t periodMs) {
  uint16_t on  = _currentOpts.onMs ? _currentOpts.onMs : (periodMs / 2);
  if (on > periodMs) on = periodMs;
  uint16_t off = periodMs > on ? (periodMs - on) : 10;

  writeColor(RGB_R(color), RGB_G(color), RGB_B(color));
  vTaskDelay(pdMS_TO_TICKS(on));
  writeColor(0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(off));
}

void RGBLed::stepBreathe(uint32_t color, uint16_t periodMs) {
  static int16_t level = 0;
  static int8_t dir    = 1;

  if (periodMs < 400) periodMs = 400;
  const uint8_t r = RGB_R(color);
  const uint8_t g = RGB_G(color);
  const uint8_t b = RGB_B(color);

  uint8_t rr = (uint8_t)((r * level) / 255);
  uint8_t gg = (uint8_t)((g * level) / 255);
  uint8_t bb = (uint8_t)((b * level) / 255);
  writeColor(rr, gg, bb);

  int step = 255 / 40; // ~40 steps per breathe
  level += dir * step;
  if (level >= 255) { level = 255; dir = -1; }
  if (level <= 0)   { level = 0;   dir =  1; }

  vTaskDelay(pdMS_TO_TICKS(periodMs / 40));
}

void RGBLed::doHeartbeat2(uint32_t color, uint16_t periodMs) {
  uint16_t beat = _currentOpts.onMs ? _currentOpts.onMs : 120;
  uint16_t gap  = beat / 2;
  uint16_t rest = (periodMs > (beat * 2 + gap)) ? (periodMs - (beat * 2 + gap)) : 120;

  writeColor(RGB_R(color), RGB_G(color), RGB_B(color));
  vTaskDelay(pdMS_TO_TICKS(beat));
  writeColor(0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(gap));
  writeColor(RGB_R(color), RGB_G(color), RGB_B(color));
  vTaskDelay(pdMS_TO_TICKS(beat));
  writeColor(0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(rest));
}

void RGBLed::doFlashOnce(uint32_t color, uint16_t onMs) {
  uint16_t rest = (onMs * 2 > 40) ? (onMs) : 40;
  writeColor(RGB_R(color), RGB_G(color), RGB_B(color));
  vTaskDelay(pdMS_TO_TICKS(onMs));
  writeColor(0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(rest));
}

void RGBLed::doStrobe(uint32_t color, uint16_t onMs, uint16_t offMs) {
  if (onMs == 0) onMs = 60;
  if (offMs == 0) offMs = 60;

  writeColor(RGB_R(color), RGB_G(color), RGB_B(color));
  vTaskDelay(pdMS_TO_TICKS(onMs));
  writeColor(0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(offMs));
}
