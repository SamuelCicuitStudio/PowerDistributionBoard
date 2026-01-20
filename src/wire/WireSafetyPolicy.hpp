#ifndef WIRE_SAFETY_POLICY_HPP
#define WIRE_SAFETY_POLICY_HPP

#include <Config.hpp>
#include <cstdint>

class WireConfigStore;
class WireStateModel;

class WireSafetyPolicy {
public:
  uint16_t filterMask(uint16_t requestedMask,
                      const WireConfigStore& cfg,
                      const WireStateModel& state,
                      DeviceState deviceState,
                      bool allowIdle) const;
};

#endif // WIRE_SAFETY_POLICY_HPP
