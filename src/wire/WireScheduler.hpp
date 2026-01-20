#ifndef WIRE_SCHEDULER_HPP
#define WIRE_SCHEDULER_HPP

#include <cstdint>
#include <cstddef>

class WireConfigStore;
class WireStateModel;

struct WirePacket {
  uint16_t mask = 0;
  uint16_t onMs = 0;
};

class WireScheduler {
public:
  size_t buildSchedule(const WireConfigStore& cfg,
                       const WireStateModel& state,
                       uint16_t frameMs,
                       uint16_t totalOnMs,
                       float wireMaxC,
                       uint16_t minOnMs,
                       uint16_t maxOnMs,
                       WirePacket* out,
                       size_t maxPackets);
};

#endif // WIRE_SCHEDULER_HPP
