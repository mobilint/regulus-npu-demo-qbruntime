#ifndef LATENCY_TIMELINE_H_
#define LATENCY_TIMELINE_H_

#include <chrono>

#include "recent_values.h"
#include "types.h"

namespace app {

// How old a frame is now, in ms, counted from the moment appsink handed it over.
inline double age_ms(const SteadyTime& t) {
  return std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - t}.count();
}

// One frame's journey, as its age at every handoff along the way. `X_in` is when a stage took the
// frame, `X_out` is when that stage was finished with it, `X_pushed` is when its handoff to the next
// queue returned. Consecutive differences alternate between the things that make up latency:
//
//     pre_out    -> pre_pushed   pre was blocked pushing: the queue was still full
//     pre_pushed -> plot_in      the frame sat in the queue until plot woke up and took it
//     plot_in    -> plot_out     a stage worked on it
//
// Because the points partition the whole interval, a breakdown built from them ADDS UP to
// `appsink to screen`. Stage times cannot be added up that way -- a frame sitting in a size-1
// queue, or a stage blocked pushing into a full one, is nobody's work, so no stage records it
// (the benchmarks close before push -- see infer_module).
//
// Note what is NOT on this journey: the frame never visits infer or decode. pre makes a second
// copy for the NPU and that copy goes there. What lands on the frame's own timeline instead is
// plot blocked waiting for that copy's detections -- the gap from plot_in to dets_in.
struct LatencyTimeline {
  RecentValues<double, 1000> capture_out;     // capture is finished; about to hand it on
  RecentValues<double, 1000> capture_pushed;  // its push into capture_q returned
  RecentValues<double, 1000> pre_in;          // pre took it out of capture_q
  RecentValues<double, 1000> pre_out;         // pre is finished; about to hand it on
  RecentValues<double, 1000> pre_pushed;      // its push into frame_q returned
  RecentValues<double, 1000> plot_in;         // plot took the frame out of frame_q
  RecentValues<double, 1000> dets_in;         // plot also has all three detections
  RecentValues<double, 1000> plot_out;        // plot finished writing the display buffer
  RecentValues<double, 1000> display_in;      // display took it out of display_q (HDMI only)

  // The detection branch, for ONE traced model (od -- the slowest, so it is the one plot ends up
  // waiting for). These do NOT belong to the sum above: the frame itself never goes here, pre
  // sends a copy (see the note at the top). They are printed nested under `post_q x3` because
  // they explain why that wait is as long as it is, in the source frame's own clock.
  RecentValues<double, 1000> det_infer_in;       // the infer thread saw a new pinned input
  RecentValues<double, 1000> det_infer_out;      // the NPU finished this model
  RecentValues<double, 1000> det_infer_pushed;   // its push into raw_q returned
  RecentValues<double, 1000> det_decode_in;      // decode took it out of raw_q
  RecentValues<double, 1000> det_decode_out;     // the DSP finished decode + NMS
  RecentValues<double, 1000> det_decode_pushed;  // its push into post_q returned
};

}  // namespace app

#endif  // LATENCY_TIMELINE_H_
