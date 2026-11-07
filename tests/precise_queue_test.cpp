#include "AVFrameQueue.hpp"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/pixfmt.h>
}

#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
constexpr int64_t interval = 16666667;

void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}

void fillFrame(AVFrame* frame, uint8_t identity, int64_t pts) {
    expect(frame != nullptr, "frame allocation succeeds");
    frame->format = AV_PIX_FMT_GRAY8;
    frame->width = 2;
    frame->height = 2;
    frame->pts = pts;
    expect(av_frame_get_buffer(frame, 0) == 0, "frame has reference-counted pixel storage");
    frame->data[0][0] = identity;
}

AVFrame* frame(uint8_t identity, int64_t pts) {
    auto* result = av_frame_alloc();
    fillFrame(result, identity, pts);
    return result;
}

void pushBorrowed(AVFrameQueue& queue, uint8_t identity, int64_t pts) {
    auto* input = frame(identity, pts);
    expect(queue.push(input), "borrowed frame is accepted");
    av_frame_free(&input);
}

DisplayVsyncClock::Tick unsynchronized() {
    return {false, DisplayVsyncClock::nowNs(), interval};
}
}

int main() {
    for (size_t depth = 1; depth <= 3; ++depth) {
        AVFrameQueue queue;
        queue.configure(depth, 60, false, true);
        expect(queue.capacity() == depth && queue.targetDepth() == 0,
               "precise mode uses the requested hard bound without a startup reserve");
        for (size_t i = 1; i <= depth + 2; ++i) {
            pushBorrowed(queue, static_cast<uint8_t>(i), static_cast<int64_t>(i - 1) * 16667);
            expect(queue.size() <= depth, "queue never retains more than its precise limit");
        }
        expect(queue.size() == depth && queue.getOverflowDropStat() == 2 && queue.getFramesDropStat() == 2,
               "overflow discards exactly the two oldest frames");
        expect(queue.pacingDiagnostics().hostFrames == depth + 2,
               "every valid host PTS reaches the clock, including dropped frames");
        AVFrame* last = nullptr;
        for (size_t expected = 3; expected <= depth + 2; ++expected) {
            bool consumed = false;
            last = queue.popForDisplay(unsynchronized(), &consumed);
            expect(consumed && last && last->data[0][0] == expected,
                   "retained frames are consumed oldest-first after overflow");
        }
        bool consumed = true;
        expect(queue.popForDisplay(unsynchronized(), &consumed) == last && !consumed,
               "empty precise queue repeats its last frame without claiming new consumption");
        expect(queue.getEmptyQueueStat() == 1 && queue.getFakeFrameUsage() == 1,
               "empty queue repeat is diagnosed separately");
    }

    AVFrameQueue bounds;
    bounds.configure(0, 60, false, true);
    expect(bounds.capacity() == 1, "zero precise queue limit is bounded to one");
    bounds.configure(100, 60, false, true);
    expect(bounds.capacity() == 3, "oversized precise queue limit is bounded to three");
    expect(!bounds.push(nullptr) && !bounds.pushTransferred(nullptr), "null frames are rejected without ownership changes");

    AVFrameQueue timing;
    timing.configure(1, 60, false, true);
    pushBorrowed(timing, 21, 0);
    auto diagnostics = timing.pacingDiagnostics();
    expect(diagnostics.requested && diagnostics.hostFrames == 1 && diagnostics.missingPts == 0,
           "host PTS zero is a valid first timestamp");
    expect(diagnostics.frontTargetNs > 0, "first frame has a concrete host-cadence target");
    const int64_t target = diagnostics.frontTargetNs;
    bool consumed = true;
    auto* output = timing.popForDisplay({true, target - 2 * interval, interval}, &consumed);
    expect(output == nullptr && !consumed && timing.size() == 1,
           "frame more than one display interval ahead is held in the real queue");
    expect(timing.getScheduledHoldStat() == 1 && timing.pacingDiagnostics().displaySynced,
           "future hold records an actual display-synchronized wait");
    output = timing.popForDisplay({true, target - interval, interval}, &consumed);
    expect(output && consumed && output->data[0][0] == 21 && timing.size() == 0,
           "frame is submitted when the next display edge reaches its target");

    AVFrameQueue invalidTarget;
    invalidTarget.configure(2, 60, false, true);
    pushBorrowed(invalidTarget, 31, 0);
    pushBorrowed(invalidTarget, 32, 16667);
    const auto invalidBefore = invalidTarget.pacingDiagnostics();
    output = invalidTarget.popForDisplay({true, invalidBefore.frontTargetNs - 4 * interval, interval}, &consumed);
    const auto invalidAfter = invalidTarget.pacingDiagnostics();
    expect(output && consumed && output->data[0][0] == 31,
           "invalid far-future clock falls back to immediate consumption");
    expect(invalidAfter.resets == invalidBefore.resets + 1 && invalidAfter.frontTargetNs == 0,
           "invalid clock resets and clears every queued target together");
    expect(invalidTarget.getPlayoutResyncStat() == 1, "invalid-clock recovery is counted once");
    output = invalidTarget.popForDisplay({true, invalidBefore.frontTargetNs, interval}, &consumed);
    expect(output && consumed && output->data[0][0] == 32 && invalidTarget.pacingDiagnostics().resets == invalidAfter.resets,
           "cleared queued target cannot cause repeated clock resets");

    AVFrameQueue noPts;
    noPts.configure(2, 60, false, true);
    pushBorrowed(noPts, 41, AV_NOPTS_VALUE);
    pushBorrowed(noPts, 42, -1);
    diagnostics = noPts.pacingDiagnostics();
    expect(diagnostics.hostFrames == 0 && diagnostics.missingPts == 2 && diagnostics.frontTargetNs == 0,
           "missing and negative host PTS take an explicit fallback path");
    output = noPts.popForDisplay({true, DisplayVsyncClock::nowNs(), interval}, &consumed);
    expect(output && consumed && output->data[0][0] == 41,
           "missing PTS still displays frames instead of stalling the queue");

    AVFrameQueue cloned;
    cloned.configure(1, 60, false, true);
    auto* original = frame(51, 0);
    auto* retained = av_buffer_ref(original->buf[0]);
    expect(retained != nullptr, "reference-count probe is retained");
    expect(cloned.push(original), "clone path accepts caller-owned frame");
    expect(av_buffer_get_ref_count(retained) == 3, "clone path adds exactly one pixel-buffer reference");
    original->pts = 999999;
    av_frame_free(&original);
    output = cloned.popForDisplay(unsynchronized(), &consumed);
    expect(output && consumed && output->pts == 0 && output->data[0][0] == 51,
           "queue owns independent frame metadata after caller frees the source");
    cloned.cleanup();
    expect(av_buffer_get_ref_count(retained) == 1, "clone cleanup releases both queued and displayed references");
    av_buffer_unref(&retained);

    AVFrameQueue owned;
    owned.configure(1, 60, true, true);
    auto* writer = owned.acquireWriteFrame();
    fillFrame(writer, 61, 0);
    retained = av_buffer_ref(writer->buf[0]);
    expect(retained != nullptr, "owned-buffer probe is retained");
    AVFrame* transferred = writer;
    expect(owned.pushTransferred(writer), "zero-copy path accepts transferred ownership");
    writer = nullptr;
    expect(av_buffer_get_ref_count(retained) == 2, "ownership transfer does not clone the pixel buffer");
    output = owned.popForDisplay(unsynchronized(), &consumed);
    expect(output == transferred && consumed && output->data[0][0] == 61,
           "zero-copy queue returns the exact transferred frame");
    owned.cleanup();
    expect(av_buffer_get_ref_count(retained) == 1 && retained->data[0] == 61,
           "owned-frame cleanup frees its reference without damaging an external owner");
    av_buffer_unref(&retained);
    writer = owned.acquireWriteFrame();
    fillFrame(writer, 62, 0);
    owned.recycleWriteFrame(writer);
    expect(writer == nullptr, "unused decoder frame is explicitly returned to its pool");
    owned.cleanup();

    // Reconnect uses the same cleanup/prepare lifecycle as the decoder.
    timing.cleanup();
    diagnostics = timing.pacingDiagnostics();
    expect(!diagnostics.requested && !diagnostics.displaySynced && diagnostics.hostFrames == 0 &&
           diagnostics.missingPts == 0 && diagnostics.resets == 0 && timing.size() == 0,
           "cleanup discards host timing and old connection state");
    expect(timing.getFakeFrameUsage() == 0 && timing.getFramesDropStat() == 0 &&
           timing.getEmptyQueueStat() == 0 && timing.getRebufferHoldStat() == 0 &&
           timing.getOverflowDropStat() == 0 && timing.getPacingSkipStat() == 0 &&
           timing.getScheduledHoldStat() == 0 && timing.getMaxPushBurstStat() == 0 &&
           timing.getLocalClockPacedFrameStat() == 0 && timing.getPlayoutResyncStat() == 0,
           "reconnect cleanup resets every queue counter");
    timing.configure(1, 60, false, true);
    pushBorrowed(timing, 71, 0);
    expect(timing.pacingDiagnostics().hostFrames == 1 && timing.pacingDiagnostics().resets == 0,
           "new connection starts with an independent first host PTS");

    AVFrameQueue legacy;
    legacy.configure(3, 60, false, false);
    expect(legacy.capacity() == AVFrameQueue::capacityFor(3) && legacy.capacity() == 8 && legacy.targetDepth() == 2,
           "turning precise mode off preserves the existing jitter reserve and burst headroom");
    pushBorrowed(legacy, 81, 0);
    pushBorrowed(legacy, 82, 16667);
    consumed = true;
    expect(legacy.popForDisplay(unsynchronized(), &consumed) == nullptr && !consumed,
           "legacy startup still waits for its configured reserve");
    pushBorrowed(legacy, 83, 33334);
    output = legacy.popForDisplay(unsynchronized(), &consumed);
    expect(output && consumed && output->data[0][0] == 81 && legacy.size() == 2,
           "legacy startup consumes the oldest frame after the reserve is filled");
    expect(!legacy.pacingDiagnostics().requested && legacy.pacingDiagnostics().hostFrames == 0,
           "legacy mode does not run the new host clock");
    legacy.cleanup();
    legacy.configure(1, 60, false, false);
    for (uint8_t id = 91; id <= 97; ++id) pushBorrowed(legacy, id, AV_NOPTS_VALUE);
    expect(legacy.size() == 1 && legacy.getOverflowDropStat() == 6,
           "legacy overflow retains its existing latest-frame recovery behavior");
    output = legacy.popForDisplay(unsynchronized(), &consumed);
    expect(output && consumed && output->data[0][0] == 97,
           "legacy recovery presents the newest retained frame");

    std::cout << "Passed " << checks << " actual precise queue checks\n";
}
