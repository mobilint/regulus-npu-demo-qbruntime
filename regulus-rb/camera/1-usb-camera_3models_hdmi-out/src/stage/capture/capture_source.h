#ifndef STAGE_CAPTURE_CAPTURE_SOURCE_H_
#define STAGE_CAPTURE_CAPTURE_SOURCE_H_

#include "gst/gstelement.h"

namespace app {

// A capture source produces camera frames at a GStreamer appsink. Each concrete source (USB,
// MIPI, IP/RTSP, ...) builds its own internal pipeline; the only contract to the capture stage
// is: expose the appsink, and start()/stop() the pipeline. Frames pulled from appsink MUST be
// dmabuf-backed (the pre stage does a zero-copy DMA350 copy from the dmabuf; there is no CPU
// fallback). Add a new input type by writing one subclass -- no change to capture/pre.
class CaptureSource {
   public:
    virtual ~CaptureSource() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual GstElement* appsink() = 0;
};

}  // namespace app

#endif  // STAGE_CAPTURE_CAPTURE_SOURCE_H_
