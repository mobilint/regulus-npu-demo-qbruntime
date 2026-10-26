#include "gst_capture_pipeline.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <stdexcept>

#include "gst/gstpipeline.h"
#include "gst/gstutils.h"
#include "gst/gstvalue.h"

namespace gst_wrapper {

std::string find_mapi_convert_element() {
    for (int n = 0; n < 64; ++n) {
        std::string path = "/dev/video" + std::to_string(n);
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        bool is_mapi =
            (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) &&
            std::string(reinterpret_cast<const char*>(cap.driver)) == "regulus-mapi";
        close(fd);
        if (is_mapi) return "v4l2video" + std::to_string(n) + "convert";
    }
    return "";
}

V4l2srcCapturePipeline::V4l2srcCapturePipeline(const char* cam_dev_path, uint32_t cam_w,
                                               uint32_t cam_h, uint32_t cam_fps,
                                               uint32_t sink_w, uint32_t sink_h,
                                               const char* sink_format) {
    create_elements(cam_dev_path);
    link_elements(cam_w, cam_h, cam_fps, sink_w, sink_h, sink_format);
}

void V4l2srcCapturePipeline::create_elements(const char* cam_dev_path) {
    v4l2src = gst_element_factory_make("v4l2src", "capsrc");
    jpegdec =
        gst_element_factory_make("v4l2jpegdec", "capjpegdec");  // CODAJ HW JPEG decode
    std::string conv_name = find_mapi_convert_element();  // MAPI node varies -> detect
    if (conv_name.empty()) {
        g_print("regulus-mapi V4L2 convert node not found (check MAPI driver)\n");
        throw std::runtime_error("regulus-mapi convert node not found");
    }
    mapiconvert = gst_element_factory_make(conv_name.c_str(), "capconvert");
    appsink = gst_element_factory_make("appsink", "capsink");
    if (!v4l2src || !jpegdec || !mapiconvert || !appsink) {
        g_print("Failed to create capture elements (v4l2src/v4l2jpegdec/mapi/appsink)\n");
        throw std::runtime_error("cannot create capture elements");
    }

    // Camera as MJPEG (1/10 the USB->memory DMA of raw, robust under bandwidth
    // contention).
    g_object_set(v4l2src, "device", cam_dev_path, "io-mode", 0 /*auto*/, NULL);
    g_object_set(jpegdec, "capture-io-mode", 4 /*dmabuf*/, NULL);
    g_object_set(mapiconvert, "output-io-mode", 5 /*dmabuf-import*/, NULL);
    // Buffer up to 8 RGB frames so v4l2src does not drop during downstream jitter.
    g_object_set(appsink, "max-buffers", 8, "drop", FALSE, "sync", FALSE, "emit-signals",
                 FALSE, NULL);

    pipeline = gst_pipeline_new("capture-pipeline");
    if (!pipeline) throw std::runtime_error("cannot create capture pipeline");
    gst_bin_add_many(GST_BIN(pipeline), v4l2src, jpegdec, mapiconvert, appsink, NULL);
}

void V4l2srcCapturePipeline::link_elements(uint32_t cam_w, uint32_t cam_h,
                                           uint32_t cam_fps, uint32_t sink_w,
                                           uint32_t sink_h, const char* sink_format) {
    // v4l2src -> jpegdec: fix camera input format (MJPEG cam_w x cam_h @ fps).
    GstCaps* src_caps = gst_caps_new_simple("image/jpeg", "width", G_TYPE_INT, cam_w,
                                            "height", G_TYPE_INT, cam_h, "framerate",
                                            GST_TYPE_FRACTION, cam_fps, 1, NULL);
    if (!gst_element_link_filtered(v4l2src, jpegdec, src_caps)) {
        g_print("Failed to link v4l2src -> v4l2jpegdec (MJPEG)\n");
        throw std::runtime_error("cannot link camera decoder");
    }
    gst_caps_unref(src_caps);

    // jpegdec -> mapiconvert: decoded frames (format/size negotiated by gst).
    if (!gst_element_link(jpegdec, mapiconvert)) {
        g_print("Failed to link v4l2jpegdec -> mapiconvert\n");
        throw std::runtime_error("cannot link decoder to converter");
    }

    // mapiconvert -> appsink: MAPI converts to sink_format and HW-scales to sink WxH.
    GstCaps* sink_caps =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, sink_format, "width",
                            G_TYPE_INT, sink_w, "height", G_TYPE_INT, sink_h, NULL);
    if (!gst_element_link_filtered(mapiconvert, appsink, sink_caps)) {
        g_print("Failed to link mapiconvert -> appsink\n");
        throw std::runtime_error("cannot link converter to appsink");
    }
    gst_caps_unref(sink_caps);
}

}  // namespace gst_wrapper
