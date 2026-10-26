#ifndef CAPTURE_PIPELINE_H
#define CAPTURE_PIPELINE_H

#include <cstdint>
#include <string>

#include "gst/gstelement.h"
#include "gst_base_pipeline.h"

namespace gst_wrapper {

// USB camera (MJPEG) capture with all preprocessing offloaded to HW:
//   v4l2src(MJPEG) -> v4l2jpegdec(CODAJ HW decode) -> v4l2video{N}convert(MAPI HW convert+scale)
//   -> appsink. CODAJ decodes the JPEG, MAPI converts and scales to sink WxH.
// sink_format is the MAPI output layout: "RGB" (3 bytes) or "RGBx" (4 bytes, R,G,B,unused). The
// LCD panel's scanout only accepts 4-byte formats, so that demo asks for RGBx end to end.
// The MAPI convert element name (v4l2video{N}convert) is detected at runtime since the video
// node number varies per board/boot.
class V4l2srcCapturePipeline : public BasePipeline {
 public:
  V4l2srcCapturePipeline() = delete;
  V4l2srcCapturePipeline(const char* cam_dev_path, uint32_t cam_w, uint32_t cam_h, uint32_t cam_fps, uint32_t sink_w,
                         uint32_t sink_h, const char* sink_format = "RGB");

  GstElement* appsink;

 private:
  GstElement* v4l2src;
  GstElement* jpegdec;
  GstElement* mapiconvert;

  void create_elements(const char* cam_dev_path);
  void link_elements(uint32_t cam_w, uint32_t cam_h, uint32_t cam_fps, uint32_t sink_w, uint32_t sink_h,
                     const char* sink_format);
};

// Find the MAPI HW convert element name (v4l2video{N}convert) by scanning /dev/video* for the
// "regulus-mapi" driver. Node numbers vary per board/boot, so detect at runtime. Empty on fail.
std::string find_mapi_convert_element();

}  // namespace gst_wrapper

#endif
