#ifndef WIRE_ACTUATOR_HPP
#define WIRE_ACTUATOR_HPP

#include <cstdint>
#include <Config.hpp>

class HeaterManager;
class WireConfigStore;
class WireStateModel;
class WireSafetyPolicy;

class WireActuator {
public:
  uint16_t applyRequestedMask(uint16_t requestedMask,
                              HeaterManager& heater,
                              const WireConfigStore& cfg,
                              WireStateModel& state,
                              const WireSafetyPolicy& safety,
                              DeviceState deviceState,
                              bool allowIdle);
};

#endif // WIRE_ACTUATOR_HPP
