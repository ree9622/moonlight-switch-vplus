#pragma once

#include <cstdint>
#include <memory>

// A read-only display event. No display mode, firmware or clock-rate changes.
class DisplayVsyncClock {
  public:
    struct Tick {
        bool synchronized = false;
        int64_t edgeNs = 0;
        int64_t intervalNs = 16666667;
    };
    DisplayVsyncClock();
    ~DisplayVsyncClock();
    Tick waitNext();
    static int64_t nowNs();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
