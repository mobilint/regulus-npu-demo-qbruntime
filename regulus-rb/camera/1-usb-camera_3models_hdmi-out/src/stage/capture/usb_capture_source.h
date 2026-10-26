#ifndef STAGE_CAPTURE_USB_CAPTURE_SOURCE_H_
#define STAGE_CAPTURE_USB_CAPTURE_SOURCE_H_

#include <cstdint>

#include "capture_source.h"
#include "gst_capture_pipeline.h"

namespace app {

// USB camera source: MJPEG -> CODAJ(HW decode) -> MAPI(HW convert+scale) -> appsink dmabuf.
// Wraps the existing V4l2srcCapturePipeline behind the CaptureSource interface. sink_w/sink_h is
// the MAPI (cover-scaled) output size the pre stage then center-crops. sink_format is "RGB" or
// "RGBx" -- see gst_capture_pipeline.h.
class UsbCaptureSource : public CaptureSource {
 public:
  UsbCaptureSource(const char* dev_path, uint32_t cam_w, uint32_t cam_h, uint32_t cam_fps, uint32_t sink_w,
                   uint32_t sink_h, const char* sink_format = "RGB")
      : pipe_(dev_path, cam_w, cam_h, cam_fps, sink_w, sink_h, sink_format) {}

  void start() override { pipe_.start(); }
  void stop() override { pipe_.stop(); }
  GstElement* appsink() override { return pipe_.appsink; }

 private:
  gst_wrapper::V4l2srcCapturePipeline pipe_;
};

}  // namespace app

#endif  // STAGE_CAPTURE_USB_CAPTURE_SOURCE_H_
