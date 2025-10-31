#include "Buzzer.h"
#include "NVSManager.h"   // for CONF

// ===== Singleton backing =====
Buzzer* Buzzer::s_inst = nullptr;

void Buzzer::Init(int pin, bool activeLow) {
  if (!s_inst) s_inst = new Buzzer();

  // Resolve pin strictly from BUZZER_PIN when available.
  s_inst->_pin = pin;         // fallback only if BUZZER_PIN is not defined
  s_inst->_activeLow = activeLow;

  // Apply GPIO now so begin() can proceed immediately.
  if (s_inst->_pin >= 0) {
    pinMode(s_inst->_pin, OUTPUT);
    s_inst->idleOff();
  }

  // Persist only polarity/mute (pin is not persisted)
  s_inst->storeToPrefs_();
}

Buzzer* Buzzer::Get() {
  if (!s_inst) s_inst = new Buzzer();
  return s_inst;
}

Buzzer* Buzzer::TryGet() { return s_inst; }

// ===== Lifecycle =====
bool Buzzer::begin() {
  // Load polarity/mute from CONF; resolve pin from BUZZER_PIN.
  loadFromPrefs_();
  _pin = BUZZER_PIN;
  pinMode(_pin, OUTPUT);
  idleOff();

  if (!_mtx)   _mtx   = xSemaphoreCreateMutex();
  if (!_queue) _queue = xQueueCreate(BUZZER_QUEUE_LEN, sizeof(Mode));
  if (!_mtx || !_queue) return false;

  if (!_task) {
    BaseType_t ok = xTaskCreate(&Buzzer::taskThunk, "BuzzerTask",
                                BUZZER_TASK_STACK, this,
                                BUZZER_TASK_PRIORITY, &_task);
    if (ok != pdPASS) return false;
  }
  DEBUG_PRINTLN("### Buzzer.begin(): task and queue ready");
  return true;
}

void Buzzer::end() {
  if (_task) { TaskHandle_t t = _task; _task = nullptr; vTaskDelete(t); }
  if (_queue) { vQueueDelete(_queue); _queue = nullptr; }
  if (_mtx)   { vSemaphoreDelete(_mtx); _mtx = nullptr; }
  idleOff();
}

// NOTE: attachPin does NOT persist the pin anymore.
// It only rebinds runtime pin; polarity change IS persisted.
void Buzzer::attachPin(int pin, bool activeLow) {
  _pin = pin;
  _activeLow = activeLow;

  if (_pin >= 0) {
    pinMode(_pin, OUTPUT);
    idleOff();
  }
  storeToPrefs_();  // polarity/mute only
}

void Buzzer::setMuted(bool on) {
  _muted = on;
  if (on) {
    // Stop any current tone immediately and clear pending sounds
    noTone(_pin);
    idleOff();
    if (_queue) xQueueReset(_queue);   // drop everything pending
  }
  storeToPrefs_();  // persist mute flag
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
    // Blocks forever with zero CPU until something is enqueued (not enqueued while muted)
    if (xQueueReceive(_queue, &m, portMAX_DELAY) == pdTRUE) {
      playMode(m);
      idleOff();
    }
  }
}

// ===== Low-level tone helper =====
void Buzzer::playTone(int freqHz, int durationMs) {
  if (_pin < 0) return;

  // If already muted, return quickly
  if (_muted) { idleOff(); return; }

  // Start a continuous tone and then sleep in small slices so we can abort
  tone(_pin, freqHz);

  int remaining = durationMs;
  const int slice = 10; // ms, adjust if you want even snappier (<10ms) stops
  while (remaining > 0) {
    if (_muted) {                // mute toggled during play -> abort immediately
      noTone(_pin);
      idleOff();
      return;
    }
    int step = remaining < slice ? remaining : slice;
    vTaskDelay(pdMS_TO_TICKS(step));
    remaining -= step;
  }

  noTone(_pin);
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
  // Always resolve pin from BUZZER_PIN (if defined); otherwise keep current _pin.
  _pin = BUZZER_PIN;
  _activeLow = CONF->GetBool(BUZLOW_KEY, _activeLow);
  _muted     = CONF->GetBool(BUZMUT_KEY, _muted);
}

void Buzzer::storeToPrefs_() const {
  CONF->PutBool(BUZLOW_KEY, _activeLow);
  CONF->PutBool(BUZMUT_KEY, _muted);
}
