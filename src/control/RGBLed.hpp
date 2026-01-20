/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef RGBLED_H
#define RGBLED_H

#include <RGBConfig.hpp>
#include <Config.hpp>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ---------- Priorities (higher preempts) ----------
enum : uint8_t {
  PRIO_BACKGROUND = 0,
  PRIO_ACTION     = 1,
  PRIO_ALERT      = 2,
  PRIO_CRITICAL   = 3
};

// ---------- Patterns (status-focused) ----------
enum class Pattern : uint8_t { OFF, SOLID, BLINK, BREATHE, HEARTBEAT2, FLASH_ONCE, STROBE, CODE };

// ---------- Background states ----------
enum class DevState : uint8_t {
  BOOT,
  INIT,
  PAIRING,
  READY_ONLINE,
  READY_OFFLINE,
  SLEEP,
  START,
  IDLE,
  RUN,
  OFF,
  FAULT,
  MAINT,
  WAIT   // waiting for 12V/button/ready
};

// ---------- Overlay events ----------
enum class OverlayEvent : uint8_t {
  // Generic
  WAKE_FLASH, NET_RECOVER, RESET_TRIGGER, LOW_BATT, CRITICAL_BATT,

  // Wi-Fi + Web roles
  WIFI_STATION, WIFI_AP_, WIFI_LOST,
  WEB_ADMIN_ACTIVE, WEB_USER_ACTIVE,

  // Fan / Relay
  FAN_ON, FAN_OFF, RELAY_ON, RELAY_OFF,

  // Temperature / Current
  TEMP_WARN, TEMP_CRIT, CURR_WARN, CURR_TRIP,

  // Output feedback (indexed also available)
  OUTPUT_TOGGLED_ON, OUTPUT_TOGGLED_OFF,

  // Power-up sequence (Device::loopTask)
  PWR_WAIT_12V,
  PWR_CHARGING,
  PWR_THRESH_OK,
  PWR_BYPASS_ON,
  PWR_WAIT_BUTTON,
  PWR_START,

  // Power & protection detail
  PWR_12V_LOST,
  PWR_DC_LOW,
  FAULT_OVERCURRENT,
  FAULT_THERMAL_GLOBAL,
  FAULT_THERMAL_CH_LOCK,
  FAULT_SENSOR_MISSING,
  FAULT_CFG_ERROR,
  DISCHG_ACTIVE,
  DISCHG_DONE,
  BYPASS_FORCED_OFF
};

// ---------- Error codes (color = category, blink count = code) ----------
enum class ErrorCategory : uint8_t {
  POWER,      // red
  CALIB,      // yellow
  THERMAL,    // amber
  SENSOR,     // blue
  CONFIG,     // magenta
  COMMS       // cyan
};

// ---------- Latched alert levels ----------
enum class AlertLevel : uint8_t {
  NONE,
  WARN,
  CRITICAL
};

// ---------- Pattern options payload ----------
struct PatternOpts {
  uint32_t color      = RGB_HEX(255,255,255);
  uint16_t periodMs   = 300;
  uint16_t onMs       = 100;
  uint16_t gapMs      = 800;   // for Pattern::CODE: pause after the code group
  uint32_t durationMs = 0;     // 0 => indefinite
  uint8_t  count      = 0;     // for Pattern::CODE: number of blinks
  uint8_t  priority   = PRIO_ACTION;
  bool     preempt    = true;
};

class RGBLed {
public:
  // ---------------- Singleton access ----------------
  static void     Init(int pinR, int pinG, int pinB = -1, bool activeLow = true);
  static RGBLed*  Get();
  static RGBLed*  TryGet();

  // ---------------- Lifecycle ----------------
  bool begin();
  void end();

  // ---------------- Background state ----------------
  void setDeviceState(DevState s);

  // Convenience
  inline void setStart() { setDeviceState(DevState::START); }
  inline void setIdle()  { setDeviceState(DevState::IDLE);  }
  inline void setRun()   { setDeviceState(DevState::RUN);   }
  inline void setOff()   { setDeviceState(DevState::OFF);   }
  inline void setFault() { setDeviceState(DevState::FAULT); }
  inline void setMaint() { setDeviceState(DevState::MAINT); }
  inline void setWait()  { setDeviceState(DevState::WAIT);  }

  // ---------------- Overlay events ----------------
  void postOverlay(OverlayEvent e);

  // Indexed output overlay (e.g., channel 1..10)
  void postOutputEvent(uint8_t channelIndex, bool on, uint8_t priority = PRIO_ACTION);

  // ---------------- Direct helpers ----------------
  void off(uint8_t priority = PRIO_ACTION, bool preempt = true);
  void solid(uint32_t color, uint8_t priority = PRIO_ACTION, bool preempt = true, uint32_t durationMs = 0);
  void blink(uint32_t color, uint16_t periodMs, uint8_t priority = PRIO_ACTION, bool preempt = true, uint32_t durationMs = 0);
  void breathe(uint32_t color, uint16_t periodMs, uint8_t priority = PRIO_ACTION, bool preempt = true, uint32_t durationMs = 0);
  void heartbeat(uint32_t color, uint16_t periodMs = 1500, uint8_t priority = PRIO_ACTION, bool preempt = true, uint32_t durationMs = 0);
  void flash(uint32_t color, uint16_t onMs = 120, uint8_t priority = PRIO_ACTION, bool preempt = true);
  void strobe(uint32_t color, uint16_t onMs = 60, uint16_t offMs = 60, uint8_t priority = PRIO_CRITICAL, bool preempt = true, uint32_t durationMs = 0);
  void playPattern(Pattern pat, const PatternOpts& opts);

  // Clear current overlay/pattern and return to background.
  void clearActivePattern();

  // Error codes: color encodes category, blink count encodes code.
  void showError(ErrorCategory category,
                 uint8_t code,
                 uint8_t priority = PRIO_CRITICAL,
                 bool preempt = true,
                 uint32_t durationMs = 0);
  void showErrorCode(uint32_t color,
                     uint8_t code,
                     uint16_t onMs = 120,
                     uint16_t offMs = 120,
                     uint16_t gapMs = 800,
                     uint8_t priority = PRIO_CRITICAL,
                     bool preempt = true,
                     uint32_t durationMs = 0);

  // Latched alert (warning/critical) shown until cleared.
  void setAlert(AlertLevel level, uint32_t color = 0);
  void clearAlert();
  bool hasAlert() const { return _alertActive; }

  // Pins (Blue is expected; pass pinB = -1 only if unwired)
  void attachPins(int pinR, int pinG, int pinB = -1, bool activeLow = true);

private:
  // ----- Internal command wire -----
  enum class CmdType : uint8_t { SET_BACKGROUND, SET_ALERT, CLEAR_ALERT, PLAY, STOP, SHUTDOWN };
  struct Cmd {
    CmdType     type;
    DevState    bgState;
    Pattern     pattern;
    PatternOpts opts;
  };

  static void taskThunk(void* arg);
  void        taskLoop();

  void applyBackground();
  void applyBackground(DevState s);
  void setActivePattern(Pattern pat, const PatternOpts& opts);

  // Low-level I/O
  void writeColor(uint8_t r, uint8_t g, uint8_t b);

  // Helpers
  bool sendCmd(const Cmd& c, TickType_t to = 0);
  void stepBlink(uint32_t color, uint16_t periodMs);
  void stepBreathe(uint32_t color, uint16_t periodMs);
  void doHeartbeat2(uint32_t color, uint16_t periodMs);
  void doFlashOnce(uint32_t color, uint16_t onMs);
  void doStrobe(uint32_t color, uint16_t onMs, uint16_t offMs);
  void doCode(uint32_t color, uint8_t count, uint16_t onMs, uint16_t periodMs, uint16_t gapMs);

private:
  // -------- Singleton storage --------
  static RGBLed* s_instance;
  RGBLed() = default;
  RGBLed(int pinR, int pinG, int pinB, bool activeLow) { attachPins(pinR,pinG,pinB,activeLow); }
  RGBLed(const RGBLed&) = delete;
  RGBLed& operator=(const RGBLed&) = delete;

  // pins
  int  _pinR = -1, _pinG = -1, _pinB = -1;
  bool _activeLow = true;

  // RTOS
  TaskHandle_t      _task  = nullptr;
  QueueHandle_t     _queue = nullptr;

  // live state (owned by worker)
  uint8_t     _currentPrio    = PRIO_BACKGROUND;
  Pattern     _currentPat     = Pattern::OFF;
  PatternOpts _currentOpts    {};
  bool        _haveCurrent    = false;
  uint32_t    _currentStartMs = 0;

  // background (worker owns)
  DevState _bgState = DevState::START;

  // latched alert (worker owns)
  bool       _alertActive = false;
  Pattern    _alertPat    = Pattern::OFF;
  PatternOpts _alertOpts  {};
};

#define RGB RGBLed::Get()  // convenience

#endif // RGBLED_H

