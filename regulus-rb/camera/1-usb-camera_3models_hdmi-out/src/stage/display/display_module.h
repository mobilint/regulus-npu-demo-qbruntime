#ifndef STAGE_DISPLAY_DISPLAY_MODULE_H_
#define STAGE_DISPLAY_DISPLAY_MODULE_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "benchmarker.h"
#include "latency_timeline.h"
#include "recent_values.h"
#include "run_state.h"
#include "types.h"

namespace app {

struct DisplayConfig {
    const char* format;  // "RGBx" -- 4 bytes per pixel, see CmaDisplayRing
    uint32_t width, height, fps;
    RunState* run;
    Benchmarker* bc = nullptr;        // Time display
    Benchmarker* frame_bc = nullptr;  // total frame rate
    RecentValues<double, 1000>* latencies = nullptr;
    std::atomic<uint64_t>* frames = nullptr;  // frames that reach the kmssink sink pad
    std::atomic<uint64_t>* drops = nullptr;   // appsrc frames lost before the sink pad
    LatencyTimeline* tl =
        nullptr;  // frame age when this stage takes the frame out of display_q
};

// Thread: pop composited frames and push them to the HDMI kmssink pipeline.
std::thread display_module(DisplayConfig cfg, Queue& in);

}  // namespace app

#endif
