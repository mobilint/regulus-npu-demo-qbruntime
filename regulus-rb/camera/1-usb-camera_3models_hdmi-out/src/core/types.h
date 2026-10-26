#ifndef CORE_TYPES_H_
#define CORE_TYPES_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vector>

#include "qbmodel.h"
#include "tsqueue.h"

namespace app {
using SteadyTime = std::chrono::time_point<std::chrono::steady_clock>;

// Decoded detections passed decode -> post -> plot. Boxes are xyxy in model-input
// (letterboxed WxH) coordinates; keypoints (pose only) are 17*(x,y,vis) in the same
// space.
struct Detections {
  std::vector<std::array<float, 4>> boxes;    // xyxy, model-input coords
  std::vector<float> scores;                  // per box
  std::vector<int> labels;                    // per box (class index)
  std::vector<std::vector<float>> keypoints;  // pose only: 51 = 17*(x,y,vis) per box
};

// A raw NPU output that the DSP consumes. Keep the complete description with the pointer so
// decode can tell the planes apart and reject an incompatible model before it gives the DSP a
// bad buffer.
struct RawTensor {
  const float* data = nullptr;
  uint64_t bytes = 0;
  std::vector<int64_t> shape;
};

// One item flowing through the pipeline queues. Each stage fills the fields the next
// stage needs and leaves the rest default. frame rides pre->plot->display; the raw
// tensors ride infer->decode; Detections ride decode->post->plot.
struct Packet {
  cv::Mat frame;               // display frame (center-cropped WxH, BGR)
  int dmabuf_fd = -1;          // capture -> pre: camera dmabuf fd; plot -> display: CMA scanout fd
  int frame_dmabuf_fd = -1;    // pre -> plot: the CMA buffer `frame` views, so plot can hand
                               // it to hardware (DMA350 compose) instead of copying by CPU
  void* gst_sample = nullptr;  // capture -> pre: owns the GstSample* (pre unrefs). void* keeps
                               // types.h free of the GStreamer headers.
  SteadyTime time_point;       // capture timestamp (for latency)
  std::vector<RawTensor> raw;  // infer -> decode: validated pinned head tensor descriptors
  Detections dets;             // decode -> post -> plot
};

using Buffer = ThreadSafeBuffer<Packet>;
using Queue = ThreadSafeQueue<Packet>;
}  // namespace app

#endif
