#ifndef RGBLED_H
#define RGBLED_H

#include "RGBConfig.h"
#include "Config.h"
#include <Arduino.h>

// ---------- Priorities (bigger = stronger) ----------
enum : uint8_t {
  PRIO_BACKGROUND = 0,
  PRIO_ACTION     = 1,
  PRIO_ALERT      = 2,
  PRIO_CRITICAL   = 3
};

// ---------- Patterns ----------
enum class Pattern : uint8_t {
  OFF, SOLID, BLINK, BREATHE, RAINBOW, HEARTBEAT2, FLASH_ONCE,
};

// ---------- Background states ----------
enum class DevState : uint8_t {
  BOOT, INIT, PAIRING, READY_ONLINE, READY_OFFLINE, SLEEP,
  START, IDLE, RUN, OFF, FAULT, MAINT,
  WAIT   // unified “waiting” background (12V/button)
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
  PWR_12V_LOST,            // 12 V supply dropped while active
  PWR_DC_LOW,              // DC bus under threshold (but not fully lost)
  FAULT_OVERCURRENT,       // latched overcurrent
  FAULT_THERMAL_GLOBAL,    // global thermal lock / all wires blocked
  FAULT_THERMAL_CH_LOCK,   // some channels thermally locked
  FAULT_SENSOR_MISSING,    // temp/current sensor missing / invalid
  FAULT_CFG_ERROR,         // invalid configuration / NVS / calibration issue
  DISCHG_ACTIVE,           // capacitor discharge ongoing
  DISCHG_DONE,             // discharge finished
  BYPASS_FORCED_OFF        // bypass MOSFET forced off due to safety
};


// ---------- Pattern options payload ----------
struct PatternOpts {
  uint32_t color      = RGB_HEX(255,255,255);
  uint16_t periodMs   = 300;
  uint16_t onMs       = 100;
  uint32_t durationMs = 0;
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
  void rainbow(uint16_t stepMs = 20, uint8_t priority = PRIO_BACKGROUND, bool preempt = true, uint32_t durationMs = 0);
  void heartbeat(uint32_t color, uint16_t periodMs = 1500, uint8_t priority = PRIO_ACTION, bool preempt = true, uint32_t durationMs = 0);
  void flash(uint32_t color, uint16_t onMs = 120, uint8_t priority = PRIO_ACTION, bool preempt = true);
  void playPattern(Pattern pat, const PatternOpts& opts);

  // Pins (pass pinB = -1 if Blue not wired)
  void attachPins(int pinR, int pinG, int pinB = -1, bool activeLow = true);

private:
  // ----- Internal command wire -----
  enum class CmdType : uint8_t { SET_BACKGROUND, PLAY, STOP, SHUTDOWN };
  struct Cmd {
    CmdType     type;
    DevState    bgState;
    Pattern     pattern;
    PatternOpts opts;
  };

  static void taskThunk(void* arg);
  void        taskLoop();

  // Low-level I/O
  void writeColor(uint8_t r, uint8_t g, uint8_t b);

  // Helpers
  bool sendCmd(const Cmd& c, TickType_t to = 0);
  void stepRainbow(uint16_t stepMs);
  void stepBlink(uint32_t color, uint16_t periodMs);
  void stepBreathe(uint32_t color, uint16_t periodMs);
  void doHeartbeat2(uint32_t color, uint16_t periodMs);
  void doFlashOnce(uint32_t color, uint16_t onMs);
  void applyBackground(DevState s);

  // NEW: asymmetric strobe for FAULT background (private helper)
  void doStrobe(uint32_t color, uint16_t onMs, uint16_t offMs);

  // RG-only utility: strip blue channel if needed
  inline uint32_t rgOnly(uint32_t c) const {
#if RGB_FORCE_RG_ONLY
    return RGB_HEX(RGB_R(c), RGB_G(c), 0);
#else
    return c;
#endif
  }

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
  SemaphoreHandle_t _mtx   = nullptr;

  // live state (owned by worker)
  uint8_t     _currentPrio = PRIO_BACKGROUND;
  Pattern     _currentPat  = Pattern::OFF;
  PatternOpts _currentOpts {};
  bool        _haveCurrent = false;

  // background (worker owns)
  DevState _bgState = DevState::START;
};

#define RGB RGBLed::Get()  // convenience

#endif // RGBLED_H
