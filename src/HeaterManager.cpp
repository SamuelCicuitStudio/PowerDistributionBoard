#include "HeaterManager.h"


void HeaterManager::begin() {
  DEBUGGSTART();
  DEBUG_PRINTLN("###########################################################");
  DEBUG_PRINTLN("#                Starting Heater Manager                  #");
  DEBUG_PRINTLN("###########################################################");
  DEBUGGSTOP();
 
  // Create mutex so future ops are serialized
  _mutex = xSemaphoreCreateMutex();
  DEBUG_PRINTLN("All ENA outputs initialized and disabled 🔌");
  // Initialize timing from desired voltage (same behavior you had)

}

// ---------------------------------------------------------------------------
// setOutput(): turn one heater line ON/HIGH or OFF/LOW, thread-safe.
// ---------------------------------------------------------------------------
void HeaterManager::setOutput(uint8_t index, bool enable) {
  if (!lock()) return;

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
    default: break; // invalid index, ignore
  }

  unlock();
  DEBUG_PRINTF("Output #%d %s ✅\n", index, enable ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------
// disableAll(): emergency kill switch. Thread-safe.
// We don't queue this because it MUST preempt everything ASAP.
// ---------------------------------------------------------------------------
void HeaterManager::disableAll() {
  if (!lock()) return;

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
  unlock();

  DEBUG_PRINTLN("All outputs disabled 📴");
}

// ---------------------------------------------------------------------------
// getOutputState(): read a pin level safely. Thread-safe.
// ---------------------------------------------------------------------------
bool HeaterManager::getOutputState(uint8_t index) const {
  if (!lock()) {
    // best-effort fallback read without mutex
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
      default: return false;
    }
  }

  bool result = false;
  switch (index) {
    case 1:  result = (digitalRead(ENA01_E_PIN) == HIGH); break;
    case 2:  result = (digitalRead(ENA02_E_PIN) == HIGH); break;
    case 3:  result = (digitalRead(ENA03_E_PIN) == HIGH); break;
    case 4:  result = (digitalRead(ENA04_E_PIN) == HIGH); break;
    case 5:  result = (digitalRead(ENA05_E_PIN) == HIGH); break;
    case 6:  result = (digitalRead(ENA06_E_PIN) == HIGH); break;
    case 7:  result = (digitalRead(ENA07_E_PIN) == HIGH); break;
    case 8:  result = (digitalRead(ENA08_E_PIN) == HIGH); break;
    case 9:  result = (digitalRead(ENA09_E_PIN) == HIGH); break;
    case 10: result = (digitalRead(ENA10_E_PIN) == HIGH); break;
    default: result = false; break;
  }

  unlock();
  return result;
}

