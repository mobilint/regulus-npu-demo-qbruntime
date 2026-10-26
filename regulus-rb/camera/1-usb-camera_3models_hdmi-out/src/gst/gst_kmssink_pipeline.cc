#include "gst_kmssink_pipeline.h"

#include <cstdint>
#include <stdexcept>

#include "gst/app/gstappsrc.h"
#include "gst/gstpipeline.h"
#include "gst/gstutils.h"
#include "gst/gstvalue.h"

namespace gst_wrapper {
AppsrcKmssinkPipeline::AppsrcKmssinkPipeline(const char* format_in, uint32_t width_in, uint32_t height_in,
                                             uint32_t fps_in) {
  create_elements(format_in, width_in, height_in, fps_in);
  link_elements(format_in, width_in, height_in, fps_in);
}

AppsrcKmssinkPipeline::~AppsrcKmssinkPipeline() {
  stop();
  remove_sink_probe();
}

void AppsrcKmssinkPipeline::add_sink_probe(GstPadProbeCallback callback, gpointer user_data) {
  sink_pad_ = gst_element_get_static_pad(kmssink, "sink");
  if (!sink_pad_) throw std::runtime_error("cannot get kmssink sink pad");
  sink_probe_id_ = gst_pad_add_probe(sink_pad_, GST_PAD_PROBE_TYPE_BUFFER, callback, user_data, nullptr);
  if (sink_probe_id_ == 0) {
    gst_object_unref(sink_pad_);
    sink_pad_ = nullptr;
    throw std::runtime_error("cannot add kmssink sink probe");
  }
}

void AppsrcKmssinkPipeline::remove_sink_probe() {
  if (sink_pad_ && sink_probe_id_ != 0) {
    gst_pad_remove_probe(sink_pad_, sink_probe_id_);
    sink_probe_id_ = 0;
  }
  if (sink_pad_) {
    gst_object_unref(sink_pad_);
    sink_pad_ = nullptr;
  }
}

void AppsrcKmssinkPipeline::create_elements(const char* format_in, uint32_t width_in, uint32_t height_in,
                                            uint32_t fps_in) {
  /* create appsrc */
  appsrc = gst_element_factory_make("appsrc", "appsrc");
  if (!appsrc) {
    g_print("Failed to create element of type 'appsrc'\n");
    throw std::runtime_error("cannot create appsrc");
  }

  g_object_set(appsrc, "max-buffers", 1, "block", FALSE, "emit-signals", FALSE, NULL);
  gst_app_src_set_leaky_type(GST_APP_SRC_CAST(appsrc), GST_APP_LEAKY_TYPE_DOWNSTREAM);

  GstCaps* caps_appsrc_v4l2convert;
  caps_appsrc_v4l2convert =
      gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_in, "width", G_TYPE_INT, width_in, "height",
                          G_TYPE_INT, height_in, "framerate", GST_TYPE_FRACTION, fps_in, 1, NULL);

  if (!caps_appsrc_v4l2convert) {
    g_print("Failed to create caps_appsrc_v4l2convert\n");
    throw std::runtime_error("cannot create appsrc caps");
  }

  gst_app_src_set_caps(GST_APP_SRC_CAST(appsrc), caps_appsrc_v4l2convert);
  gst_caps_unref(caps_appsrc_v4l2convert);

  /* create kmssink */
  kmssink = gst_element_factory_make("kmssink", "kmssink");
  if (!kmssink) {
    g_print("Failed to create element 'kmssink'\n");
    throw std::runtime_error("cannot create kmssink");
  }

  g_object_set(kmssink, "driver-name", "regulus", "force-modesetting", TRUE, "sync", FALSE, NULL);

  /* create pipeline */
  pipeline = gst_pipeline_new("pipeline");
  if (!pipeline) throw std::runtime_error("cannot create display pipeline");

  /* add elements to the pipeline */
  gst_bin_add_many(GST_BIN(pipeline), appsrc, kmssink, NULL);
}

void AppsrcKmssinkPipeline::link_elements(const char* format_out, uint32_t width_out, uint32_t height_out,
                                          uint32_t fps_out) {
  /* link appsrc, kmssink */
  GstCaps* caps_appsrc_kmssink;
  caps_appsrc_kmssink =
      gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_out, "width", G_TYPE_INT, width_out, "height",
                          G_TYPE_INT, height_out, "framerate", GST_TYPE_FRACTION, fps_out, 1, NULL);

  if (!caps_appsrc_kmssink) {
    g_print("Failed to create caps_appsrc_kmssink\n");
    throw std::runtime_error("cannot create kmssink caps");
  }

  if (!gst_element_link_filtered(appsrc, kmssink, caps_appsrc_kmssink)) {
    g_warning("Failed to link appsrc and kmssink!");
    throw std::runtime_error("cannot link appsrc to kmssink");
  }

  gst_caps_unref(caps_appsrc_kmssink);
}
}  // namespace gst_wrapper
