#pragma once

// Adapted from Moonlight V+ HostCadenceClock.kt and FramePacingController.kt:
// https://github.com/qiin2333/moonlight-vplus/tree/cadb66e2dbe5dabbfb4d5f40d93274232d59bf6a
// Original Moonlight V+ contributors' rights are retained. Distributed under
// GNU GPL version 3; see upstream LICENSE.txt and this repository's LICENSE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace host_cadence_detail {
inline int64_t add(int64_t a, int64_t b) {
    if (b > 0 && a > std::numeric_limits<int64_t>::max() - b)
        return std::numeric_limits<int64_t>::max();
    if (b < 0 && a < std::numeric_limits<int64_t>::min() - b)
        return std::numeric_limits<int64_t>::min();
    return a + b;
}
inline int64_t subtract(int64_t a, int64_t b) {
    if (b > 0 && a < std::numeric_limits<int64_t>::min() + b)
        return std::numeric_limits<int64_t>::min();
    if (b < 0 && a > std::numeric_limits<int64_t>::max() + b)
        return std::numeric_limits<int64_t>::max();
    return a - b;
}
} // namespace host_cadence_detail

// One clock per presentation path. Callers serialize access and sample nowNs
// at frame arrival, not after waiting in the presentation queue. As upstream,
// a target in the past means immediate presentation; it must not move the PI
// clock's grid forward to the arrival time.
class HostCadenceClock {
  public:
    int64_t presentTimeNs(int64_t nowNs, int64_t hostPtsUs, int64_t frameIntervalNs) {
        using host_cadence_detail::add;
        using host_cadence_detail::subtract;
        if (hostPtsUs < 0 || hostPtsUs > std::numeric_limits<int64_t>::max() / 1000 ||
            frameIntervalNs <= 0) {
            reset();
            cushionNs_ = 0;
            return nowNs;
        }
        const int64_t hostNs = hostPtsUs * 1000;
        const int64_t instantOffset = subtract(nowNs, hostNs);
        const bool discontinuity = initialized_ &&
            (hostPtsUs < lastHostPtsUs_ || hostPtsUs - lastHostPtsUs_ > 2000000);
        if (!initialized_ || discontinuity) {
            if (discontinuity) ++resetCount_;
            estimatedOffsetNs_ = instantOffset;
            skewNs_ = 0;
            jitterEstimateNs_ = frameIntervalNs / 16.0;
            residualNs_ = 0;
            initialized_ = true;
        } else {
            const int64_t prediction = add(estimatedOffsetNs_, skewNs_);
            const int64_t residual = subtract(instantOffset, prediction);
            const int64_t clamped = std::clamp<int64_t>(residual, -8000000, 8000000);
            // Kotlin Long division also truncates toward zero.
            estimatedOffsetNs_ = add(prediction, clamped / 64);
            skewNs_ = add(skewNs_, clamped / 2048);
            jitterEstimateNs_ += (std::fabs(static_cast<double>(residual)) - jitterEstimateNs_) / 32.0;
            residualNs_ = residual;
        }
        lastHostPtsUs_ = hostPtsUs;
        // PRECISE_SYNC uses 0.5 * MAD, floor zero, capped at one interval.
        // Clamp before the integer conversion to keep invalid extremes defined.
        const double adaptiveCushion = 0.5 * jitterEstimateNs_;
        cushionNs_ = adaptiveCushion >= static_cast<double>(frameIntervalNs)
            ? frameIntervalNs : static_cast<int64_t>(std::max(0.0, adaptiveCushion));
        return add(add(hostNs, estimatedOffsetNs_), cushionNs_);
    }

    // Match the source: force re-anchoring on the next frame, not immediately.
    // resetCount counts explicit reset requests plus detected discontinuities;
    // the first natural anchor is not counted as a reset.
    void reset() { initialized_ = false; ++resetCount_; }
    bool initialized() const { return initialized_; }
    double jitterEstimateNs() const { return jitterEstimateNs_; }
    int64_t cushionNs() const { return cushionNs_; }
    int64_t skewNs() const { return skewNs_; }
    int64_t offsetNs() const { return estimatedOffsetNs_; }
    int64_t residualNs() const { return residualNs_; }
    uint64_t resetCount() const { return resetCount_; }

  private:
    bool initialized_ = false;
    int64_t estimatedOffsetNs_ = 0;
    int64_t skewNs_ = 0;
    double jitterEstimateNs_ = 0;
    int64_t cushionNs_ = 0;
    int64_t residualNs_ = 0;
    int64_t lastHostPtsUs_ = 0;
    uint64_t resetCount_ = 0;
};

// Upstream step 2: round upward to the local vsync grid. Offset zero is a
// mathematically valid grid origin; the Android caller's "offset unavailable"
// handling is platform-specific and intentionally left to this caller.
inline int64_t snapUpToVsync(int64_t rawNs, int64_t vsyncOffsetNs, int64_t frameIntervalNs) {
    if (frameIntervalNs <= 0) return rawNs;
    const int64_t remainder = host_cadence_detail::subtract(rawNs, vsyncOffsetNs) % frameIntervalNs;
    const int64_t delta = remainder > 0 ? frameIntervalNs - remainder : -remainder;
    return host_cadence_detail::add(rawNs, delta);
}

struct HostCadenceSnapResult {
    int64_t targetNs = 0; // Zero has upstream's immediate-presentation meaning.
    bool resetClock = false;
};

inline HostCadenceSnapResult computeHostCadenceSnapTime(
    int64_t hostTargetNs, int64_t nowNs, int64_t vsyncOffsetNs,
    int64_t frameIntervalNs, int64_t presentationDeadlineNs = 0) {
    using host_cadence_detail::subtract;
    if (frameIntervalNs <= 0) return {0, true};
    const int64_t nextVsync = snapUpToVsync(std::max(hostTargetNs, nowNs), vsyncOffsetNs, frameIntervalNs);
    // Preserve the source's check order: a missed presentation deadline asks
    // for immediate rendering without invalidating the host clock.
    if (presentationDeadlineNs > 0 &&
        subtract(subtract(nextVsync, presentationDeadlineNs), nowNs) < 0)
        return {0, false};
    const int64_t untilVsync = subtract(nextVsync, nowNs);
    const int64_t maxFuture = frameIntervalNs > std::numeric_limits<int64_t>::max() / 3
        ? std::numeric_limits<int64_t>::max() : frameIntervalNs * 3;
    if (untilVsync < 0 || untilVsync > maxFuture) return {0, true};
    return {nextVsync, false};
}
