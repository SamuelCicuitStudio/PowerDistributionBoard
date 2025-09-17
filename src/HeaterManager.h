// HeaterManager.h  (drop-in)
#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

#include "Utils.h"

/**
 * HeaterManager
 * --------------
 * Controls 10 nichrome outputs using ENAxx pins (active HIGH).
 * Power is controlled by time-based ON/OFF windows stored in ConfigManager:
 *   - ON_TIME_KEY  (ms)
 *   - OFF_TIME_KEY (ms)
 * setPower(desiredVoltage) recomputes and stores new ON/OFF times according
 * to desiredVoltage/DC_VOLTAGE_KEY ratio. No INA_OPT PWM is used anymore.
 */
class HeaterManager {
public:
  explicit HeaterManager(ConfigManager* cfg) : config(cfg) {
    // ENA pins as outputs, default LOW (outputs inactive)
    // Explicit pin setup
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

  /** Initialize ENA pins LOW and seed timing keys if missing */
  void begin();

  /** Enable/disable one of the 10 outputs (1..10) */
  void setOutput(uint8_t index, bool enable);

  /** Disable all outputs */
  void disableAll();

  /**
   * Recompute ON/OFF times from desiredVoltage and save them to preferences.
   * desiredVoltage is clamped to [0..DC_VOLTAGE_KEY].
   */
  void setPower(float desiredVoltage);

  /** Return current digital state of ENA pin (true if HIGH) */
  bool getOutputState(uint8_t index) const;

private:
  ConfigManager* config = nullptr;

  // Active-HIGH ENA control lines (ENA01–ENA10)
  const uint8_t enaPins[10] = {
    ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
    ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA10_E_PIN
  };

  // Cached timing (ms), kept in sync with ConfigManager
  uint16_t onTimeMs  = DEFAULT_ON_TIME;
  uint16_t offTimeMs = DEFAULT_OFF_TIME;

  // Helper: read timing keys (with defaults) into cache
  void loadTimingFromPrefs();
  // Helper: write cache timing back to prefs
  void storeTimingToPrefs();
};

#endif // HEATER_MANAGER_H
