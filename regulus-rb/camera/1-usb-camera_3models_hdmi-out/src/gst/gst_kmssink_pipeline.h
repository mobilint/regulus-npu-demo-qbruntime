#ifndef KMSSINK_PIPELINE_H
#define KMSSINK_PIPELINE_H

#include <cstdint>

#include "gst/gstelement.h"
#include "gst_base_pipeline.h"

namespace gst_wrapper {
class AppsrcKmssinkPipeline : public BasePipeline {
   public:
    AppsrcKmssinkPipeline() = delete;
    AppsrcKmssinkPipeline(const char* format_in, uint32_t width_in, uint32_t height_in,
                          uint32_t fps_in);
    ~AppsrcKmssinkPipeline();

    GstElement* appsrc;
    void add_sink_probe(GstPadProbeCallback callback, gpointer user_data);
    void remove_sink_probe();

   private:
    GstElement* kmssink;
    GstPad* sink_pad_ = nullptr;
    gulong sink_probe_id_ = 0;

    void create_elements(const char* format_in, uint32_t width_in, uint32_t height_in,
                         uint32_t fps_in);
    void link_elements(const char* format_out, uint32_t width_out, uint32_t height_out,
                       uint32_t fps_out);
};
}  // namespace gst_wrapper

#endif
