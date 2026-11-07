#include "VideoStatsWindow.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
bool near(float actual, float expected) {
    return std::isfinite(actual) && std::fabs(actual - expected) < 0.0001f;
}
}

int main() {
    using video_stats::FrameCounts;
    using video_stats::FrameRateWindow;

    FrameRateWindow regular;
    unsigned published = 0;
    for (uint64_t frame = 0; frame <= 180; ++frame) {
        const uint64_t now = (frame * 1000 + 30) / 60;
        const auto rates = regular.observe(now, {frame + 1, frame + 1, frame + 1});
        if (!rates) continue;
        ++published;
        expect(rates->elapsedMs == 1000, "steady stream uses one second of elapsed wall time");
        expect(near(rates->host, 60) && near(rates->received, 60) && near(rates->decoded, 60),
               "60 fps stream never counts the first boundary frame as an extra interval");
        expect(rates->startMs == (published - 1) * 1000, "successive windows share their exact boundary");
    }
    expect(published == 3, "zero-time startup still publishes all three complete windows");

    FrameRateWindow variableDecodeCost;
    published = 0;
    for (uint64_t frame = 0; frame <= 600; ++frame) {
        // The same 60 fps stream finishes decode at different offsets. CPU
        // decode cost must not set the statistics sampling cadence.
        const uint64_t decodeCostMs = frame % 5;
        const uint64_t now = (frame * 1000 + 30) / 60 + decodeCostMs;
        const auto rates = variableDecodeCost.observe(now, {frame + 1, frame + 1, frame + 1});
        if (!rates) continue;
        ++published;
        expect(rates->elapsedMs == 1000, "irregular decode costs preserve wall-time sampling");
        expect(near(rates->received, 60) && near(rates->decoded, 60),
               "irregular decode cost does not inflate frame rates");
    }
    expect(published == 10, "decode-cost variation produces ten samples over ten seconds");

    FrameRateWindow continuous;
    expect(!continuous.observe(0, {1, 1, 1}), "first completed frame establishes a boundary only");
    expect(!continuous.observe(500, {31, 31, 31}), "a partial half-second is not published");
    auto rates = continuous.observe(1000, {61, 61, 61});
    expect(rates && near(rates->received, 60), "first full second reports 60 rather than 61 fps");
    expect(!continuous.observe(1017, {62, 62, 62}), "first frame of next window does not move its boundary");
    rates = continuous.observe(2000, {121, 121, 121});
    expect(rates && rates->startMs == 1000 && near(rates->received, 60),
           "next window retains the previous frame-to-frame interval");

    FrameRateWindow stopped;
    stopped.observe(0, {1, 1, 1});
    stopped.observe(1000, {61, 61, 61});
    rates = stopped.observe(10000, {61, 61, 61});
    expect(rates && rates->elapsedMs == 9000 && near(rates->host, 0) && near(rates->decoded, 0),
           "long idle interval reports zero finite frame rates");
    rates = stopped.observe(11000, {121, 121, 121});
    expect(rates && near(rates->received, 60), "stream recovers to 60 fps after a long pause");
    rates = stopped.observe(21000, {122, 122, 122});
    expect(rates && near(rates->received, 0.1f), "single frame after a long interruption includes elapsed idle time");

    FrameRateWindow noOutput;
    noOutput.observe(0, {1, 1, 0});
    rates = noOutput.observe(1000, {81, 61, 0});
    expect(rates && near(rates->host, 80) && near(rates->received, 60) && near(rates->decoded, 0),
           "host, received and zero decoded frame counts remain independent");
    expect(near(video_stats::average(100, 0), 0), "zero decoded frames cannot create infinite latency");
    expect(near(video_stats::average(0, 0), 0), "empty session averages remain finite");
    expect(near(video_stats::average(183, 61), 3), "session average can include the boundary frame and current window");
    expect(near(video_stats::average(180, 60), 3), "window average uses that window's own frame count");

    FrameRateWindow empty;
    empty.observe(0, {});
    expect(!empty.observe(0, {}), "equal timestamps never divide by zero");
    rates = empty.observe(1000, {});
    expect(rates && near(rates->host, 0) && near(rates->received, 0) && near(rates->decoded, 0),
           "an entirely empty window has finite zero rates");
    expect(!empty.observe(500, {}), "backwards clock resets the observation without unsigned underflow");
    rates = empty.observe(1500, {60, 60, 60});
    expect(rates && near(rates->decoded, 60), "statistics recover after a clock rebase");
    expect(!empty.observe(2000, {}), "reset counters rebase rather than producing huge rates");
    rates = empty.observe(3000, {60, 60, 60});
    expect(rates && near(rates->decoded, 60), "statistics recover after counter reset");
    empty.reset();
    expect(!empty.started() && !empty.observe(5000, FrameCounts{1, 1, 1}),
           "new decoder setup starts an independent sampling session");

    std::cout << "Passed " << checks << " video statistics checks\n";
}
