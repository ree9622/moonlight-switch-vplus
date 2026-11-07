#pragma once

#include "HostCadenceClock.hpp"
#include "DisplayVsyncClock.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class AVFrameQueue {
public:
    explicit AVFrameQueue();
    ~AVFrameQueue();

    bool push(AVFrame* item);
    bool pushTransferred(AVFrame* item);
    AVFrame* pop(bool* consumed = nullptr);
    AVFrame* popForDisplay(const DisplayVsyncClock::Tick& tick, bool* consumed = nullptr);
    AVFrame* acquireWriteFrame();
    void recycleWriteFrame(AVFrame*& frame);
    void configure(size_t queueLimit, int streamFps,
                   bool transferOwnershipEnabled, bool hostCadenceEnabled = false);
    static size_t capacityFor(size_t configuredQueueSize);

    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t targetDepth() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] size_t getFakeFrameUsage() const;
    [[nodiscard]] size_t getFramesDropStat() const;
    [[nodiscard]] size_t getEmptyQueueStat() const;
    [[nodiscard]] size_t getRebufferHoldStat() const;
    [[nodiscard]] size_t getOverflowDropStat() const;
    [[nodiscard]] size_t getPacingSkipStat() const;
    [[nodiscard]] size_t getScheduledHoldStat() const;
    [[nodiscard]] size_t getMaxPushBurstStat() const;
    [[nodiscard]] size_t getLocalClockPacedFrameStat() const;
    [[nodiscard]] size_t getPlayoutResyncStat() const;
    [[nodiscard]] double getEstimatedSourceFps() const;

    struct PacingDiagnostics {
        bool requested = false;
        bool displaySynced = false;
        uint64_t hostFrames = 0;
        uint64_t missingPts = 0;
        uint64_t resets = 0;
        double jitterMs = 0;
        double cushionMs = 0;
        int64_t frontTargetNs = 0;
    };
    [[nodiscard]] PacingDiagnostics pacingDiagnostics() const;
    void cleanup();

private:
    friend class AVFrameHolder;
    AVFrame* acquireFrameLocked();
    bool pushTransferredLocked(AVFrame* item);
    void recordArrivalLocked(std::chrono::steady_clock::time_point now);
    void resetArrivalRateEstimatorLocked();
    void trimToPlayoutWindowLocked();
    struct QueuedFrame { AVFrame* frame; int64_t targetNs; };
    QueuedFrame timedFrameLocked(AVFrame* frame);
    AVFrame* popPreciseLocked(const DisplayVsyncClock::Tick& tick, bool* consumed);
    size_t limit = 0;
    std::queue<QueuedFrame> queue;
    bool hostCadenceEnabled = false;
    bool displaySynced = false;
    std::shared_ptr<DisplayVsyncClock> displayClock;
    HostCadenceClock hostClock;
    uint64_t hostTimedFrames = 0;
    uint64_t missingHostPts = 0;
    std::queue<AVFrame*> freeQueue;
    AVFrame* bufferFrame = nullptr;
    bool transferOwnership = false;
    size_t targetBufferedFrames = 0;
    int streamFps = 0;
    std::chrono::nanoseconds adaptiveFrameInterval{0};
    std::chrono::steady_clock::time_point lastDraw{};
    std::chrono::nanoseconds averageDrawInterval{0};
    std::chrono::steady_clock::time_point arrivalWindowStart{};
    std::chrono::steady_clock::time_point lastArrival{};
    size_t arrivalWindowFrames = 0;
    size_t arrivalRateSamples = 0;
    double estimatedSourceFps = 0.0;
    double frameCredit = 0.0;
    bool drawClockStarted = false;
    bool arrivalClockStarted = false;
    bool startupBuffering = true;
    bool playoutResyncNeeded = true;
    mutable std::mutex m_mutex;
    size_t fakeFrameUsedStat = 0;
    size_t framesDroppedStat = 0;
    size_t emptyQueueStat = 0;
    size_t rebufferHoldStat = 0;
    size_t overflowDropStat = 0;
    size_t pacingSkipStat = 0;
    size_t scheduledHoldStat = 0;
    size_t pushesSincePop = 0;
    size_t maxPushBurstStat = 0;
    size_t localClockPacedFrameStat = 0;
    size_t playoutResyncStat = 0;
};

