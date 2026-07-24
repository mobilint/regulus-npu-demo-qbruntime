#ifndef V4L2JPEGENC_PIPELINE_H
#define V4L2JPEGENC_PIPELINE_H

#include <gst/gst.h>

#include <cstdint>
#include <vector>

#include "gst/gstelement.h"
#include "gst_base_pipeline.h"

namespace gst_wrapper {
class AppsrcV4l2jpegencAppsinkPipeline : public BasePipeline {
public:
    AppsrcV4l2jpegencAppsinkPipeline() = delete;
    AppsrcV4l2jpegencAppsinkPipeline(const char* format_in, uint32_t width_in,
                                     uint32_t height_in, uint32_t fps_in,
                                     const char* format_temp, uint32_t width_out,
                                     uint32_t height_out, uint32_t fps_out);

    GstElement* appsrc;
    GstElement* appsink;

private:
    GstElement *v4l2convert, *v4l2jpegenc;
    std::vector<GstElement*> queues{2};

    void create_elements(const char* format_in, uint32_t width_in, uint32_t height_in,
                         uint32_t fps_in);
    void link_elements(const char* format_out, uint32_t width_out, uint32_t height_out,
                       uint32_t fps_out);
};
}  // namespace gst_wrapper

#endif
