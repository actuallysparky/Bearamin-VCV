#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bearfield {

constexpr uint8_t kGridWidth = 8;
constexpr uint8_t kCellCount = 64;
constexpr uint16_t kNormMax = 4095;
constexpr uint16_t kNormCenter = 2048;

enum class QuarterTurns : uint8_t {
  Deg0 = 0,
  Deg90 = 1,
  Deg180 = 2,
  Deg270 = 3,
};

enum class PixelOrder : uint8_t {
  RowMajor,
  Serpentine,
  LookupTable,
};

struct GridTransform {
  QuarterTurns rotation = QuarterTurns::Deg0;
  bool mirrorX = false;
  bool mirrorY = false;
};

struct SensorMountConfig {
  GridTransform rawToCanonical = {};
  uint64_t activeZoneMask = UINT64_MAX;
};

struct DisplayMountConfig {
  GridTransform canonicalToPhysical = {};
  PixelOrder pixelOrder = PixelOrder::RowMajor;
  const uint8_t *pixelLut = nullptr;
};

struct Coordinate {
  uint8_t x;
  uint8_t y;
};

inline constexpr uint8_t cellIndex(uint8_t x, uint8_t y) {
  return static_cast<uint8_t>(y * kGridWidth + x);
}

inline Coordinate transformCoordinate(Coordinate input,
                                      GridTransform transform) {
  Coordinate output = input;
  switch (transform.rotation) {
    case QuarterTurns::Deg90:
      output = {static_cast<uint8_t>(7 - input.y), input.x};
      break;
    case QuarterTurns::Deg180:
      output = {static_cast<uint8_t>(7 - input.x),
                static_cast<uint8_t>(7 - input.y)};
      break;
    case QuarterTurns::Deg270:
      output = {input.y, static_cast<uint8_t>(7 - input.x)};
      break;
    case QuarterTurns::Deg0:
    default:
      break;
  }
  if (transform.mirrorX) output.x = static_cast<uint8_t>(7 - output.x);
  if (transform.mirrorY) output.y = static_cast<uint8_t>(7 - output.y);
  return output;
}

inline uint8_t displayPixelIndex(uint8_t canonicalX,
                                 uint8_t canonicalY,
                                 const DisplayMountConfig &mount) {
  const Coordinate physical = transformCoordinate(
      {canonicalX, canonicalY}, mount.canonicalToPhysical);
  const uint8_t logical = cellIndex(physical.x, physical.y);
  if (mount.pixelOrder == PixelOrder::LookupTable && mount.pixelLut != nullptr) {
    return mount.pixelLut[logical];
  }
  if (mount.pixelOrder == PixelOrder::Serpentine && (physical.y & 1u) != 0) {
    return cellIndex(static_cast<uint8_t>(7 - physical.x), physical.y);
  }
  return logical;
}

struct DepthFrame8x8 {
  uint16_t distanceMm[kCellCount] = {};
  uint64_t validMask = 0;
  uint32_t sequence = 0;
  uint64_t capturedUs = 0;
};

inline DepthFrame8x8 orientFrame(const DepthFrame8x8 &raw,
                                 const SensorMountConfig &mount) {
  DepthFrame8x8 canonical = {};
  canonical.sequence = raw.sequence;
  canonical.capturedUs = raw.capturedUs;
  for (uint8_t rawY = 0; rawY < kGridWidth; ++rawY) {
    for (uint8_t rawX = 0; rawX < kGridWidth; ++rawX) {
      const uint8_t rawIndex = cellIndex(rawX, rawY);
      if (((raw.validMask >> rawIndex) & 1ULL) == 0) continue;
      const Coordinate target = transformCoordinate(
          {rawX, rawY}, mount.rawToCanonical);
      const uint8_t targetIndex = cellIndex(target.x, target.y);
      if (((mount.activeZoneMask >> targetIndex) & 1ULL) == 0) continue;
      canonical.distanceMm[targetIndex] = raw.distanceMm[rawIndex];
      canonical.validMask |= 1ULL << targetIndex;
    }
  }
  return canonical;
}

struct FeelProfile {
  uint16_t minimumDepthMm = 60;
  uint16_t playableMaximumMm = 600;
  uint16_t fadeMaximumMm = 750;
  uint16_t maximumSourceDepthMm = 750;
  uint16_t depthOffsetMm = 0;
  uint16_t blobWindowMm = 250;
  uint8_t minimumBlobCells = 4;
  uint8_t centroidMinimumX = 0;
  uint8_t centroidMaximumX = 7;
  uint8_t centroidMinimumY = 0;
  uint8_t centroidMaximumY = 7;
  uint16_t presenceHoldMs = 250;
  float axisTauSeconds = 0.0480898f;
  float depthTauSeconds = 0.0480898f;
  uint16_t idleAxisNorm = kNormCenter;
  uint16_t idleDepthNorm = kNormCenter;
  uint16_t spankMaximumDepthMm = 150;
  float spankMinimumApproachMmPerSecond = 250.0f;
  uint16_t spankReleaseRiseMm = 21;
  uint16_t spankRefractoryMs = 300;
};

inline constexpr FeelProfile BearaminV1Compat() {
  FeelProfile profile = {};
  profile.axisTauSeconds = 0.231735f;
  return profile;
}

inline constexpr FeelProfile BearaminV2() {
  FeelProfile profile = BearaminV1Compat();
  return profile;
}

inline constexpr FeelProfile BearCvV1() {
  FeelProfile profile = {};
  profile.idleAxisNorm = 0;
  profile.idleDepthNorm = 0;
  profile.axisTauSeconds = 0.090f;
  profile.depthTauSeconds = 0.090f;
  return profile;
}

struct ProcessedFieldState {
  bool present = false;
  bool spankStarted = false;
  bool spankHeld = false;
  uint16_t xMilli = 500;
  uint16_t yMilli = 500;
  uint16_t zMm = 0;
  uint16_t xNorm12 = kNormCenter;
  uint16_t yNorm12 = kNormCenter;
  uint16_t zNorm12 = kNormCenter;
  uint16_t pitchNorm12 = kNormCenter;
  uint16_t rollNorm12 = kNormCenter;
  uint8_t validCount = 0;
  uint8_t blobCount = 0;
  uint8_t orientationConfidence = 0;
  uint32_t sourceSequence = 0;
  uint64_t sourceCapturedUs = 0;
  uint64_t generatedUs = 0;
};

class Processor {
 public:
  explicit Processor(FeelProfile profile = BearaminV2())
      : profile_(profile) {
    reset();
  }

  void reset() {
    target_ = {};
    output_ = {};
    target_.xNorm12 = output_.xNorm12 = profile_.idleAxisNorm;
    target_.yNorm12 = output_.yNorm12 = profile_.idleAxisNorm;
    target_.zNorm12 = output_.zNorm12 = profile_.idleDepthNorm;
    previousX_ = profile_.idleAxisNorm;
    previousY_ = profile_.idleAxisNorm;
    previousZ_ = profile_.idleDepthNorm;
    previousPitch_ = kNormCenter;
    previousRoll_ = kNormCenter;
    lastTickUs_ = 0;
    lastPresenceUs_ = 0;
    lastDepthUs_ = 0;
    lastSpankUs_ = 0;
    lastMedianMm_ = 0;
    spankHeld_ = false;
  }

  ProcessedFieldState process(const DepthFrame8x8 &frame,
                              uint64_t nowUs) {
    ProcessedFieldState next = target_;
    next.spankStarted = false;
    next.sourceSequence = frame.sequence;
    next.sourceCapturedUs = frame.capturedUs;
    next.generatedUs = nowUs;

    uint16_t nearestAny = 0;
    uint16_t nearestMusic = 0;
    uint16_t validDepths[kCellCount] = {};
    uint8_t validDepthCount = 0;
    for (uint8_t index = 0; index < kCellCount; ++index) {
      if (((frame.validMask >> index) & 1ULL) == 0) continue;
      const uint16_t distance = frame.distanceMm[index];
      if (distance == 0 || distance > 2000) continue;
      validDepths[validDepthCount++] = distance;
      if (nearestAny == 0 || distance < nearestAny) nearestAny = distance;
      if (distance <= profile_.maximumSourceDepthMm &&
          (nearestMusic == 0 || distance < nearestMusic)) {
        nearestMusic = distance;
      }
    }
    next.validCount = validDepthCount;

    const uint16_t blobAnchor = nearestMusic != 0 ? nearestMusic : nearestAny;
    const uint16_t blobLimit = static_cast<uint16_t>(std::min<uint32_t>(
        profile_.maximumSourceDepthMm,
        static_cast<uint32_t>(blobAnchor) + profile_.blobWindowMm));
    uint64_t weightedX = 0;
    uint64_t weightedY = 0;
    uint64_t weightSum = 0;
    uint8_t blobCount = 0;
    for (uint8_t y = 0; y < kGridWidth; ++y) {
      for (uint8_t x = 0; x < kGridWidth; ++x) {
        const uint8_t index = cellIndex(x, y);
        if (((frame.validMask >> index) & 1ULL) == 0) continue;
        const uint16_t distance = frame.distanceMm[index];
        if (distance == 0 || distance > blobLimit) continue;
        const uint16_t weight = static_cast<uint16_t>(blobLimit - distance + 1);
        weightedX += static_cast<uint64_t>(x) * 1000ULL * weight;
        weightedY += static_cast<uint64_t>(y) * 1000ULL * weight;
        weightSum += weight;
        ++blobCount;
      }
    }
    next.blobCount = blobCount;

    const bool acquired = blobCount >= profile_.minimumBlobCells && weightSum != 0;
    if (acquired) {
      next.present = true;
      lastPresenceUs_ = nowUs;
      next.xMilli = normalizedCentroid(
          weightedX, weightSum,
          profile_.centroidMinimumX, profile_.centroidMaximumX);
      next.yMilli = normalizedCentroid(
          weightedY, weightSum,
          profile_.centroidMinimumY, profile_.centroidMaximumY);
      next.zMm = nearestMusic != 0 ? nearestMusic : nearestAny;
      next.xNorm12 = milliToNorm(next.xMilli);
      next.yNorm12 = milliToNorm(next.yMilli);
      next.zNorm12 = depthToNorm(static_cast<uint16_t>(std::min<uint32_t>(
          65535u, static_cast<uint32_t>(next.zMm) + profile_.depthOffsetMm)));
      estimateOrientation(frame, next);
      updateSpank(validDepths, validDepthCount, nowUs, next);
    } else {
      const uint64_t holdUs = static_cast<uint64_t>(profile_.presenceHoldMs) * 1000ULL;
      next.present = lastPresenceUs_ != 0 && nowUs - lastPresenceUs_ <= holdUs;
      next.blobCount = 0;
      next.zMm = nearestAny;
      if (!next.present) {
        next.xNorm12 = profile_.idleAxisNorm;
        next.yNorm12 = profile_.idleAxisNorm;
        next.zNorm12 = profile_.idleDepthNorm;
        next.pitchNorm12 = kNormCenter;
        next.rollNorm12 = kNormCenter;
        next.orientationConfidence = 0;
        spankHeld_ = false;
      }
      next.spankHeld = spankHeld_;
    }
    target_ = next;
    return advance(nowUs);
  }

  ProcessedFieldState tick(uint64_t nowUs) {
    target_.spankStarted = false;
    return advance(nowUs);
  }

  const ProcessedFieldState &target() const { return target_; }

 private:
  static uint16_t normalizedCentroid(uint64_t weightedCoordinate,
                                     uint64_t weightSum,
                                     uint8_t minimumCell,
                                     uint8_t maximumCell) {
    if (weightSum == 0 || maximumCell <= minimumCell) return 500;
    const uint64_t meanCellMilli = weightedCoordinate / weightSum;
    const uint64_t minimumMilli = static_cast<uint64_t>(minimumCell) * 1000ULL;
    const uint64_t maximumMilli = static_cast<uint64_t>(maximumCell) * 1000ULL;
    if (meanCellMilli <= minimumMilli) return 0;
    if (meanCellMilli >= maximumMilli) return 1000;
    return static_cast<uint16_t>(
        ((meanCellMilli - minimumMilli) * 1000ULL +
         (maximumMilli - minimumMilli) / 2ULL) /
        (maximumMilli - minimumMilli));
  }

  static uint16_t milliToNorm(uint16_t value) {
    const uint16_t clamped = std::min<uint16_t>(value, 1000);
    return static_cast<uint16_t>((static_cast<uint32_t>(clamped) * kNormMax + 500) / 1000);
  }

  uint16_t depthToNorm(uint16_t depthMm) const {
    if (depthMm == 0 || depthMm >= profile_.fadeMaximumMm) return 0;
    if (depthMm <= profile_.minimumDepthMm) return kNormMax;
    if (depthMm <= profile_.playableMaximumMm) {
      const uint32_t span = profile_.playableMaximumMm - profile_.minimumDepthMm;
      const uint32_t remaining = profile_.playableMaximumMm - depthMm;
      const uint32_t curved = (remaining * remaining * 1000ULL + span * span / 2) /
                              (span * span);
      constexpr uint16_t farEdge = 210;
      return static_cast<uint16_t>(farEdge +
          ((kNormMax - farEdge) * curved + 500) / 1000);
    }
    const uint32_t fadeSpan = profile_.fadeMaximumMm - profile_.playableMaximumMm;
    const uint32_t remaining = profile_.fadeMaximumMm - depthMm;
    return static_cast<uint16_t>((210UL * remaining + fadeSpan / 2) / fadeSpan);
  }

  static uint16_t approach(uint16_t current,
                           uint16_t target,
                           float alpha) {
    const float value = static_cast<float>(current) +
        (static_cast<float>(target) - static_cast<float>(current)) * alpha;
    return static_cast<uint16_t>(std::max(0.0f, std::min(4095.0f, std::round(value))));
  }

  ProcessedFieldState advance(uint64_t nowUs) {
    float axisAlpha = lastTickUs_ == 0 ? 1.0f : 0.0f;
    float depthAlpha = lastTickUs_ == 0 ? 1.0f : 0.0f;
    if (lastTickUs_ != 0 && nowUs > lastTickUs_) {
      const float dt = static_cast<float>(nowUs - lastTickUs_) / 1000000.0f;
      axisAlpha = 1.0f - std::exp(-dt / std::max(0.001f, profile_.axisTauSeconds));
      depthAlpha = 1.0f - std::exp(-dt / std::max(0.001f, profile_.depthTauSeconds));
    }
    output_ = target_;
    output_.xNorm12 = approach(previousX_, target_.xNorm12, axisAlpha);
    output_.yNorm12 = approach(previousY_, target_.yNorm12, axisAlpha);
    output_.zNorm12 = approach(previousZ_, target_.zNorm12, depthAlpha);
    output_.pitchNorm12 = approach(previousPitch_, target_.pitchNorm12, axisAlpha);
    output_.rollNorm12 = approach(previousRoll_, target_.rollNorm12, axisAlpha);
    output_.generatedUs = nowUs;
    previousX_ = output_.xNorm12;
    previousY_ = output_.yNorm12;
    previousZ_ = output_.zNorm12;
    previousPitch_ = output_.pitchNorm12;
    previousRoll_ = output_.rollNorm12;
    lastTickUs_ = nowUs;
    return output_;
  }

  static uint16_t median(uint16_t *values, uint8_t count) {
    if (count == 0) return 0;
    std::sort(values, values + count);
    if ((count & 1u) != 0) return values[count / 2];
    return static_cast<uint16_t>((static_cast<uint32_t>(values[count / 2 - 1]) +
                                  values[count / 2]) / 2);
  }

  void updateSpank(uint16_t *depths,
                   uint8_t count,
                   uint64_t nowUs,
                   ProcessedFieldState &state) {
    const uint16_t currentMedian = median(depths, count);
    bool started = false;
    if (lastMedianMm_ != 0 && lastDepthUs_ != 0 && nowUs > lastDepthUs_) {
      const float elapsed = static_cast<float>(nowUs - lastDepthUs_) / 1000000.0f;
      const float approachVelocity =
          (static_cast<float>(lastMedianMm_) - currentMedian) / std::max(0.001f, elapsed);
      const bool refractory = lastSpankUs_ == 0 ||
          nowUs - lastSpankUs_ >= static_cast<uint64_t>(profile_.spankRefractoryMs) * 1000ULL;
      if (!spankHeld_ && refractory &&
          currentMedian <= profile_.spankMaximumDepthMm &&
          approachVelocity >= profile_.spankMinimumApproachMmPerSecond) {
        spankHeld_ = true;
        lastSpankUs_ = nowUs;
        started = true;
      }
    }
    if (spankHeld_ && currentMedian >
        static_cast<uint16_t>(profile_.spankMaximumDepthMm + profile_.spankReleaseRiseMm)) {
      spankHeld_ = false;
    }
    lastMedianMm_ = currentMedian;
    lastDepthUs_ = nowUs;
    state.spankStarted = started;
    state.spankHeld = spankHeld_;
  }

  void estimateOrientation(const DepthFrame8x8 &frame,
                           ProcessedFieldState &state) const {
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float sxx = 0.0f, syy = 0.0f, sxy = 0.0f;
    float sxz = 0.0f, syz = 0.0f;
    uint8_t count = 0;
    for (uint8_t y = 0; y < kGridWidth; ++y) {
      for (uint8_t x = 0; x < kGridWidth; ++x) {
        const uint8_t index = cellIndex(x, y);
        if (((frame.validMask >> index) & 1ULL) == 0) continue;
        const uint16_t depth = frame.distanceMm[index];
        if (depth == 0 || depth > profile_.fadeMaximumMm) continue;
        const float xf = static_cast<float>(x) - 3.5f;
        const float yf = static_cast<float>(y) - 3.5f;
        sx += xf; sy += yf; sz += depth;
        sxx += xf * xf; syy += yf * yf; sxy += xf * yf;
        sxz += xf * depth; syz += yf * depth;
        ++count;
      }
    }
    if (count < 12) {
      state.pitchNorm12 = kNormCenter;
      state.rollNorm12 = kNormCenter;
      state.orientationConfidence = 0;
      return;
    }
    const float n = count;
    const float a00 = sxx - sx * sx / n;
    const float a01 = sxy - sx * sy / n;
    const float a11 = syy - sy * sy / n;
    const float b0 = sxz - sx * sz / n;
    const float b1 = syz - sy * sz / n;
    const float determinant = a00 * a11 - a01 * a01;
    if (std::fabs(determinant) < 0.001f) {
      state.orientationConfidence = 0;
      return;
    }
    const float slopeX = (b0 * a11 - b1 * a01) / determinant;
    const float slopeY = (b1 * a00 - b0 * a01) / determinant;
    const auto slopeToNorm = [](float slope) {
      const float unit = std::max(-1.0f, std::min(1.0f, slope / 75.0f));
      return static_cast<uint16_t>(std::round((unit + 1.0f) * 0.5f * kNormMax));
    };
    state.rollNorm12 = slopeToNorm(slopeX);
    state.pitchNorm12 = slopeToNorm(slopeY);
    state.orientationConfidence = static_cast<uint8_t>(
        std::min<uint16_t>(255, static_cast<uint16_t>(count) * 4));
  }

  FeelProfile profile_;
  ProcessedFieldState target_ = {};
  ProcessedFieldState output_ = {};
  uint64_t lastTickUs_ = 0;
  uint64_t lastPresenceUs_ = 0;
  uint64_t lastDepthUs_ = 0;
  uint64_t lastSpankUs_ = 0;
  uint16_t lastMedianMm_ = 0;
  uint16_t previousX_ = kNormCenter;
  uint16_t previousY_ = kNormCenter;
  uint16_t previousZ_ = kNormCenter;
  uint16_t previousPitch_ = kNormCenter;
  uint16_t previousRoll_ = kNormCenter;
  bool spankHeld_ = false;
};

}  // namespace bearfield
