#ifndef APP_CAPTURE_AND_PREPROCESS_H_
#define APP_CAPTURE_AND_PREPROCESS_H_

#include <qbruntime/model.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"

namespace app {
namespace capture_and_preprocess {
struct Module_config {
    std::string camera_dev_path;
    uint32_t camera_width;
    uint32_t camera_height;
    uint32_t camera_fps;
    int npu_w;
    int npu_h;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_config_rep {
    std::string camera_dev_path;
    uint32_t camera_width;
    uint32_t camera_height;
    uint32_t camera_fps;
    int npu_w;
    int npu_h;
    mobilint::Model* model;
    std::vector<mobilint::Buffer>* repositioned_buffer;
    std::atomic<bool>& push_on;
};

struct Module_out {
    Buffer& buffer;
};
}  // namespace capture_and_preprocess

std::thread module_opencv_capture_and_preprocess(
    capture_and_preprocess::Module_config&& module_conf,
    capture_and_preprocess::Module_out&& module_out);

std::thread module_opencv_capture_and_preprocess_with_reposition(
    capture_and_preprocess::Module_config_rep&& module_conf,
    capture_and_preprocess::Module_out&& module_out);

std::thread module_gstreamer_mipi_capture_and_preprocess(
    capture_and_preprocess::Module_config&& module_conf,
    capture_and_preprocess::Module_out&& module_out);

std::thread module_gstreamer_mipi_capture_and_preprocess_with_reposition(
    capture_and_preprocess::Module_config_rep&& module_conf,
    capture_and_preprocess::Module_out&& module_out);
}  // namespace app

#endif
