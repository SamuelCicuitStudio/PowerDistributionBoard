#include <WireSafetyPolicy.hpp>
#include <WireSubsystem.hpp>

uint16_t WireSafetyPolicy::filterMask(uint16_t requestedMask,
                                      const WireConfigStore& cfg,
                                      const WireStateModel& state,
                                      DeviceState deviceState,
                                      bool allowIdle) const {
    const bool heatingAllowed =
        (deviceState == DeviceState::Running) ||
        (allowIdle && deviceState == DeviceState::Idle);
    if (!heatingAllowed) {
        return 0;
    }

    const uint16_t validMask =
        static_cast<uint16_t>((1u << HeaterManager::kWireCount) - 1u);
    uint16_t mask = requestedMask & validMask;

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(mask & bit)) continue;

        const WireRuntimeState& ws = state.wire(i + 1);
        const bool accessAllowed =
            ws.allowedByAccess || cfg.getAccessFlag(i + 1);

        if (!accessAllowed || !ws.present || ws.locked || ws.overTemp) {
            mask &= ~bit;
        }
    }

    return mask;
}
