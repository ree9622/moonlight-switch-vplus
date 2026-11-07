#include "DisplayVsyncClock.hpp"
#include <chrono>

#if defined(PLATFORM_SWITCH)
#include <switch.h>
#endif

struct DisplayVsyncClock::Impl {
    bool ready = false;
    int64_t lastEdgeNs = 0;
    int64_t intervalNs = 16666667;
#if defined(PLATFORM_SWITCH)
    ViDisplay display{};
    Event event{};
    bool serviceOpen = false;
    bool displayOpen = false;
    bool eventOpen = false;
    ~Impl() {
        if (eventOpen) eventClose(&event);
        if (displayOpen) viCloseDisplay(&display);
        if (serviceOpen) viExit();
    }
#endif
};

int64_t DisplayVsyncClock::nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

DisplayVsyncClock::DisplayVsyncClock() : impl(std::make_unique<Impl>()) {
#if defined(PLATFORM_SWITCH)
    impl->serviceOpen = R_SUCCEEDED(viInitialize(ViServiceType_Default));
    if (!impl->serviceOpen) return;
    impl->displayOpen = R_SUCCEEDED(viOpenDefaultDisplay(&impl->display));
    if (!impl->displayOpen) return;
    impl->eventOpen = R_SUCCEEDED(viGetDisplayVsyncEvent(&impl->display, &impl->event));
    impl->ready = impl->eventOpen;
#endif
}
DisplayVsyncClock::~DisplayVsyncClock() = default;

DisplayVsyncClock::Tick DisplayVsyncClock::waitNext() {
#if defined(PLATFORM_SWITCH)
    if (impl->ready) {
        // Discard a stale signaled edge, then wait for a fresh display event.
        // Never hold the decoder queue mutex while waiting. The bounded wait
        // also lets shutdown/input continue if a display stops signaling.
        eventClear(&impl->event);
        if (R_SUCCEEDED(eventWait(&impl->event, 50000000))) {
            const int64_t edge = nowNs();
            const int64_t elapsed = edge - impl->lastEdgeNs;
            if (impl->lastEdgeNs && elapsed >= 14000000 && elapsed <= 20000000)
                impl->intervalNs = (impl->intervalNs * 15 + elapsed) / 16;
            impl->lastEdgeNs = edge;
            return {true, edge, impl->intervalNs};
        }
        // Fail visibly and use the existing swapchain cadence until reconnect,
        // rather than stalling the UI for every frame after a service failure.
        impl->ready = false;
    }
#endif
    return {false, nowNs(), impl->intervalNs};
}
