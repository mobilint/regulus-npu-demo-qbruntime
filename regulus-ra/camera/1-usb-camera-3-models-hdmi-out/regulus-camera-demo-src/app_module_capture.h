#ifndef APP_CAPTURE_H_
#define APP_CAPTURE_H_

#include <qbruntime/model.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>

#include "app_maccel.h"
#include "app_type.h"
#include "benchmarker.h"

namespace app {
namespace capture {
struct Module_config {
    std::string camera_dev_path;
    uint32_t camera_width;
    uint32_t camera_height;
    uint32_t camera_fps;
    int npu_w;
    int npu_h;
    MaccelRepositionBuffer* reposition_buffer = nullptr;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_out {
    Buffer& buffer;
};
}  // namespace capture

std::thread module_opencv_capture_fb(capture::Module_config&& module_conf,
                                     capture::Module_out&& module_out);
std::thread module_opencv_capture_kmssink(capture::Module_config&& module_conf,
                                          capture::Module_out&& module_out);
std::thread module_gstreamer_mipi_capture_fb(capture::Module_config&& module_conf,
                                             capture::Module_out&& module_out);
std::thread module_gstreamer_mipi_capture_kmssink(capture::Module_config&& module_conf,
                                                  capture::Module_out&& module_out);
}  // namespace app

#endif
