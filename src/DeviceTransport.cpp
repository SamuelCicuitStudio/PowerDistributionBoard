#include "DeviceTransport.h"

DeviceTransport* DeviceTransport::s_inst = nullptr;

DeviceTransport* DeviceTransport::Get() {
  if (!s_inst) s_inst = new DeviceTransport();
  return s_inst;
}

Device::StateSnapshot DeviceTransport::getStateSnapshot() const {
  if (!DEVICE) {
    Device::StateSnapshot snap{};
    snap.state = DeviceState::Shutdown;
    snap.seq = 0;
    snap.sinceMs = 0;
    return snap;
  }
  return DEVICE->getStateSnapshot();
}

bool DeviceTransport::waitForStateEvent(Device::StateSnapshot& out, TickType_t toTicks) {
  if (!DEVICE) return false;
  return DEVICE->waitForStateEvent(out, toTicks);
}
