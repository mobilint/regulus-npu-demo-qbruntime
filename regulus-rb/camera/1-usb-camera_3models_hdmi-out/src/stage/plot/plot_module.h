#ifndef STAGE_PLOT_PLOT_MODULE_H_
#define STAGE_PLOT_PLOT_MODULE_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "benchmarker.h"
#include "latency_timeline.h"
#include "run_state.h"
#include "types.h"

namespace app {

struct PlotConfig {
  RunState* run;
  // plot is three spans, not one: cpy4 and prep run before the detection wait, draw after it.
  Benchmarker* bc_prep = nullptr;          // sub-stage: CPU work that needs no detections
  Benchmarker* bc_cpy4 = nullptr;          // sub-stage: 4-panel frame write (DMA350 compose)
  Benchmarker* bc_draw3 = nullptr;         // sub-stage: 3 overlay draws (after the detection wait)
  std::atomic<uint64_t>* drops = nullptr;  // frames dropped here (a compose failed)
  LatencyTimeline* tl = nullptr;           // frame age at this stage's handoffs
};

// Thread: pop one display frame, compose it into the four panels and do the drawing that needs no
// detections, then pop the face/pose/od detections (lockstep), draw the overlays and push the
// composited frame to display.
//
// The four panel writes run on the DMA350 (see dma350_composer.h), so the frame must arrive with
// Packet::frame_dmabuf_fd set and the display buffers must be dma-buf backed. If either is missing
// the stage sets a fatal stop instead of falling back to a CPU copy, matching pre_module.
std::thread plot_module(PlotConfig cfg, Queue& frame_in, Queue& face_in, Queue& pose_in, Queue& od_in, Queue& out);

}  // namespace app

#endif
