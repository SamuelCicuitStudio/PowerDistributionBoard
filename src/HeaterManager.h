#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

#include "Utils.h"
#include "NVSManager.h"


/**
 * HeaterManager
 * --------------
 * Controls 10 nichrome outputs using ENAxx pins (active HIGH).
 * Power is controlled by time-based ON/OFF windows stored in ConfigManager:
 *   - ON_TIME_KEY  (ms)
 *   - OFF_TIME_KEY (ms)
 *
 * setPower(desiredVoltage) recomputes ON/OFF timing for duty cycle control
 * based on desiredVoltage / DC_VOLTAGE_KEY ratio. :contentReference[oaicite:4]{index=4}
 *
 * Thread-safety model:
 * - We add a mutex to protect:
 *   • writing/reading any ENAxx GPIO,
 *   • updating onTimeMs/offTimeMs,
 *   • recomputing timing in setPower().
 *
 * - We intentionally DO NOT add a command queue here. Safety (disableAll)
 *   must be able to override instantly, not wait in FIFO behind older "turn on"
 *   requests. This is different from Indicator/Fan logic.
 */
class HeaterManager {
public:
  explicit HeaterManager()
  : 
    _mutex(nullptr)
  {
    // Configure ENA pins as outputs, default LOW (all heaters OFF/safe)
    pinMode(ENA01_E_PIN, OUTPUT);  digitalWrite(ENA01_E_PIN, LOW);
    pinMode(ENA02_E_PIN, OUTPUT);  digitalWrite(ENA02_E_PIN, LOW);
    pinMode(ENA03_E_PIN, OUTPUT);  digitalWrite(ENA03_E_PIN, LOW);
    pinMode(ENA04_E_PIN, OUTPUT);  digitalWrite(ENA04_E_PIN, LOW);
    pinMode(ENA05_E_PIN, OUTPUT);  digitalWrite(ENA05_E_PIN, LOW);
    pinMode(ENA06_E_PIN, OUTPUT);  digitalWrite(ENA06_E_PIN, LOW);
    pinMode(ENA07_E_PIN, OUTPUT);  digitalWrite(ENA07_E_PIN, LOW);
    pinMode(ENA08_E_PIN, OUTPUT);  digitalWrite(ENA08_E_PIN, LOW);
    pinMode(ENA09_E_PIN, OUTPUT);  digitalWrite(ENA09_E_PIN, LOW);
    pinMode(ENA10_E_PIN, OUTPUT);  digitalWrite(ENA10_E_PIN, LOW);
  }

  /** Initialize:
   *  - create mutex
   *  - debug banner
   *  - seed timing from desired voltage in prefs
   */
  void begin();

  /** Enable or disable one of the 10 outputs (1..10). Thread-safe. */
  void setOutput(uint8_t index, bool enable);

  /** Disable ALL outputs immediately. Thread-safe. */
  void disableAll();

  /** Return current digital state of ENA pin (true if HIGH). Thread-safe. */
  bool getOutputState(uint8_t index) const;
  /** Cache a single wire resistance (Ω) for channel 1..10. Thread-safe. */
  void setWireResistance(uint8_t index, float ohms);

  /** Set global target resistance (Ω) used by control logic. Thread-safe. */
  void setTargetResistanceAll(float ohms);

  /** Optional helper: get cached wire resistance (Ω), 0 if invalid index. */
  float getWireResistance(uint8_t index) const;

private:
  // Active-HIGH ENA control lines (ENA01–ENA10) for reference
  const uint8_t enaPins[10] = {
    ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
    ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA10_E_PIN
  };
  // Cached per-channel wire resistances and global target (Ω)
  float wireResOhms[10] = {0};
  float targetResOhms   = 0;
  // Mutex protects: onTimeMs/offTimeMs + all ENA GPIO writes/reads
  SemaphoreHandle_t _mutex;

  // Lock helpers
  inline bool lock() const {
    if (_mutex == nullptr) return true;
    return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
  }
  inline void unlock() const {
    if (_mutex) xSemaphoreGive(_mutex);
  }
};

#endif // HEATER_MANAGER_H
