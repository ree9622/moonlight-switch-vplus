#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

class MotionReportRequests {
  public:
    MotionReportRequests() { clear(); }
    void clear() { for (auto& rate : rates) rate.store(0); }
    void set(unsigned controller, unsigned type, uint16_t rate) {
        if (controller < 16 && type >= 1 && type <= 2) rates[controller * 2 + type - 1].store(rate);
    }
    uint16_t get(unsigned controller, unsigned type) const {
        return controller < 16 && type >= 1 && type <= 2 ? rates[controller * 2 + type - 1].load() : 0;
    }
  private:
    std::array<std::atomic<uint16_t>, 32> rates;
};

// Local timestamps are advanced only after the report was successfully queued.
class MotionReportLimiter {
  public:
    void reset() { for (auto& time : sent) time.reset(); }
    bool eligible(unsigned controller, unsigned type, uint16_t rate, uint64_t nowUs) const {
        if (controller >= 16 || type < 1 || type > 2 || rate == 0) return false;
        const auto& time = sent[controller * 2 + type - 1];
        // Floor at microsecond precision: ceil would discard alternating 60 Hz
        // samples whose timestamps are rounded to 16666 us instead of 16667 us.
        return !time || (nowUs >= *time && nowUs - *time >= 1000000u / rate);
    }
    void markSent(unsigned controller, unsigned type, uint64_t nowUs) {
        if (controller < 16 && type >= 1 && type <= 2) sent[controller * 2 + type - 1] = nowUs;
    }
  private:
    std::array<std::optional<uint64_t>, 32> sent{};
};
