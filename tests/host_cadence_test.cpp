#include "HostCadenceClock.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
bool near(double actual, double expected, double tolerance = 0.0001) {
    return std::isfinite(actual) && std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    constexpr int64_t interval = 16666666;
    constexpr int64_t origin = 1000000000;

    HostCadenceClock steady;
    int64_t previous = 0;
    for (int64_t i = 0; i < 600; ++i) {
        const int64_t pts = i * 16667;
        const int64_t now = origin + pts * 1000;
        const int64_t target = steady.presentTimeNs(now, pts, interval);
        expect(target >= now, "steady source never schedules before its exact arrival grid");
        expect(steady.cushionNs() >= 0 && steady.cushionNs() <= interval, "steady cushion stays within one frame");
        if (i > 0) expect(target > previous, "steady source preserves increasing host cadence");
        previous = target;
    }
    expect(steady.initialized() && steady.resetCount() == 0, "first anchor and normal cadence do not count as resets");
    expect(steady.skewNs() == 0 && steady.offsetNs() == origin, "steady clocks retain their exact offset without false skew");
    expect(steady.cushionNs() == 0, "stable stream's adaptive cushion can decay to the source floor zero");

    HostCadenceClock constants;
    expect(constants.presentTimeNs(100000000, 0, 16000000) == 100500000,
           "PTS zero is valid and startup cushion is half of interval/16");
    expect(constants.presentTimeNs(122400000, 16000, 16000000) == 116684375,
           "source PI correction and EMA reproduce the exact next target");
    expect(constants.offsetNs() == 100100000 && constants.skewNs() == 3125,
           "proportional 1/64 and integral 1/2048 gains are preserved");
    expect(near(constants.jitterEstimateNs(), 1168750), "MAD estimate uses the source 1/32 EMA");
    const auto priorOffset = constants.offsetNs();
    const auto priorSkew = constants.skewNs();
    constants.presentTimeNs(332000000, 32000, 16000000);
    expect(constants.offsetNs() == priorOffset + priorSkew + 125000,
           "offset correction clamps residual to positive 8ms");
    expect(constants.skewNs() == priorSkew + 3906, "positive clamped integral correction truncates like Kotlin Long");
    expect(constants.jitterEstimateNs() > 7000000, "jitter estimator consumes the raw residual rather than its clamped correction");
    const auto positiveOffset = constants.offsetNs();
    const auto positiveSkew = constants.skewNs();
    constants.presentTimeNs(0, 48000, 16000000);
    expect(constants.offsetNs() == positiveOffset + positiveSkew - 125000,
           "offset correction clamps residual to negative 8ms");
    expect(constants.skewNs() == positiveSkew - 3906, "negative division truncates toward zero");

    HostCadenceClock jitter;
    int64_t minTargetOffset = INT64_MAX, maxTargetOffset = INT64_MIN;
    for (int64_t i = 0; i < 1800; ++i) {
        const int64_t pts = i * 16667;
        const int64_t arrivalJitter = (i % 2 ? 3000000 : -3000000);
        const int64_t target = jitter.presentTimeNs(origin + pts * 1000 + arrivalJitter, pts, interval);
        expect(jitter.cushionNs() >= 0 && jitter.cushionNs() <= interval, "jitter cushion is bounded per frame");
        if (i >= 1200) {
            minTargetOffset = std::min(minTargetOffset, target - pts * 1000);
            maxTargetOffset = std::max(maxTargetOffset, target - pts * 1000);
        }
    }
    expect(maxTargetOffset - minTargetOffset < 200000,
           "restored host grid smooths a 6ms alternating arrival spread");
    expect(jitter.jitterEstimateNs() > 2500000 && jitter.jitterEstimateNs() < 3500000,
           "steady alternating jitter is measured as mean absolute deviation");

    HostCadenceClock drift;
    for (int64_t i = 0; i < 12000; ++i) {
        const int64_t pts = i * 16667;
        drift.presentTimeNs(origin + pts * 1000 + i * 500, pts, interval);
    }
    expect(std::llabs(drift.skewNs() - 500) < 100, "integral recovery tracks 500ns per-frame clock drift");
    expect(std::llabs(drift.residualNs()) < 10000, "clock drift does not leave a growing phase ramp");

    HostCadenceClock discontinuity;
    discontinuity.presentTimeNs(origin, 0, interval);
    discontinuity.presentTimeNs(origin + 20000000, 20000, interval);
    const auto backward = discontinuity.presentTimeNs(origin + 30000000, 10000, interval);
    expect(discontinuity.resetCount() == 1 && discontinuity.skewNs() == 0,
           "backward PTS reanchors and clears skew");
    expect(backward == origin + 30000000 + interval / 32, "backward PTS anchor uses the current arrival time");
    discontinuity.presentTimeNs(origin + 40000000, 2010001, interval);
    expect(discontinuity.resetCount() == 2, "host PTS gap greater than two seconds reanchors");
    HostCadenceClock exactGap;
    exactGap.presentTimeNs(origin, 0, interval);
    exactGap.presentTimeNs(origin + 2000000000, 2000000, interval);
    expect(exactGap.resetCount() == 0, "exactly two seconds preserves upstream's strict greater-than threshold");
    exactGap.reset();
    expect(!exactGap.initialized() && exactGap.resetCount() == 1, "explicit reset defers reanchoring until the next frame");
    expect(exactGap.presentTimeNs(0, 0, interval) == interval / 32,
           "zero local timestamp and zero host PTS reanchor without sentinel ambiguity");
    expect(exactGap.offsetNs() == 0 && exactGap.skewNs() == 0, "explicit reanchor clears old phase and drift");

    HostCadenceClock largeJitter;
    largeJitter.presentTimeNs(0, 0, interval);
    largeJitter.presentTimeNs(10000000000, 16667, interval);
    expect(largeJitter.cushionNs() == interval, "very large jitter cannot add more than one frame of cushion");
    expect(largeJitter.presentTimeNs(123, -1, interval) == 123 && !largeJitter.initialized(),
           "missing negative host PTS fails to immediate rendering");
    expect(largeJitter.presentTimeNs(456, 0, 0) == 456, "zero interval cannot divide by zero");

    expect(snapUpToVsync(5000000, 5000000, 16000000) == 5000000, "vsync boundary remains unchanged");
    expect(snapUpToVsync(5000001, 5000000, 16000000) == 21000000, "one nanosecond after a boundary snaps upward");
    expect(snapUpToVsync(4000000, 5000000, 16000000) == 5000000, "time before the current origin snaps to that origin");
    expect(snapUpToVsync(0, 0, interval) == 0, "zero is a valid grid timestamp");
    expect(snapUpToVsync(123, 0, 0) == 123, "unavailable grid interval leaves a raw target unchanged");
    auto snapped = computeHostCadenceSnapTime(70000000, 100000000, 1000000, 10000000);
    expect(snapped.targetNs == 101000000 && !snapped.resetClock, "late target snaps current time without dragging the recovered host clock");
    snapped = computeHostCadenceSnapTime(131000000, 101000000, 1000000, 10000000);
    expect(snapped.targetNs == 131000000 && !snapped.resetClock, "exactly three future vsync intervals remain valid");
    snapped = computeHostCadenceSnapTime(131000001, 101000000, 1000000, 10000000);
    expect(snapped.targetNs == 0 && snapped.resetClock, "target beyond three future vsyncs asks for reset and immediate rendering");
    snapped = computeHostCadenceSnapTime(130000000, 129000000, 1000000, 10000000, 3000000);
    expect(snapped.targetNs == 0 && !snapped.resetClock, "missed presentation deadline renders immediately without clock reset");
    snapped = computeHostCadenceSnapTime(130000000, 128000000, 1000000, 10000000, 3000000);
    expect(snapped.targetNs == 131000000 && !snapped.resetClock, "exact presentation deadline remains valid");
    snapped = computeHostCadenceSnapTime(0, 0, 0, 0);
    expect(snapped.targetNs == 0 && snapped.resetClock, "invalid interval has a defined reset result");

    std::cout << "Passed " << checks << " host cadence checks\n";
}
