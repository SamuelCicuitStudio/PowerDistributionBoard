#include <Buzzer.hpp>
#include <NVSManager.hpp>   // for CONF
#include <Utils.hpp>
namespace {
constexpr uint32_t kAlertWarnRepeatMs = 10000;
constexpr uint32_t kAlertCritRepeatMs = 4000;
constexpr uint32_t kAlertPollMs = 200;
}

// ===== Singleton backing =====
Buzzer* Buzzer::s_inst = nullptr;

void Buzzer::Init(int pin, bool activeLow) {
  if (!s_inst) s_inst = new Buzzer();

  // 1) Start from compile-time defaults (used only if NVS empty)
  s_inst->_activeLow = activeLow;
  s_inst->_muted     = false;

  // 2) Load persisted state (if present). This may override _activeLow/_muted.
  s_inst->loadFromPrefs_();

  // 3) Resolve pin:
  //    - Prefer BUZZER_PIN when defined.
  //    - Otherwise use the provided pin argument.
#ifdef BUZZER_PIN
  s_inst->_pin = BUZZER_PIN;
#else
  if (pin >= 0) {
    s_inst->_pin = pin;
  }
#endif

  // 4) Configure GPIO according to the resolved pin and polarity.
  if (s_inst->_pin >= 0) {
    pinMode(s_inst->_pin, OUTPUT);
    s_inst->idleOff();   // idle state, no sound; honored even if muted.
  }

  // IMPORTANT:
  // - We DO NOT call storeToPrefs_() here.
  //   Calling it would overwrite BUZMUT_KEY with the default on every boot,
  //   destroying the previously saved mute state.
}

Buzzer* Buzzer::Get() {
  if (!s_inst) s_inst = new Buzzer();
  return s_inst;
}

Buzzer* Buzzer::TryGet() { return s_inst; }

// ===== Lifecycle =====
bool Buzzer::begin() {
  // Load polarity/mute from CONF and resolve pin from BUZZER_PIN again,
  // to be 100% sure we're honoring persisted settings.
  loadFromPrefs_();

#ifdef BUZZER_PIN
  _pin = BUZZER_PIN;
#endif

  if (_pin >= 0) {
    pinMode(_pin, OUTPUT);
    idleOff();

    // Reserve a dedicated LEDC channel for the buzzer so it never
    // collides with fan or RGB PWM channels (see Config.h).
    ledcSetup(BUZZER_PWM_CHANNEL, 4000, 8);   // base setup; freq overridden by ledcWriteTone
    ledcAttachPin(_pin, BUZZER_PWM_CHANNEL);
    ledcWriteTone(BUZZER_PWM_CHANNEL, 0);    // ensure silent
    if (_muted) {
      ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
      ledcDetachPin(_pin);
      digitalWrite(_pin, HIGH);
    }
  }

  if (!_mtx)   _mtx   = xSemaphoreCreateMutex();
  if (!_queue) _queue = xQueueCreate(BUZZER_QUEUE_LEN, sizeof(Mode));
  if (!_mtx || !_queue) return false;

  if (!_task) {
    BaseType_t ok = xTaskCreate(&Buzzer::taskThunk, "BuzzerTask",
                                BUZZER_TASK_STACK, this,
                                BUZZER_TASK_PRIORITY, &_task);
    if (ok != pdPASS) return false;
  }
  DEBUGGSTART();
  DEBUG_PRINTLN("[Buzzer] task and queue ready");
  DEBUGGSTOP();
  return true;
}

void Buzzer::end() {
  if (_task)  { TaskHandle_t t = _task; _task = nullptr; vTaskDelete(t); }
  if (_queue) { vQueueDelete(_queue); _queue = nullptr; }
  if (_mtx)   { vSemaphoreDelete(_mtx); _mtx = nullptr; }
  idleOff();
}

// NOTE: attachPin does NOT persist the pin.
// It only rebinds runtime pin; polarity/mute ARE persisted.
void Buzzer::attachPin(int pin, bool activeLow) {
  _pin       = pin;
  _activeLow = activeLow;

  if (_pin >= 0) {
    pinMode(_pin, OUTPUT);
    idleOff();
  }

  // Persist polarity + current mute state (but NOT pin).
  storeToPrefs_();
}

void Buzzer::setMuted(bool on) {
  if (_muted == on) {
    // No change -> no need to touch NVS or queue.
    return;
  }

  _muted = on;

  if (on) {
    // Stop any current tone immediately and clear pending sounds
    if (_pin >= 0) {
      noTone(_pin);
      ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
      ledcDetachPin(_pin);
      digitalWrite(_pin, HIGH);
    }
    if (_queue) {
      xQueueReset(_queue);   // drop everything pending
    }
  } else {
    if (_pin >= 0) {
      pinMode(_pin, OUTPUT);
      ledcAttachPin(_pin, BUZZER_PWM_CHANNEL);
      ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
      idleOff();
    }
  }

  // Persist new mute flag (this is the ONLY time we change BUZMUT_KEY,
  // aside from explicit polarity changes).
  storeToPrefs_();
}

void Buzzer::setAlert(AlertLevel level) {
  if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
  if (_alertLevel == level) {
    if (_mtx) xSemaphoreGive(_mtx);
    return;
  }

  _alertLevel = level;
  switch (level) {
    case AlertLevel::WARNING:
      _alertRepeatMs = kAlertWarnRepeatMs;
      break;
    case AlertLevel::CRITICAL:
      _alertRepeatMs = kAlertCritRepeatMs;
      break;
    default:
      _alertRepeatMs = 0;
      break;
  }
  _alertNextMs = millis();
  if (_mtx) xSemaphoreGive(_mtx);
}

// ===== Public API (enqueue) =====
void Buzzer::bip()                   { enqueue(Mode::BIP); }
void Buzzer::successSound()          { enqueue(Mode::SUCCESS); }
void Buzzer::failedSound()           { enqueue(Mode::FAILED); }
void Buzzer::bipWiFiConnected()      { enqueue(Mode::WIFI_CONNECTED); }
void Buzzer::bipWiFiOff()            { enqueue(Mode::WIFI_OFF); }
void Buzzer::bipOverTemperature()    { enqueue(Mode::OVER_TEMPERATURE); }
void Buzzer::bipFault()              { enqueue(Mode::FAULT); }
void Buzzer::bipStartupSequence()    { enqueue(Mode::STARTUP); }
void Buzzer::bipSystemReady()        { enqueue(Mode::READY); }
void Buzzer::bipSystemShutdown()     { enqueue(Mode::SHUTDOWN); }
void Buzzer::bipClientConnected()    { enqueue(Mode::CLIENT_CONNECTED); }
void Buzzer::bipClientDisconnected() { enqueue(Mode::CLIENT_DISCONNECTED); }

void Buzzer::enqueue(Mode m) {
  // While muted: do nothing (no queue traffic, no wakeups)
  if (_muted) return;
  if (!_queue) return;

  if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);

  if (xQueueSendToBack(_queue, &m, 0) != pdTRUE) {
    Mode dump;
    xQueueReceive(_queue, &dump, 0);      // drop oldest
    xQueueSendToBack(_queue, &m, 0);      // push newest
  }

  if (_mtx) xSemaphoreGive(_mtx);
}

// ===== Task plumbing =====
void Buzzer::taskThunk(void* arg) {
  static_cast<Buzzer*>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void Buzzer::taskLoop() {
  for (;;) {
    Mode m;
    AlertLevel alertLevel = AlertLevel::NONE;
    uint32_t alertNextMs = 0;
    uint32_t alertRepeatMs = 0;

    if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
    alertLevel = _alertLevel;
    alertNextMs = _alertNextMs;
    alertRepeatMs = _alertRepeatMs;
    if (_mtx) xSemaphoreGive(_mtx);

    TickType_t waitTicks = pdMS_TO_TICKS(kAlertPollMs);
    if (alertLevel != AlertLevel::NONE) {
      uint32_t now = millis();
      uint32_t untilMs = (alertNextMs > now) ? (alertNextMs - now) : 0;
      if (untilMs < kAlertPollMs) waitTicks = pdMS_TO_TICKS(untilMs);
    }

    if (xQueueReceive(_queue, &m, waitTicks) == pdTRUE) {
      playMode(m);
      idleOff();
    }

    if (alertLevel != AlertLevel::NONE) {
      uint32_t now = millis();
      if (now >= alertNextMs) {
        playAlert(alertLevel);
        if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
        if (_alertLevel == alertLevel) {
          _alertNextMs = now + alertRepeatMs;
        }
        if (_mtx) xSemaphoreGive(_mtx);
      }
    }
  }
}

// ===== Low-level tone helper =====
void Buzzer::playTone(int freqHz, int durationMs) {
  if (_pin < 0) return;

  // If already muted, ensure idle and bail
  if (_muted) {
    idleOff();
    return;
  }

  // Use dedicated LEDC channel (see begin()) so we never share
  // PWM resources with RGB or fans.
  ledcWriteTone(BUZZER_PWM_CHANNEL, freqHz);

  int remaining      = durationMs;
  const int sliceMs  = 10;

  while (remaining > 0) {
    if (_muted) {
      ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
      idleOff();
      return;
    }
    int step = (remaining < sliceMs) ? remaining : sliceMs;
    vTaskDelay(pdMS_TO_TICKS(step));
    remaining -= step;
  }

  ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
  idleOff();
}

void Buzzer::playAlert(AlertLevel level) {
  if (_muted || _pin < 0) return;
  switch (level) {
    case AlertLevel::WARNING:
      playTone(1400, 70);
      vTaskDelay(pdMS_TO_TICKS(60));
      playTone(1400, 70);
      break;
    case AlertLevel::CRITICAL:
      playTone(400, 100);
      vTaskDelay(pdMS_TO_TICKS(60));
      playTone(400, 100);
      vTaskDelay(pdMS_TO_TICKS(60));
      playTone(400, 140);
      break;
    default:
      break;
  }
  idleOff();
}

// ===== Patterns =====
void Buzzer::playMode(Mode mode) {
  switch (mode) {
    case Mode::BIP:               playTone(1000, 50); break;
    case Mode::SUCCESS:           playTone(1000, 40); vTaskDelay(pdMS_TO_TICKS(30)); playTone(1300, 40); vTaskDelay(pdMS_TO_TICKS(30)); playTone(1600, 60); break;
    case Mode::FAILED:            for (int i=0;i<2;++i){ playTone(500,50); vTaskDelay(pdMS_TO_TICKS(50)); } break;
    case Mode::WIFI_CONNECTED:    playTone(1200, 100); vTaskDelay(pdMS_TO_TICKS(50)); playTone(1500, 100); break;
    case Mode::WIFI_OFF:          playTone(800, 150); break;
    case Mode::OVER_TEMPERATURE:  for (int i=0;i<4;++i){ playTone(2000,40); vTaskDelay(pdMS_TO_TICKS(60)); } break;
    case Mode::FAULT:             for (int i=0;i<5;++i){ playTone(300,80); vTaskDelay(pdMS_TO_TICKS(40)); } break;
    case Mode::STARTUP:           playTone(600,80); vTaskDelay(pdMS_TO_TICKS(50)); playTone(1000,80); vTaskDelay(pdMS_TO_TICKS(50)); playTone(1400,80); break;
    case Mode::READY:             playTone(2000,50); vTaskDelay(pdMS_TO_TICKS(50)); playTone(2500,50); break;
    case Mode::SHUTDOWN:          playTone(1500,80); vTaskDelay(pdMS_TO_TICKS(50)); playTone(1000,80); vTaskDelay(pdMS_TO_TICKS(50)); playTone(600,80); break;
    case Mode::CLIENT_CONNECTED:  playTone(1100,50); vTaskDelay(pdMS_TO_TICKS(30)); playTone(1300,60); break;
    case Mode::CLIENT_DISCONNECTED: playTone(1200,80); vTaskDelay(pdMS_TO_TICKS(40)); playTone(900,60); break;
  }
  idleOff();
}

// ===== Persistence (no pin in NVS) =====
void Buzzer::loadFromPrefs_() {
  // Use current members as defaults so Init() can seed first-boot values.
  _activeLow = CONF->GetBool(BUZLOW_KEY, _activeLow);
  _muted     = CONF->GetBool(BUZMUT_KEY, _muted);
}

void Buzzer::storeToPrefs_() const {
  CONF->PutBool(BUZLOW_KEY, _activeLow);
  CONF->PutBool(BUZMUT_KEY, _muted);
}
