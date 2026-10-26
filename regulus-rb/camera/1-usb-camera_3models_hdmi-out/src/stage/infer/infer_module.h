#ifndef STAGE_INFER_INFER_MODULE_H_
#define STAGE_INFER_INFER_MODULE_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "benchmarker.h"
#include "latency_timeline.h"
#include "qbmodel.h"
#include "run_state.h"
#include "types.h"

namespace app {

struct InferConfig {
  QbMultiModel* mm;  // shared model set + shared pinned input
  int model_idx;     // which model this stage runs (0=face,1=pose,2=od)
  RunState* run;
  const char* branch;
  std::atomic<uint64_t>* activity;
  Benchmarker* bc = nullptr;
  // Set on the traced model only (od); the others leave it null so they do not overwrite it.
  LatencyTimeline* tl = nullptr;
};

// Thread: wait for a new frame in the shared input, run inferPinnedMemory for one model, push the
// raw head tensors. One infer stage per model; all read the same shared pinned input.
std::thread infer_module(InferConfig cfg, Buffer& in, Queue& out);

}  // namespace app

#endif
