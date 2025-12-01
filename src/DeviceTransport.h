#ifndef DEVICE_TRANSPORT_H
#define DEVICE_TRANSPORT_H

#include "Device.h"

/**
 * @brief Thin facade for WiFi/UI to interact with Device without touching internals.
 */
class DeviceTransport {
public:
  static DeviceTransport* Get();

  Device::StateSnapshot getStateSnapshot() const;
  bool waitForStateEvent(Device::StateSnapshot& out, TickType_t toTicks);

private:
  static DeviceTransport* s_inst;
  DeviceTransport() = default;
};

#define DEVTRAN DeviceTransport::Get()

#endif // DEVICE_TRANSPORT_H
