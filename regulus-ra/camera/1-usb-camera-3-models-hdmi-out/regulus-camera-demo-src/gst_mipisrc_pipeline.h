#ifndef MIPISRC_PIPELINE_H
#define MIPISRC_PIPELINE_H

#include <cstdint>

#include "gst/gstelement.h"
#include "gst_base_pipeline.h"

namespace gst_wrapper {
class MipiSrcAppsinkPipeline : public BasePipeline {
public:
    MipiSrcAppsinkPipeline() = delete;
    MipiSrcAppsinkPipeline(const char* mipi_dev_path, const char* camera_format,
                           uint32_t camera_width, uint32_t camera_height,
                           uint32_t camera_fps, const char* sink_format,
                           uint32_t sink_width, uint32_t sink_height, uint32_t sink_fps);

    GstElement* v4l2src;
    GstElement* videorate;
    GstElement* appsink;

private:
    GstElement* v4l2convert;

    void create_elements(const char* mipi_dev_path);
    void link_elements(const char* camera_format, uint32_t camera_width,
                       uint32_t camera_height, uint32_t camera_fps,
                       const char* sink_format, uint32_t sink_width, uint32_t sink_height,
                       uint32_t sink_fps);
};
}  // namespace gst_wrapper

#endif
