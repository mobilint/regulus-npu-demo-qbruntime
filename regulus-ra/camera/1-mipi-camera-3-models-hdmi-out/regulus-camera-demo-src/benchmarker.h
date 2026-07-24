#ifndef DEMO_BENCHMARKER_H_
#define DEMO_BENCHMARKER_H_

#include <array>
#include <chrono>
#include <cstddef>

class Benchmarker {
    using Clock = std::chrono::steady_clock;
    static constexpr size_t SIZE = 1000;

public:
    Benchmarker();

    void start();
    float end();

    float getSec() const;
    float getAvgSec() const;
    float getFPS() const;
    float getAvgFPS() const;
    float getRunningTime() const;
    size_t getCount() const;
    float getTimeSinceCreated() const;
    bool isStarted() const;

private:
    size_t getSampleCount_() const;

private:
    std::array<float, SIZE> mTimes;
    float mSum;
    size_t mCount;
    Clock::time_point mCreated;
    Clock::time_point mPrev;
    float mRunningTime;
    bool mHasLast;
    float mLastSec;
    bool mIsStarted;
};

#endif  // DEMO_BENCHMARKER_H_
