#pragma once

#include "AVFrameQueue.hpp"
#include "Singleton.hpp"
#include "Settings.hpp"
#include <functional>

class AVFrameHolder : public Singleton<AVFrameHolder> {
  public:
    void push(AVFrame* frame) {
        m_frame_queue.push(frame);
    }

    void pushTransferred(AVFrame* frame) {
        m_frame_queue.pushTransferred(frame);
    }

    void get(const std::function<void(AVFrame*)>& fn) {
        auto frame = m_frame_queue.pop();

        if (frame) {
            fn(frame);
        }
    }

    AVFrame* acquireWriteFrame() {
        return m_frame_queue.acquireWriteFrame();
    }

    void recycleWriteFrame(AVFrame*& frame) {
        m_frame_queue.recycleWriteFrame(frame);
    }

    void prepare(int streamFps, bool transferOwnership = false) {
        m_frame_queue.configure(Settings::instance().frames_queue_size(),
                                streamFps, transferOwnership,
#if defined(PLATFORM_SWITCH) && defined(BOREALIS_USE_DEKO3D)
                                Settings::instance().enable_host_cadence_precise_sync()
#else
                                false
#endif
        );
    }

    void cleanup() {
        m_frame_queue.cleanup();
    }

    [[nodiscard]] AVFrameQueue::PacingDiagnostics getPacingDiagnostics() const { return m_frame_queue.pacingDiagnostics(); }
    [[nodiscard]] size_t getFakeFrameStat() const { return m_frame_queue.getFakeFrameUsage(); }
    [[nodiscard]] size_t getFrameDropStat() const { return m_frame_queue.getFramesDropStat(); }
    [[nodiscard]] size_t getFrameQueueSize() const { return m_frame_queue.size(); }
    [[nodiscard]] size_t getFrameQueueTargetDepth() const { return m_frame_queue.targetDepth(); }
    [[nodiscard]] size_t getFrameQueueCapacity() const { return m_frame_queue.capacity(); }
    [[nodiscard]] size_t getFrameQueueEmptyStat() const { return m_frame_queue.getEmptyQueueStat(); }
    [[nodiscard]] size_t getFrameQueueRebufferHoldStat() const { return m_frame_queue.getRebufferHoldStat(); }
    [[nodiscard]] size_t getFrameQueueOverflowDropStat() const { return m_frame_queue.getOverflowDropStat(); }
    [[nodiscard]] size_t getFrameQueuePacingSkipStat() const { return m_frame_queue.getPacingSkipStat(); }
    [[nodiscard]] size_t getFrameQueueScheduledHoldStat() const { return m_frame_queue.getScheduledHoldStat(); }
    [[nodiscard]] size_t getFrameQueueMaxPushBurstStat() const { return m_frame_queue.getMaxPushBurstStat(); }
    [[nodiscard]] size_t getFrameQueueLocalClockPacedFrameStat() const { return m_frame_queue.getLocalClockPacedFrameStat(); }
    [[nodiscard]] size_t getFrameQueuePlayoutResyncStat() const { return m_frame_queue.getPlayoutResyncStat(); }
    [[nodiscard]] double getFrameQueueEstimatedSourceFps() const { return m_frame_queue.getEstimatedSourceFps(); }

  private:
    AVFrameQueue m_frame_queue;
};
