#include <WireActuator.hpp>
#include <WireSafetyPolicy.hpp>
#include <WireSubsystem.hpp>
#include <HeaterManager.hpp>

uint16_t WireActuator::applyRequestedMask(uint16_t requestedMask,
                                          HeaterManager& heater,
                                          const WireConfigStore& cfg,
                                          WireStateModel& state,
                                          const WireSafetyPolicy& safety,
                                          DeviceState deviceState,
                                          bool allowIdle) {
    const uint16_t safeMask =
        safety.filterMask(requestedMask, cfg, state, deviceState, allowIdle);
    heater.setOutputMask(safeMask);
    state.setLastMask(safeMask);
    return safeMask;
}
