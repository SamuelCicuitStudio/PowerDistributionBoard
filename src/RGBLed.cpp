#include "RGBLed.h"

// -------- Singleton backing pointer --------
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

// ---------- Debug helpers ----------
#ifndef DEBUG_PRINT
#define DEBUG_PRINT(x)   do{}while(0)
#define DEBUG_PRINTLN(x) do{}while(0)
#endif

static const char* patternName(Pattern p) {
  switch (p) {
    case Pattern::OFF:         return "OFF";
    case Pattern::SOLID:       return "SOLID";
    case Pattern::BLINK:       return "BLINK";
    case Pattern::BREATHE:     return "BREATHE";
    case Pattern::RAINBOW:     return "RAINBOW";
    case Pattern::HEARTBEAT2:  return "HEARTBEAT2";
    case Pattern::FLASH_ONCE:  return "FLASH_ONCE";
    default:                   return "?";
  }
}

// ---------------- Pin attach ----------------
void RGBLed::attachPins(int pinR, int pinG, int pinB, bool activeLow) {
  _pinR = pinR; _pinG = pinG; _pinB = pinB; _activeLow = activeLow;
}

// ---------------- Lifecycle ----------------
bool RGBLed::begin() {
  if (_pinR < 0 || _pinG < 0) return false;
  pinMode(_pinR, OUTPUT);
  pinMode(_pinG, OUTPUT);
  if (_pinB >= 0) pinMode(_pinB, OUTPUT);

  writeColor(0,0,0);

  _mtx = xSemaphoreCreateMutex();
  _queue = xQueueCreate(RGB_CMD_QUEUE_LEN, sizeof(Cmd));
  if (!_mtx || !_queue) return false;

  if (xTaskCreate(&RGBLed::taskThunk, "RGBLed", RGB_TASK_STACK, this,
                  RGB_TASK_PRIORITY, &_task) != pdPASS) return false;

  // default background at startup -> START
  setDeviceState(DevState::START);
  return true;
}

void RGBLed::end() {
  if (!_queue) return;
  Cmd c{}; c.type = CmdType::SHUTDOWN;
  sendCmd(c, portMAX_DELAY);
}

// ---------------- Public API: background state ----------------
void RGBLed::setDeviceState(DevState s) {
  Cmd c{}; c.type = CmdType::SET_BACKGROUND; c.bgState = s;
  sendCmd(c, 0);
}

// ---------------- Public API: overlay events ----------------
void RGBLed::postOverlay(OverlayEvent e) {
  PatternOpts o{};
  Cmd c{}; c.type = CmdType::PLAY;

  switch (e) {
    // General
    case OverlayEvent::WAKE_FLASH:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_WAKE_FLASH);
      o.onMs = 180; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 220;
      break;

    case OverlayEvent::NET_RECOVER:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_NET_RECOVER);
      o.onMs = 160; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 200;
      break;

    case OverlayEvent::RESET_TRIGGER:
      c.pattern = Pattern::BLINK;
      o.color = rgOnly(RGB_OVR_RESET_TRIGGER);
      o.periodMs = 180; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 600;
      break;

    case OverlayEvent::LOW_BATT:
      c.pattern = Pattern::BLINK;
      o.color = rgOnly(RGB_OVR_LOW_BATT);
      o.periodMs = 900; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 4000;
      break;

    case OverlayEvent::CRITICAL_BATT:
      c.pattern = Pattern::BLINK; // short red strobe burst
      o.color = rgOnly(RGB_OVR_CRITICAL_BATT);
      o.periodMs = 160; o.priority = PRIO_CRITICAL; o.preempt = true; o.durationMs = 800;
      break;

    // Wi-Fi / Web
    case OverlayEvent::WIFI_STATION:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_WIFI_STA);
      o.onMs = 140; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 180;
      break;

    case OverlayEvent::WIFI_AP_:
      c.pattern = Pattern::HEARTBEAT2;
      o.color = rgOnly(RGB_OVR_WIFI_AP);
      o.periodMs = 1500; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 3000;
      break;

    case OverlayEvent::WIFI_LOST:
      c.pattern = Pattern::BLINK;
      o.color = rgOnly(RGB_OVR_WIFI_LOST);
      o.periodMs = 250; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 800;
      break;

    case OverlayEvent::WEB_ADMIN_ACTIVE:
      c.pattern = Pattern::BREATHE;
      o.color = rgOnly(RGB_OVR_WEB_ADMIN);
      o.periodMs = 900; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 2500;
      break;

    case OverlayEvent::WEB_USER_ACTIVE:
      c.pattern = Pattern::BREATHE;
      o.color = rgOnly(RGB_OVR_WEB_USER);
      o.periodMs = 900; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 2500;
      break;

    // Fan / Relay
    case OverlayEvent::FAN_ON:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_FAN_ON);
      o.onMs = 120; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 160;
      break;

    case OverlayEvent::FAN_OFF:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_FAN_OFF);
      o.onMs = 120; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 160;
      break;

    case OverlayEvent::RELAY_ON:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_RELAY_ON);
      o.onMs = 140; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 180;
      break;

    case OverlayEvent::RELAY_OFF:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_RELAY_OFF);
      o.onMs = 140; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 180;
      break;

    // Temperature / Current
    case OverlayEvent::TEMP_WARN:
      c.pattern = Pattern::BLINK;
      o.color = rgOnly(RGB_OVR_TEMP_WARN);
      o.periodMs = 600; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 2400;
      break;

    case OverlayEvent::TEMP_CRIT:
      c.pattern = Pattern::BLINK; // rapid red burst
      o.color = rgOnly(RGB_OVR_TEMP_CRIT);
      o.periodMs = 160; o.priority = PRIO_CRITICAL; o.preempt = true; o.durationMs = 600;
      break;

    case OverlayEvent::CURR_WARN:
      c.pattern = Pattern::BLINK;
      o.color = rgOnly(RGB_OVR_CURR_WARN);
      o.periodMs = 400; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 1600;
      break;

    case OverlayEvent::CURR_TRIP:
      c.pattern = Pattern::BLINK; // rapid red burst
      o.color = rgOnly(RGB_OVR_CURR_TRIP);
      o.periodMs = 160; o.priority = PRIO_CRITICAL; o.preempt = true; o.durationMs = 600;
      break;

    // Generic output toggles
    case OverlayEvent::OUTPUT_TOGGLED_ON:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_OUTPUT_ON);
      o.onMs = 120; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 150;
      break;

    case OverlayEvent::OUTPUT_TOGGLED_OFF:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_OUTPUT_OFF);
      o.onMs = 120; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 150;
      break;

    // ---- Power-up sequence overlays ----
    case OverlayEvent::PWR_WAIT_12V:
      c.pattern = Pattern::BREATHE;
      o.color = rgOnly(RGB_OVR_PWR_WAIT_12V);
      o.periodMs = 1200; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 2000;
      break;

    case OverlayEvent::PWR_CHARGING:
      c.pattern = Pattern::BREATHE;
      o.color = rgOnly(RGB_OVR_PWR_CHARGING);
      o.periodMs = 800; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 1000; // shorter; post ≤1/s from caller
      break;

    case OverlayEvent::PWR_THRESH_OK:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_PWR_THRESH_OK);
      o.onMs = 180; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 220;
      break;

    case OverlayEvent::PWR_BYPASS_ON:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_PWR_BYPASS_ON);
      o.onMs = 160; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 200;
      break;

    case OverlayEvent::PWR_WAIT_BUTTON:
      c.pattern = Pattern::HEARTBEAT2;
      o.color = rgOnly(RGB_OVR_PWR_WAIT_BUTTON);
      o.periodMs = 1400; o.priority = PRIO_ACTION; o.preempt = true; o.durationMs = 3500;
      break;

    case OverlayEvent::PWR_START:
      c.pattern = Pattern::FLASH_ONCE;
      o.color = rgOnly(RGB_OVR_PWR_START);
      o.onMs = 200; o.priority = PRIO_ALERT; o.preempt = true; o.durationMs = 240;
      break;
  }

  c.opts = o;
  sendCmd(c, 0);
}

// Output index signaling: blink "channelIndex" times in chosen color
void RGBLed::postOutputEvent(uint8_t channelIndex, bool on, uint8_t priority) {
  if (!channelIndex) return;
  const uint32_t col = rgOnly(on ? RGB_OVR_OUTPUT_ON : RGB_OVR_OUTPUT_OFF);

  // Encode as short grouped pulses: (count) x [ON 120ms, OFF 120ms], pause 350ms
  // Repeat the group twice for visibility if priority >= ALERT
  uint8_t groups = (priority >= PRIO_ALERT) ? 2 : 1;
  for (uint8_t g = 0; g < groups; ++g) {
    for (uint8_t i = 0; i < channelIndex; ++i) {
      flash(col, 120, priority, true);
      vTaskDelay(pdMS_TO_TICKS(120));
    }
    vTaskDelay(pdMS_TO_TICKS(350));
  }
}

// ---------------- Public helpers ----------------
void RGBLed::off(uint8_t priority, bool preempt) {
  PatternOpts o{}; o.priority = priority; o.preempt = preempt;
  playPattern(Pattern::OFF, o);
}

void RGBLed::solid(uint32_t color, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{}; o.color = rgOnly(color); o.priority = priority; o.preempt = preempt; o.durationMs = durationMs;
  playPattern(Pattern::SOLID, o);
}

void RGBLed::blink(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{}; o.color = rgOnly(color); o.periodMs = periodMs; o.priority = priority; o.preempt = preempt; o.durationMs = durationMs;
  playPattern(Pattern::BLINK, o);
}

void RGBLed::breathe(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{}; o.color = rgOnly(color); o.periodMs = periodMs; o.priority = priority; o.preempt = preempt; o.durationMs = durationMs;
  playPattern(Pattern::BREATHE, o);
}

void RGBLed::rainbow(uint16_t stepMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{}; o.periodMs = stepMs; o.priority = priority; o.preempt = preempt; o.durationMs = durationMs;
  playPattern(Pattern::RAINBOW, o);
}

void RGBLed::heartbeat(uint32_t color, uint16_t periodMs, uint8_t priority, bool preempt, uint32_t durationMs) {
  PatternOpts o{}; o.color = rgOnly(color); o.periodMs = periodMs; o.priority = priority; o.preempt = preempt; o.durationMs = durationMs;
  playPattern(Pattern::HEARTBEAT2, o);
}

void RGBLed::flash(uint32_t color, uint16_t onMs, uint8_t priority, bool preempt) {
  PatternOpts o{}; o.color = rgOnly(color); o.onMs = onMs; o.priority = priority; o.preempt = preempt; o.durationMs = onMs + 20;
  playPattern(Pattern::FLASH_ONCE, o);
}

void RGBLed::playPattern(Pattern pat, const PatternOpts& opts) {
  Cmd c{}; c.type = CmdType::PLAY; c.pattern = pat; c.opts = opts;
  sendCmd(c, 0);
}

// ---------------- Worker task ----------------
void RGBLed::taskThunk(void* arg) {
  static_cast<RGBLed*>(arg)->taskLoop();
}

bool RGBLed::sendCmd(const Cmd& c, TickType_t to) {
  if (!_queue) return false;
  if (xQueueSend(_queue, &c, to) == pdTRUE) return true;
  if (c.type == CmdType::PLAY && c.opts.priority >= PRIO_ALERT) {
    Cmd dump{}; xQueueReceive(_queue, &dump, 0);
    return xQueueSend(_queue, &c, to) == pdTRUE;
  }
  return false;
}

void RGBLed::taskLoop() {
  TickType_t last = xTaskGetTickCount();
  bool running = true;

  while (running) {
    Cmd cmd{};
    if (_haveCurrent) {
      if (xQueueReceive(_queue, &cmd, 0) == pdTRUE) {
        if (cmd.type == CmdType::SHUTDOWN) { running = false; break; }
        else if (cmd.type == CmdType::SET_BACKGROUND) {
          _bgState = cmd.bgState;
        } else if (cmd.type == CmdType::PLAY) {
          if (cmd.opts.preempt && cmd.opts.priority >= _currentPrio) {
            _currentPat   = cmd.pattern;
            _currentOpts  = cmd.opts;
            _currentPrio  = cmd.opts.priority;
            _haveCurrent  = true;
            last = xTaskGetTickCount();
          }
        } else if (cmd.type == CmdType::STOP) {
          _haveCurrent = false;
          writeColor(0,0,0);
        }
      }

      // step current pattern
      switch (_currentPat) {
        case Pattern::OFF:
          writeColor(0,0,0);
          vTaskDelay(pdMS_TO_TICKS(15));
          break;
        case Pattern::SOLID:
          writeColor(RGB_R(_currentOpts.color), RGB_G(_currentOpts.color), RGB_B(_currentOpts.color));
          vTaskDelay(pdMS_TO_TICKS(25));
          break;
        case Pattern::BLINK:
          stepBlink(_currentOpts.color, _currentOpts.periodMs);
          break;
        case Pattern::BREATHE:
          stepBreathe(_currentOpts.color, _currentOpts.periodMs);
          break;
        case Pattern::RAINBOW:
          stepRainbow(_currentOpts.periodMs ? _currentOpts.periodMs : 20);
          break;
        case Pattern::HEARTBEAT2:
          doHeartbeat2(_currentOpts.color, _currentOpts.periodMs ? _currentOpts.periodMs : 1400);
          break;
        case Pattern::FLASH_ONCE:
          doFlashOnce(_currentOpts.color, _currentOpts.onMs ? _currentOpts.onMs : 120);
          _haveCurrent = false;
          break;
      }

      // duration expiry?
      if (_haveCurrent && _currentOpts.durationMs > 0) {
        uint32_t elapsedMs = (xTaskGetTickCount() - last) * portTICK_PERIOD_MS;
        if (elapsedMs >= _currentOpts.durationMs) _haveCurrent = false;
      }

      if (!_haveCurrent) applyBackground(_bgState);

    } else {
      // idle: maintain background, but remain responsive
      applyBackground(_bgState);
      if (xQueueReceive(_queue, &cmd, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (cmd.type == CmdType::SHUTDOWN) { running = false; break; }
        else if (cmd.type == CmdType::SET_BACKGROUND) {
          _bgState = cmd.bgState;
        } else if (cmd.type == CmdType::PLAY) {
          _currentPat   = cmd.pattern;
          _currentOpts  = cmd.opts;
          _currentPrio  = cmd.opts.priority;
          _haveCurrent  = true;
          last = xTaskGetTickCount();
        }
      }
    }
  }

  // shutdown
  writeColor(0,0,0);
  vTaskDelete(nullptr);
}

// ---------------- Background mapping ----------------
void RGBLed::applyBackground(DevState s) {
  switch (s) {
    // legacy compatibility
    case DevState::BOOT:
    case DevState::INIT:
      writeColor(RGB_R(RG_WHT_DARK), RGB_G(RG_WHT_DARK), 0);
      vTaskDelay(pdMS_TO_TICKS(20));
      break;

    case DevState::PAIRING:
      // amber heartbeat instead of rainbow (RG only)
      doHeartbeat2(RG_AMB, 1500);
      break;

    case DevState::READY_ONLINE:
      doHeartbeat2(RGB_HEX(0,180,0), 1500);
      break;

    case DevState::READY_OFFLINE:
      stepBlink(RG_AMB, 1000);
      break;

    case DevState::SLEEP:
      writeColor(0,0,0);
      vTaskDelay(pdMS_TO_TICKS(60));
      break;

    // Power Manager set
    case DevState::START:
      doHeartbeat2(RGB_BG_START_COLOR, 900);   // quick double pulse
      break;

    case DevState::IDLE:
      doHeartbeat2(RGB_BG_IDLE_COLOR, 2000);   // slow soft pulse (was 1600)
      break;

    case DevState::RUN:
      doHeartbeat2(RGB_BG_RUN_COLOR, 1400);    // brighter double heartbeat (was 1200)
      break;

    case DevState::OFF:
      writeColor(0,0,0);
      vTaskDelay(pdMS_TO_TICKS(60));
      break;

    case DevState::FAULT:
      // very fast red strobe (~8 Hz): 50 ms on / 75 ms off
      doStrobe(RGB_BG_FAULT_COLOR, RGB_FAULT_STROBE_ON_MS, RGB_FAULT_STROBE_OFF_MS);
      break;

    case DevState::MAINT:
      stepBlink(RGB_BG_MAINT_COLOR, 900);
      break;

    case DevState::WAIT:
      // Smooth, visible “getting ready” cue
      stepBreathe(RGB_BG_WAIT_COLOR, 1200);
      break;
  }
}

// ---------------- Pattern primitives ----------------
void RGBLed::writeColor(uint8_t r, uint8_t g, uint8_t b) {
#if RGB_FORCE_RG_ONLY
  b = 0;
#endif
  if (_activeLow) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(_pinR, r);
  analogWrite(_pinG, g);
  if (_pinB >= 0) analogWrite(_pinB, b);
}

void RGBLed::stepRainbow(uint16_t stepMs) {
#if RGB_FORCE_RG_ONLY
  static uint8_t a = 0; static int8_t dir = 8;
  uint8_t g = a, r = 255 - a;  // sweep between red <-> green
  writeColor(r, g, 0);
  a = (uint8_t)std::min(255, std::max(0, a + dir));
  if (a == 0 || a == 255) dir = -dir;
  vTaskDelay(pdMS_TO_TICKS(stepMs));
#else
  // HSV rainbow (unused in RG-only)
  static float hue = 0.0f;
  const float stepDeg = RGB_RAINBOW_STEP_DEG;
  if (hue >= 360.0f) hue -= 360.0f;
  // ... (omitted for brevity)
  vTaskDelay(pdMS_TO_TICKS(stepMs));
#endif
}

void RGBLed::stepBlink(uint32_t color, uint16_t periodMs) {
  uint32_t c = rgOnly(color);
  uint16_t half = periodMs / 2;
  writeColor(RGB_R(c), RGB_G(c), 0);
  vTaskDelay(pdMS_TO_TICKS(half));
  writeColor(0,0,0);
  vTaskDelay(pdMS_TO_TICKS(half));
}

void RGBLed::stepBreathe(uint32_t color, uint16_t periodMs) {
  static int16_t a = 0, dir = 1;
  uint32_t c = rgOnly(color);
  uint8_t r = RGB_R(c), g = RGB_G(c);
  uint8_t rr = (uint8_t)((r * a) / 255);
  uint8_t gg = (uint8_t)((g * a) / 255);
  writeColor(rr, gg, 0);

  int step = 255 / 25; // ~50 steps per cycle
  a += dir * step;
  if (a >= 255) { a = 255; dir = -1; }
  if (a <= 0)   { a = 0;   dir =  1; }
  vTaskDelay(pdMS_TO_TICKS(periodMs / 50));
}

void RGBLed::doHeartbeat2(uint32_t color, uint16_t periodMs) {
  uint32_t c = rgOnly(color);
  uint16_t beat = periodMs / 8;
  uint16_t gap  = periodMs / 8;
  uint16_t rest = periodMs - (beat*2 + gap);

  writeColor(RGB_R(c), RGB_G(c), 0);
  vTaskDelay(pdMS_TO_TICKS(beat));
  writeColor(0,0,0);
  vTaskDelay(pdMS_TO_TICKS(gap));
  writeColor(RGB_R(c), RGB_G(c), 0);
  vTaskDelay(pdMS_TO_TICKS(beat));
  writeColor(0,0,0);
  vTaskDelay(pdMS_TO_TICKS(rest));
}

void RGBLed::doFlashOnce(uint32_t color, uint16_t onMs) {
  uint32_t c = rgOnly(color);
  writeColor(RGB_R(c), RGB_G(c), 0);
  vTaskDelay(pdMS_TO_TICKS(onMs));
  writeColor(0,0,0);
}

// NEW: asymmetric strobe (used for FAULT background)
void RGBLed::doStrobe(uint32_t color, uint16_t onMs, uint16_t offMs) {
  uint32_t c = rgOnly(color);
  writeColor(RGB_R(c), RGB_G(c), 0);
  vTaskDelay(pdMS_TO_TICKS(onMs));
  writeColor(0,0,0);
  vTaskDelay(pdMS_TO_TICKS(offMs));
}
