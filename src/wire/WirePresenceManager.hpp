#ifndef WIRE_PRESENCE_MANAGER_HPP
#define WIRE_PRESENCE_MANAGER_HPP

#include <cstdint>

class HeaterManager;
class WireStateModel;
class WireConfigStore;
class CpDischg;

class WirePresenceManager {
public:
  void resetFailures();
  bool probeAll(HeaterManager& heater, WireStateModel& state, const WireConfigStore& cfg,
                CpDischg* discharger);
  bool updatePresenceFromMask(HeaterManager& heater, WireStateModel& state,
                              uint16_t mask, float busVoltageStart, float busVoltage);
  bool hasAnyConnected(const WireStateModel& state) const;

private:
  static constexpr uint8_t kWireCount = 10;
  uint8_t _failCount[kWireCount] = {0};

  void setWirePresent_(HeaterManager& heater, WireStateModel& state,
                       uint8_t index, bool present);
};

#endif // WIRE_PRESENCE_MANAGER_HPP
