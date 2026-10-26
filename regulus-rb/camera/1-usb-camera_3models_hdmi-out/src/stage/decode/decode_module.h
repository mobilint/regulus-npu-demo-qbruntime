#ifndef STAGE_DECODE_DECODE_MODULE_H_
#define STAGE_DECODE_DECODE_MODULE_H_

#include <atomic>
#include <thread>

#include "benchmarker.h"
#include "latency_timeline.h"
#include "run_state.h"
#include "types.h"

namespace app {

enum class DecodeKind { Det, Pose };

struct DecodeConfig {
  DecodeKind kind;
  int nc;            // classes (det); pose uses 1
  int imh, imw;      // model input size
  float conf_thres;  // score threshold
  float iou_thres;   // NMS IoU threshold
  RunState* run;
  const char* branch;
  std::atomic<uint64_t>* activity;
  Benchmarker* bc = nullptr;
  // Set on the traced model only (od); the others leave it null so they do not overwrite it.
  LatencyTimeline* tl = nullptr;
};

// Thread: pop raw head tensors, run DSP decode+postprocess (filter+NMS) to final Detections, push.
std::thread decode_module(DecodeConfig cfg, Queue& in, Queue& out);

}  // namespace app

#endif
