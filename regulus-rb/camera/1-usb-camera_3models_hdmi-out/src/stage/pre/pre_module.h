#ifndef STAGE_PRE_PRE_MODULE_H_
#define STAGE_PRE_PRE_MODULE_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "benchmarker.h"
#include "latency_timeline.h"
#include "qbmodel.h"
#include "run_state.h"
#include "types.h"

namespace app {

struct PreConfig {
    uint32_t cam_w, cam_h;  // for the cover-scale + center-crop geometry (with npu size)
    int npu_w, npu_h;
    QbMultiModel* mm;  // shared pinned input (all models) is written here
    RunState* run;
    Benchmarker* bc = nullptr;
    std::atomic<uint64_t>* drops = nullptr;  // frames dropped here (DMA350 copy failed)
    // Bytes per pixel of the captured frame: 3 (RGB) or 4 (RGBX). The NPU input is always 3
    // channels, so at 4 this stage drops the unused byte on its way into the pinned buffer.
    int bpp = 3;
    LatencyTimeline* tl = nullptr;  // frame age at this stage's two handoffs
};

// Consume dmabuf frames from the capture stage: DMA350-copy the camera dmabuf (center-cropped via
// a byte offset) into a CMA display buffer, then copy that frame into the shared pinned NPU
// input (dropping the unused byte when the capture is 4 bytes per pixel). Pushes the display frame to frame_out (for plot) and a reposition packet to
// reposition_out (a Buffer read by the infer stages). Each input Packet owns a GstSample that this
// stage unrefs after the copy. No CPU fallback: a failed DMA350 copy drops the frame.
std::thread pre_module(PreConfig cfg, Queue& capture_in, Queue& frame_out, Buffer& reposition_out);

}  // namespace app

#endif
