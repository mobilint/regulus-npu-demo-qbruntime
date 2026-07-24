#include "benchmarker.h"

Benchmarker::Benchmarker()
    : mTimes{},
      mSum(0.0f),
      mCount(0),
      mCreated(Clock::now()),
      mPrev(mCreated),
      mRunningTime(0.0f),
      mHasLast(false),
      mLastSec(0.0f),
      mIsStarted(false) {}

void Benchmarker::start() {
    mPrev = Clock::now();
    mIsStarted = true;
}

float Benchmarker::end() {
    if (!mIsStarted) {
        mHasLast = true;
        mLastSec = 0.0f;
        return 0.0f;
    }

    std::chrono::duration<float> dt = Clock::now() - mPrev;
    float t = dt.count();

    // Circular buffer handling
    if (mCount >= SIZE) {
        mSum -= mTimes[mCount % SIZE];
    }

    mTimes[mCount % SIZE] = t;
    mSum += t;

    mRunningTime += t;
    mCount++;
    mHasLast = true;
    mLastSec = t;

    mIsStarted = false;
    return t;
}

float Benchmarker::getSec() const { return mHasLast ? mLastSec : 0.0f; }

float Benchmarker::getAvgSec() const {
    size_t n = getSampleCount_();
    if (n == 0) return 0.0f;
    return mSum / static_cast<float>(n);
}

float Benchmarker::getFPS() const {
    float s = getSec();
    return (s == 0.0f) ? 0.0f : (1.0f / s);
}

float Benchmarker::getAvgFPS() const {
    float s = getAvgSec();
    return (s == 0.0f) ? 0.0f : (1.0f / s);
}

float Benchmarker::getRunningTime() const { return mRunningTime; }

size_t Benchmarker::getCount() const { return mCount; }

float Benchmarker::getTimeSinceCreated() const {
    std::chrono::duration<float> dt = Clock::now() - mCreated;
    return dt.count();
}

bool Benchmarker::isStarted() const { return mIsStarted; }

size_t Benchmarker::getSampleCount_() const { return (mCount < SIZE) ? mCount : SIZE; }
