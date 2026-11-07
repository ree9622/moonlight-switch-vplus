#include "MotionReportState.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    unsigned checks = 0;
    auto expect = [&](bool result) { ++checks; if (!result) std::exit(1); };
    MotionReportRequests requests;
    MotionReportLimiter limiter;
    expect(requests.get(0, 1) == 0);
    expect(!limiter.eligible(0, 1, requests.get(0, 1), 0));
    requests.set(0, 1, 60);
    expect(requests.get(0, 1) == 60 && requests.get(0, 2) == 0);
    expect(limiter.eligible(0, 1, 60, 0));
    limiter.markSent(0, 1, 0);
    expect(!limiter.eligible(0, 1, 60, 16665));
    expect(limiter.eligible(0, 1, 60, 16666));
    expect(limiter.eligible(1, 1, 60, 0));
    expect(limiter.eligible(0, 2, 30, 0));
    requests.set(0, 1, 0);
    expect(!limiter.eligible(0, 1, requests.get(0, 1), 100000));
    limiter.markSent(0, 2, 100000);
    expect(!limiter.eligible(0, 2, 30, 133332));
    expect(limiter.eligible(0, 2, 30, 133333));
    expect(!limiter.eligible(0, 2, 30, 99999));
    requests.set(15, 2, 65535);
    expect(requests.get(15, 2) == 65535);
    requests.set(16, 1, 60);
    requests.set(0, 0, 60);
    expect(requests.get(16, 1) == 0 && requests.get(0, 0) == 0);
    expect(!limiter.eligible(16, 1, 60, 1000000));
    expect(!limiter.eligible(0, 3, 60, 1000000));
    requests.clear();
    limiter.reset();
    expect(requests.get(15, 2) == 0 && requests.get(0, 1) == 0);
    expect(limiter.eligible(0, 1, 60, 0));
    expect(!limiter.eligible(0, 1, requests.get(0, 1), 1000000));
    std::cout << "Passed " << checks << " host motion request checks\n";
}
