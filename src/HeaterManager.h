#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

#include "Utils.h"

/**
 * HeaterManager
 * --------------
 * Controls 10 nichrome wire outputs using:
 * - ENAxx pins via UCC27524ADR (active HIGH = enable)
 * - Shared INA_OPT PWM output for power control
 * 
 * Features:
 * - Enable/disable individual outputs
 * - Global disable
 * - PWM adjustment using configured DC and desired output voltage
 */
class HeaterManager {
public:
  /**
   * Constructor
   * @param cfg Pointer to ConfigManager (for preferences access)
   */
  explicit HeaterManager(ConfigManager* cfg) : config(cfg) {}

  /**
   * Initialize GPIOs and set PWM power from config
   */
  void begin();

  /**
   * Enable or disable one of the 10 outputs
   * @param index 1–10 (corresponds to ENA01–ENA10)
   * @param enable true to activate, false to deactivate
   */
  void setOutput(uint8_t index, bool enable);

  /**
   * Disable all outputs and turn off PWM
   */
  void disableAll();

  /**
   * Set PWM power output based on desired voltage
   * @param desiredVoltage Target voltage to scale against DC supply
   */
  void setPower(float desiredVoltage);

private:
  ConfigManager* config = nullptr;

  // Active HIGH control lines for UCC27524ADR inputs (ENA01–ENA10)
  const uint8_t enaPins[10] = {
    ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
    ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA10_E_PIN
  };
};

#endif // HEATER_MANAGER_H
