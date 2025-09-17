// HeaterManager.cpp  (drop-in)
#include "HeaterManager.h"

void HeaterManager::begin() {

  DEBUG_PRINTLN("###########################################################");
  DEBUG_PRINTLN("#                Starting Heater Manager                  #");
  DEBUG_PRINTLN("###########################################################");

  DEBUG_PRINTLN("All ENA outputs initialized and disabled 🔌");

  // Load existing ON/OFF times (seed with defaults if not present)
  loadTimingFromPrefs();

  // Initialize timing from desired voltage (keeps behavior consistent with prior begin())
  const float desiredVoltage =
      config->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, DEFAULT_DESIRED_OUTPUT_VOLTAGE);
  setPower(desiredVoltage);
  DEBUG_PRINTF("Initial timing set for desired %.1fV ⚡️ (ON=%ums, OFF=%ums)\n",
               desiredVoltage, onTimeMs, offTimeMs);
}

void HeaterManager::setOutput(uint8_t index, bool enable) {
  switch (index) {
    case 1:  digitalWrite(ENA01_E_PIN, enable ? HIGH : LOW); break;
    case 2:  digitalWrite(ENA02_E_PIN, enable ? HIGH : LOW); break;
    case 3:  digitalWrite(ENA03_E_PIN, enable ? HIGH : LOW); break;
    case 4:  digitalWrite(ENA04_E_PIN, enable ? HIGH : LOW); break;
    case 5:  digitalWrite(ENA05_E_PIN, enable ? HIGH : LOW); break;
    case 6:  digitalWrite(ENA06_E_PIN, enable ? HIGH : LOW); break;
    case 7:  digitalWrite(ENA07_E_PIN, enable ? HIGH : LOW); break;
    case 8:  digitalWrite(ENA08_E_PIN, enable ? HIGH : LOW); break;
    case 9:  digitalWrite(ENA09_E_PIN, enable ? HIGH : LOW); break;
    case 10: digitalWrite(ENA10_E_PIN, enable ? HIGH : LOW); break;
    default: break; // invalid index, do nothing
  }

  // Optional debug output
  // DEBUG_PRINTF("Output #%d %s ✅\n", index, enable ? "enabled" : "disabled");
}


void HeaterManager::disableAll() {
  digitalWrite(ENA01_E_PIN, LOW);
  digitalWrite(ENA02_E_PIN, LOW);
  digitalWrite(ENA03_E_PIN, LOW);
  digitalWrite(ENA04_E_PIN, LOW);
  digitalWrite(ENA05_E_PIN, LOW);
  digitalWrite(ENA06_E_PIN, LOW);
  digitalWrite(ENA07_E_PIN, LOW);
  digitalWrite(ENA08_E_PIN, LOW);
  digitalWrite(ENA09_E_PIN, LOW);
  digitalWrite(ENA10_E_PIN, LOW);

  DEBUG_PRINTLN("All outputs disabled 📴");
}


void HeaterManager::setPower(float desiredVoltage) {
  // Read DC supply and clamp ratio
  const float dcMax = config->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);
  if (dcMax <= 0.0f) {
    // Fallback: zero power → ON=0, OFF=period
    loadTimingFromPrefs();
    onTimeMs = 0;
    offTimeMs = (uint16_t)max<int>(1, (int)DEFAULT_ON_TIME + (int)DEFAULT_OFF_TIME);
    storeTimingToPrefs();
    DEBUG_PRINTLN("Invalid DC_VOLTAGE_KEY; set ON=0 and OFF=period ❌");
    return;
  }

  const float ratio = constrain(desiredVoltage / dcMax, 0.0f, 1.0f);

  // Use existing total period as baseline (keep your feel), then redistribute by ratio.
  loadTimingFromPrefs();
  uint32_t period = (uint32_t)onTimeMs + (uint32_t)offTimeMs;
  if (period == 0) {
    period = (uint32_t)DEFAULT_ON_TIME + (uint32_t)DEFAULT_OFF_TIME;
  }

  // Compute new ON/OFF ensuring at least 1 ms total and non-negative parts.
  const uint32_t newOn  = (uint32_t)lroundf(ratio * period);
  const uint32_t newOff = (period > newOn) ? (period - newOn) : 0;

  // Guardrails: keep period ≥ 2 ms when there is any ON time to avoid jittery toggling,
  // and ensure OFF has at least 1 ms unless ratio==1.
  onTimeMs  = (uint16_t)newOn;
  offTimeMs = (uint16_t)newOff;

  if (onTimeMs > 0 && offTimeMs == 0 && ratio < 1.0f) {
    // Give OFF a minimal slot if not at 100% request
    offTimeMs = 1;
    if (onTimeMs > 0) onTimeMs = (uint16_t)max<int>(0, (int)period - (int)offTimeMs);
  }
  if (onTimeMs == 0 && ratio > 0.0f) {
    onTimeMs = 1;
    if (period > 1) offTimeMs = (uint16_t)(period - 1);
  }

  storeTimingToPrefs();

  DEBUG_PRINTF(
    "Timing updated from desired %.2fV (dc=%.2fV, ratio=%.1f%%): ON=%ums, OFF=%ums 🕒\n",
    desiredVoltage, dcMax, ratio * 100.0f, onTimeMs, offTimeMs
  );
}

bool HeaterManager::getOutputState(uint8_t index) const {
  switch (index) {
    case 1:  return digitalRead(ENA01_E_PIN) == HIGH;
    case 2:  return digitalRead(ENA02_E_PIN) == HIGH;
    case 3:  return digitalRead(ENA03_E_PIN) == HIGH;
    case 4:  return digitalRead(ENA04_E_PIN) == HIGH;
    case 5:  return digitalRead(ENA05_E_PIN) == HIGH;
    case 6:  return digitalRead(ENA06_E_PIN) == HIGH;
    case 7:  return digitalRead(ENA07_E_PIN) == HIGH;
    case 8:  return digitalRead(ENA08_E_PIN) == HIGH;
    case 9:  return digitalRead(ENA09_E_PIN) == HIGH;
    case 10: return digitalRead(ENA10_E_PIN) == HIGH;
    default: return false; // invalid index
  }
}


void HeaterManager::loadTimingFromPrefs() {
  onTimeMs  = (uint16_t)config->GetInt(ON_TIME_KEY,  DEFAULT_ON_TIME);
  offTimeMs = (uint16_t)config->GetInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);

  // Ensure sane values
  if (onTimeMs + offTimeMs == 0) {
    onTimeMs  = DEFAULT_ON_TIME;
    offTimeMs = DEFAULT_OFF_TIME;
  }
}

void HeaterManager::storeTimingToPrefs() {
  // Persist immediately so other modules reading the keys see the change right away.
  config->PutInt(ON_TIME_KEY,  onTimeMs);
  config->PutInt(OFF_TIME_KEY, offTimeMs);
}
