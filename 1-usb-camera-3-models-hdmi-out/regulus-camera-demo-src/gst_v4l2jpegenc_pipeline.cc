#include "gst_v4l2jpegenc_pipeline.h"

#include <gst/gst.h>

#include <cstdint>
#include <string>
#include <vector>

#include "glib-object.h"
#include "gst/app/gstappsrc.h"
#include "gst/gstpipeline.h"
#include "gst/gstutils.h"
#include "gst/gstvalue.h"

namespace gst_wrapper {
AppsrcV4l2jpegencAppsinkPipeline::AppsrcV4l2jpegencAppsinkPipeline(
    const char* format_in, uint32_t width_in, uint32_t height_in, uint32_t fps_in,
    const char* format_temp, uint32_t width_out, uint32_t height_out, uint32_t fps_out) {
    create_elements(format_in, width_in, height_in, fps_in);
    link_elements(format_temp, width_out, height_out, fps_out);
}

void AppsrcV4l2jpegencAppsinkPipeline::create_elements(const char* format_in,
                                                       uint32_t width_in,
                                                       uint32_t height_in,
                                                       uint32_t fps_in) {
    /* create appsrc */
    appsrc = gst_element_factory_make("appsrc", "appsrc");
    if (!appsrc) {
        g_print("Failed to create element of type 'appsrc'\n");
        exit(-1);
    }

    g_object_set(appsrc, "max-buffers", 1, "block", FALSE, "emit-signals", FALSE, NULL);

    GstCaps* caps_appsrc_v4l2convert;
    caps_appsrc_v4l2convert = gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, format_in, "width", G_TYPE_INT, width_in,
        "height", G_TYPE_INT, height_in, "framerate", GST_TYPE_FRACTION, fps_in, 1, NULL);

    if (!caps_appsrc_v4l2convert) {
        g_print("Failed to create caps_appsrc_v4l2convert\n");
        exit(-1);
    }

    gst_app_src_set_caps(GST_APP_SRC_CAST(appsrc), caps_appsrc_v4l2convert);
    gst_caps_unref(caps_appsrc_v4l2convert);

    /* create v4l2convert */
    v4l2convert = gst_element_factory_make("v4l2convert", "v4l2convert");
    if (!v4l2convert) {
        g_print("Failed to create element 'v4l2convert'\n");
        exit(-1);
    }

    /* create v4l2jpegenc */
    v4l2jpegenc = gst_element_factory_make("v4l2jpegenc", "v4l2jpegenc");
    if (!v4l2jpegenc) {
        g_print("Failed to create element 'v4l2jpegenc'\n");
        exit(-1);
    }

    /* create appsink */
    appsink = gst_element_factory_make("appsink", "appsink");
    if (!appsink) {
        g_print("Failed to create element 'appsink'\n");
        exit(-1);
    }

    g_object_set(appsink, "max-buffers", 1, "drop", TRUE, "sync", FALSE, "emit-signals",
                 FALSE, NULL);

    /* create queues */
    for (int i = 0; i < queues.size(); i++) {
        queues[i] = gst_element_factory_make(
            "queue", (std::string("queue") + std::to_string(i)).c_str());
        if (!queues[i]) {
            g_print("Failed to create element queue%d'\n", i);
            exit(-1);
        }
    }

    /* create pipeline */
    pipeline = gst_pipeline_new("pipeline");

    /* add elements to the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), appsrc, queues[0], v4l2convert, queues[1],
                     v4l2jpegenc, appsink, NULL);
}

void AppsrcV4l2jpegencAppsinkPipeline::link_elements(const char* format_out,
                                                     uint32_t width_out,
                                                     uint32_t height_out,
                                                     uint32_t fps_out) {
    /* link */
    if (!gst_element_link_many(appsrc, queues[0], v4l2convert, NULL)) {
        g_warning("Failed to link appsrc and v4l2convert!");
        exit(-1);
    }

    /* link */
    GstCaps* caps_v4l2convert;
    caps_v4l2convert =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_out, "width",
                            G_TYPE_INT, width_out, "height", G_TYPE_INT, height_out,
                            "framerate", GST_TYPE_FRACTION, fps_out, 1, NULL);

    if (!caps_v4l2convert) {
        g_print("Failed to create caps_v4l2convert_v4l2jpegenc\n");
        exit(-1);
    }

    if (!gst_element_link_filtered(v4l2convert, queues[1], caps_v4l2convert)) {
        g_warning("Failed to link v4l2convert and queue1!");
        exit(-1);
    }

    gst_caps_unref(caps_v4l2convert);

    /* link */
    if (!gst_element_link_many(queues[1], v4l2jpegenc, appsink, NULL)) {
        g_warning("Failed to link queue1, v4l2jpegenc, and appsink!");
        exit(-1);
    }
}
}  // namespace gst_wrapper
