#include <WireScheduler.hpp>
#include <WireSubsystem.hpp>
#include <math.h>

namespace {
constexpr float kTempMarginC = 10.0f;
constexpr float kWireTempMaxC = 150.0f;
}

size_t WireScheduler::buildSchedule(const WireConfigStore& cfg,
                                    const WireStateModel& state,
                                    uint16_t frameMs,
                                    uint16_t totalOnMs,
                                    float wireMaxC,
                                    uint16_t minOnMs,
                                    uint16_t maxOnMs,
                                    WirePacket* out,
                                    size_t maxPackets) {
  if (!out || maxPackets == 0) return 0;
  if (frameMs == 0 || totalOnMs == 0) return 0;

  if (wireMaxC <= 0.0f || !isfinite(wireMaxC)) {
    wireMaxC = kWireTempMaxC;
  }
  if (wireMaxC > kWireTempMaxC) {
    wireMaxC = kWireTempMaxC;
  }

  const uint16_t budgetMs =
      (totalOnMs > frameMs) ? frameMs : totalOnMs;

  uint8_t idxs[HeaterManager::kWireCount] = {0};
  float weights[HeaterManager::kWireCount] = {0.0f};
  size_t count = 0;

  for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
    const WireRuntimeState& ws = state.wire(i + 1);
    if (!ws.allowedByAccess) continue;
    if (!ws.present) continue;
    if (ws.locked || ws.overTemp) continue;
    if (isfinite(ws.tempC) && ws.tempC >= (wireMaxC - kTempMarginC)) continue;

    float r = cfg.getWireResistance(i + 1);
    if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;

    idxs[count] = i;
    weights[count] = r;
    count++;
  }

  if (count == 0) return 0;
  if (count > maxPackets) count = maxPackets;

  double wSum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    wSum += static_cast<double>(weights[i]);
  }
  if (!(wSum > 0.0)) {
    for (size_t i = 0; i < count; ++i) {
      weights[i] = 1.0f;
    }
    wSum = static_cast<double>(count);
  }

  float onMsF[HeaterManager::kWireCount] = {0.0f};
  float frac[HeaterManager::kWireCount] = {0.0f};
  float minOnF = static_cast<float>(minOnMs);
  float maxOnF = (maxOnMs == 0) ? static_cast<float>(budgetMs)
                                : static_cast<float>(maxOnMs);
  if (maxOnF > static_cast<float>(frameMs)) {
    maxOnF = static_cast<float>(frameMs);
  }

  bool enforceMin =
      (minOnMs > 0) && (static_cast<float>(budgetMs) >= minOnF * count);

  float sumF = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    const float w = weights[i];
    float t = static_cast<float>(budgetMs) * (w / static_cast<float>(wSum));
    if (enforceMin && t < minOnF) t = minOnF;
    if (t > maxOnF) t = maxOnF;
    onMsF[i] = t;
    sumF += t;
  }

  if (sumF > static_cast<float>(budgetMs) && sumF > 0.0f) {
    if (enforceMin) {
      const float minTotal = minOnF * count;
      const float avail = (static_cast<float>(budgetMs) > minTotal)
                              ? (static_cast<float>(budgetMs) - minTotal)
                              : 0.0f;
      float extraSum = 0.0f;
      for (size_t i = 0; i < count; ++i) {
        float extra = onMsF[i] - minOnF;
        if (extra < 0.0f) extra = 0.0f;
        extraSum += extra;
      }
      const float scale = (extraSum > 0.0f) ? (avail / extraSum) : 0.0f;
      sumF = 0.0f;
      for (size_t i = 0; i < count; ++i) {
        float extra = onMsF[i] - minOnF;
        if (extra < 0.0f) extra = 0.0f;
        onMsF[i] = minOnF + extra * scale;
        if (onMsF[i] > maxOnF) onMsF[i] = maxOnF;
        sumF += onMsF[i];
      }
    } else {
      const float scale = static_cast<float>(budgetMs) / sumF;
      sumF = 0.0f;
      for (size_t i = 0; i < count; ++i) {
        onMsF[i] *= scale;
        if (onMsF[i] > maxOnF) onMsF[i] = maxOnF;
        sumF += onMsF[i];
      }
    }
  }

  uint16_t sumI = 0;
  for (size_t i = 0; i < count; ++i) {
    const float v = onMsF[i];
    uint16_t t = (v > 0.0f) ? static_cast<uint16_t>(floorf(v)) : 0;
    if (t == 0) {
      frac[i] = 0.0f;
      onMsF[i] = 0.0f;
    } else {
      frac[i] = v - static_cast<float>(t);
      onMsF[i] = static_cast<float>(t);
    }
    sumI += t;
  }

  uint16_t remaining = 0;
  if (sumI < budgetMs) {
    remaining = budgetMs - sumI;
  }

  while (remaining > 0) {
    size_t best = count;
    float bestFrac = 0.0f;
    for (size_t i = 0; i < count; ++i) {
      if (frac[i] > bestFrac) {
        bestFrac = frac[i];
        best = i;
      }
    }
    if (best == count || bestFrac <= 0.0f) break;
    onMsF[best] += 1.0f;
    frac[best] = 0.0f;
    remaining--;
  }

  size_t outCount = 0;
  for (size_t i = 0; i < count && outCount < maxPackets; ++i) {
    const uint16_t t = static_cast<uint16_t>(onMsF[i]);
    if (t == 0) continue;
    out[outCount].mask = static_cast<uint16_t>(1u << idxs[i]);
    out[outCount].onMs = t;
    outCount++;
  }

  return outCount;
}
