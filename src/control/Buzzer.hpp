/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include <Config.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ===== Task / Queue sizing =====
#ifndef BUZZER_TASK_STACK
#define BUZZER_TASK_STACK    2048
#endif
#ifndef BUZZER_TASK_PRIORITY
#define BUZZER_TASK_PRIORITY 1
#endif
#ifndef BUZZER_QUEUE_LEN
#define BUZZER_QUEUE_LEN     12
#endif

class Buzzer {
public:
  enum class Mode : uint8_t {
    BIP = 0, SUCCESS, FAILED, WIFI_CONNECTED, WIFI_OFF,
    OVER_TEMPERATURE, FAULT, STARTUP, READY, SHUTDOWN,
    CLIENT_CONNECTED, CLIENT_DISCONNECTED
  };

  // Singleton access.
  // NOTE: BUZZER_PIN (from Config.h) is the authority for pin if defined.
  //       Init() now RESPECTS stored mute state and does NOT overwrite it.
  static void    Init(int pin = -1, bool activeLow = true);
  static Buzzer* Get();
  static Buzzer* TryGet();

  // Lifecycle
  bool begin();   // loads polarity/mute from CONF, resolves pin, starts task
  void end();

  // Runtime changes (pin change is NOT persisted; polarity/mute ARE).
  void attachPin(int pin, bool activeLow = true);

  void setMuted(bool on);
  bool isMuted() const { return _muted; }

  // Enqueue helpers (no-ops while muted)
  void bip();
  void successSound();
  void failedSound();
  void bipWiFiConnected();
  void bipWiFiOff();
  void bipOverTemperature();
  void bipFault();
  void bipStartupSequence();
  void bipSystemReady();
  void bipSystemShutdown();
  void bipClientConnected();
  void bipClientDisconnected();

  void enqueue(Mode m);  // drops immediately if muted

private:
  Buzzer() = default;
  Buzzer(const Buzzer&) = delete;
  Buzzer& operator=(const Buzzer&) = delete;

  static void taskThunk(void* arg);
  void        taskLoop();

  void playMode(Mode m);
  void playTone(int freqHz, int durationMs);
  inline void idleOff() {
    if (_pin < 0) return;
    digitalWrite(_pin, HIGH);
  }

  // Persist/load helpers (NO pin in NVS)
  void loadFromPrefs_();
  void storeToPrefs_() const;

private:
  static Buzzer* s_inst;

  // HW
  int           _pin       = -1;     // from BUZZER_PIN or last attachPin
  bool          _activeLow = true;   // persisted
  volatile bool _muted     = false;  // persisted

  // RTOS
  TaskHandle_t      _task  = nullptr;
  QueueHandle_t     _queue = nullptr;
  SemaphoreHandle_t _mtx   = nullptr;
};

// Convenience macro
#define BUZZ  (Buzzer::Get())

#endif // BUZZER_H


