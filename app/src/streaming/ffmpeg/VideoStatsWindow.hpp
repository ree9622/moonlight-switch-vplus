#pragma once

#include <cstdint>
#include <optional>

namespace video_stats {

struct FrameCounts {
    uint64_t host = 0;
    uint64_t received = 0;
    uint64_t decoded = 0;
};

struct FrameRates {
    float host = 0;
    float received = 0;
    float decoded = 0;
    uint64_t startMs = 0;
    uint64_t elapsedMs = 0;
};

inline float average(uint64_t sum, uint64_t count) {
    return count ? static_cast<float>(static_cast<double>(sum) / count) : 0.0f;
}

// Rates count frame arrivals between two observations, not the frame that
// established the first observation. Keep each published boundary for the next
// window so no frame-to-frame interval disappears between samples.
class FrameRateWindow {
  public:
    bool started() const { return started_; }
    void reset() { *this = {}; }

    std::optional<FrameRates> observe(uint64_t nowMs, FrameCounts counts) {
        if (!started_ || nowMs < startMs_ || counts.host < baseline_.host ||
            counts.received < baseline_.received || counts.decoded < baseline_.decoded) {
            started_ = true;
            startMs_ = nowMs;
            baseline_ = counts;
            return std::nullopt;
        }
        const uint64_t elapsed = nowMs - startMs_;
        if (elapsed < 1000) return std::nullopt;
        const FrameRates rates{
            static_cast<float>((counts.host - baseline_.host) * 1000.0 / elapsed),
            static_cast<float>((counts.received - baseline_.received) * 1000.0 / elapsed),
            static_cast<float>((counts.decoded - baseline_.decoded) * 1000.0 / elapsed),
            startMs_, elapsed};
        startMs_ = nowMs;
        baseline_ = counts;
        return rates;
    }

  private:
    bool started_ = false;
    uint64_t startMs_ = 0;
    FrameCounts baseline_;
};

} // namespace video_stats
