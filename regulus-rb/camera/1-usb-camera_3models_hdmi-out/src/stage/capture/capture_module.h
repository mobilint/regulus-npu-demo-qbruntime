#ifndef STAGE_CAPTURE_CAPTURE_MODULE_H_
#define STAGE_CAPTURE_CAPTURE_MODULE_H_

#include <atomic>
#include <memory>
#include <thread>

#include "benchmarker.h"
#include "capture_source.h"
#include "latency_timeline.h"
#include "run_state.h"
#include "types.h"

namespace app {

struct CaptureConfig {
    RunState* run;
    Benchmarker* bc = nullptr;  // "Time capture": frame-receipt cost only (excludes the wait for
                                // the next frame and the downstream backpressure on push)
    Benchmarker* frame_bc = nullptr;  // "Camera in": interval between arriving camera frames, i.e.
                                      // the rate the camera actually delivers at. Unlike bc this
                                      // one spans the blocking pull -- it is the only place that
                                      // rate is observable, and it tells whether the pipeline is
                                      // camera-bound or is itself the bottleneck.
    std::atomic<uint64_t>* drops = nullptr;  // frames dropped here (not dmabuf-backed)
    std::atomic<uint64_t>* frames = nullptr;  // frames accepted from the camera
    LatencyTimeline* tl = nullptr;             // frame age when this stage hands the frame on
};

// Capture stage (own thread): pull dmabuf frames from the source's appsink and hand them to pre
// via `out`. Each output Packet carries the camera dmabuf fd (dmabuf_fd) and owns the GstSample
// (gst_sample); pre unrefs the sample after its DMA350 copy. Non-dmabuf frames are dropped with
// an error (no CPU fallback). The source is chosen by the caller (demo.cc) at compile time.
std::thread capture_module(CaptureConfig cfg, std::unique_ptr<CaptureSource> src, Queue& out);

}  // namespace app

#endif  // STAGE_CAPTURE_CAPTURE_MODULE_H_
