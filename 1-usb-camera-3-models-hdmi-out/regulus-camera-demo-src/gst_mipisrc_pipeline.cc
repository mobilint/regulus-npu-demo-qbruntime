#include "gst_mipisrc_pipeline.h"

#include <cstdint>

#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "gst/gstutils.h"
#include "gst/gstvalue.h"

namespace gst_wrapper {
MipiSrcAppsinkPipeline::MipiSrcAppsinkPipeline(
    const char* mipi_dev_path, const char* camera_format, uint32_t camera_width,
    uint32_t camera_height, uint32_t camera_fps, const char* sink_format,
    uint32_t sink_width, uint32_t sink_height, uint32_t sink_fps) {
    create_elements(mipi_dev_path);
    link_elements(camera_format, camera_width, camera_height, camera_fps, sink_format,
                  sink_width, sink_height, sink_fps);
}

void MipiSrcAppsinkPipeline::create_elements(const char* mipi_dev_path) {
    /* create v4l2src */
    v4l2src = gst_element_factory_make("v4l2src", "v4l2src");
    if (!v4l2src) {
        g_print("Failed to create element of type 'v4l2src'\n");
        exit(-1);
    }

    // LCD
    // GstStructure* extra_controls = gst_structure_new(
    //     "extra-controls", "ipi_mode", G_TYPE_INT, 1, "hline", G_TYPE_INT, 11852, "hsa",
    //     G_TYPE_INT, 10, "hbp", G_TYPE_INT, 10, "hsd", G_TYPE_INT, 10, "vact",
    //     G_TYPE_INT, 2160, "vsa", G_TYPE_INT, 10, "vbp", G_TYPE_INT, 15, "vfp",
    //     G_TYPE_INT, 15, "ipi2dvp_hact", G_TYPE_INT, 3840, "ipi2dvp_hblank", G_TYPE_INT,
    //     30, "ipi2dvp_vact", G_TYPE_INT, 2160, "ipi2dvp_vsa", G_TYPE_INT, 296300,
    //     "ipi2dvp_vbp", G_TYPE_INT, 15, "ipi2dvp_vfp", G_TYPE_INT, 15,
    //     "image_subsampling", G_TYPE_INT, 1, NULL);

    // HDMI
    GstStructure* extra_controls = gst_structure_new(
        "extra-controls", "ipi_mode", G_TYPE_INT, 1, "hline", G_TYPE_INT, 11852, "hsa",
        G_TYPE_INT, 10, "hbp", G_TYPE_INT, 10, "hsd", G_TYPE_INT, 10, "vact", G_TYPE_INT,
        2160, "vsa", G_TYPE_INT, 10, "vbp", G_TYPE_INT, 15, "vfp", G_TYPE_INT, 15,
        "ipi2dvp_hact", G_TYPE_INT, 3840, "ipi2dvp_hblank", G_TYPE_INT, 30,
        "ipi2dvp_vact", G_TYPE_INT, 2160, "ipi2dvp_vsa", G_TYPE_INT, 296300,
        "ipi2dvp_vbp", G_TYPE_INT, 15, "ipi2dvp_vfp", G_TYPE_INT, 15, "image_subsampling",
        G_TYPE_INT, 1, "mipi_buffer_dummy_bytes", G_TYPE_INT, 3840, NULL);

    g_object_set(G_OBJECT(v4l2src), "device", mipi_dev_path, "io-mode", 4,
                 "extra-controls", extra_controls, NULL);

    gst_structure_free(extra_controls);

    videorate = gst_element_factory_make("videorate", "videorate");
    if (!videorate) {
        g_print("Failed to create element 'videorate'\n");
        exit(-1);
    }

    // v4l2convert = gst_element_factory_make("v4l2convert", "v4l2convert");
    v4l2convert = gst_element_factory_make("videoconvertscale", "videoconvertscale");
    if (!v4l2convert) {
        g_print("Failed to create element 'v4l2convert'\n");
        exit(-1);
    }

    // g_object_set(G_OBJECT(v4l2convert), "output-io-mode", 5, "capture-io-mode", 4,
    // NULL);

    /* create appsink */
    appsink = gst_element_factory_make("appsink", "appsink");
    if (!appsink) {
        g_print("Failed to create element 'appsink'\n");
        exit(-1);
    }

    g_object_set(appsink, "max-buffers", 1, "drop", TRUE, "sync", FALSE, "emit-signals",
                 FALSE, NULL);

    /* create pipeline */
    pipeline = gst_pipeline_new("pipeline");

    /* add elements to the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), v4l2src, videorate, v4l2convert, appsink, NULL);
}

void MipiSrcAppsinkPipeline::link_elements(const char* camera_format,
                                           uint32_t camera_width, uint32_t camera_height,
                                           uint32_t camera_fps, const char* sink_format,
                                           uint32_t sink_width, uint32_t sink_height,
                                           uint32_t sink_fps) {
    // link v4l2src, videorate
    if (!gst_element_link(v4l2src, videorate)) {
        g_warning("Failed to link v4l2src and videorate!");
        exit(-1);
    }

    /* link videorate, v4l2convert */
    GstCaps* caps_videorate_v4l2convert;
    caps_videorate_v4l2convert = gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, camera_format, "width", G_TYPE_INT,
        camera_width / 4, "height", G_TYPE_INT, camera_height / 4, "framerate",
        GST_TYPE_FRACTION, camera_fps, 1, NULL);

    if (!caps_videorate_v4l2convert) {
        g_print("Failed to create caps_videorate_v4l2convert\n");
        exit(-1);
    }

    if (!gst_element_link_filtered(videorate, v4l2convert, caps_videorate_v4l2convert)) {
        g_warning("Failed to link videorate and v4l2convert!");
        exit(-1);
    }

    gst_caps_unref(caps_videorate_v4l2convert);

    /* link v4l2src, v4l2convert */
    // GstCaps* caps_v4l2src_v4l2convert;
    // caps_v4l2src_v4l2convert = gst_caps_new_simple(
    //     "video/x-raw", "format", G_TYPE_STRING, camera_format, "width", G_TYPE_INT,
    //     camera_width / 4, "height", G_TYPE_INT, camera_height / 4, "framerate",
    //     GST_TYPE_FRACTION, camera_fps, 1, NULL);

    // if (!caps_v4l2src_v4l2convert) {
    //     g_print("Failed to create caps_v4l2src_v4l2convert\n");
    //     exit(-1);
    // }

    // if (!gst_element_link_filtered(v4l2src, v4l2convert, caps_v4l2src_v4l2convert)) {
    //     g_warning("Failed to link v4l2src and v4l2convert!");
    //     exit(-1);
    // }

    // gst_caps_unref(caps_v4l2src_v4l2convert);

    /* link v4l2convert, appsink */
    GstCaps* caps_v4l2convert_appsink;
    caps_v4l2convert_appsink =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, sink_format, "width",
                            G_TYPE_INT, sink_width, "height", G_TYPE_INT, sink_height,
                            // "framerate", GST_TYPE_FRACTION, sink_fps, 1,
                            NULL);

    if (!caps_v4l2convert_appsink) {
        g_print("Failed to create caps_v4l2convert1_appsink1\n");
        exit(-1);
    }

    if (!gst_element_link_filtered(v4l2convert, appsink, caps_v4l2convert_appsink)) {
        g_warning("Failed to link v4l2convert1 and appsink1!");
        exit(-1);
    }

    gst_caps_unref(caps_v4l2convert_appsink);
}
}  // namespace gst_wrapper
