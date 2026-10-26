#ifndef CORE_RUN_STATE_H_
#define CORE_RUN_STATE_H_

#include <sp500/dsp.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace app {

enum class StopReason {
    None,
    Signal,
    Initialization,
    Capture,
    Preprocess,
    Inference,
    Decode,
    DspTimeout,
    Plot,
    Display,
};

class RunState {
   public:
    RunState() { timeout_buffers_.reserve(3); }

    bool running() const { return running_.load(std::memory_order_relaxed); }
    StopReason reason() const { return reason_.load(std::memory_order_relaxed); }
    bool failed() const {
        const StopReason value = reason();
        return value != StopReason::None && value != StopReason::Signal;
    }
    bool timed_out() const { return reason() == StopReason::DspTimeout; }

    void stop(StopReason reason) {
        StopReason current = reason_.load(std::memory_order_relaxed);
        while (
            (current == StopReason::None || current == StopReason::Signal ||
             reason == StopReason::DspTimeout) &&
            !reason_.compare_exchange_weak(current, reason, std::memory_order_relaxed)) {
        }
        running_.store(false, std::memory_order_relaxed);
    }

    void keep_timeout_buffer(sp500_buffer buffer) {
        std::lock_guard<std::mutex> lock(timeout_mutex_);
        timeout_buffers_.push_back(buffer);
    }

    void release_timeout_buffers_after_reload() {
        std::lock_guard<std::mutex> lock(timeout_mutex_);
        for (auto& buffer : timeout_buffers_) sp500_buffer_release(&buffer);
        timeout_buffers_.clear();
    }

   private:
    std::atomic<bool> running_{true};
    std::atomic<StopReason> reason_{StopReason::None};
    std::mutex timeout_mutex_;
    std::vector<sp500_buffer> timeout_buffers_;
};

}  // namespace app
#endif
