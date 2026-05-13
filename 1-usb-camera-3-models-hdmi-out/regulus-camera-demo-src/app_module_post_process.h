#ifndef APP_POST_PROCESS_H_
#define APP_POST_PROCESS_H_

#include <atomic>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"

namespace app {
namespace postprocess {
struct Module_config {
    int npu_h;
    int npu_w;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_in {
    Queue& queue_in;
};

struct Module_out {
    Queue& queue_out;
};
}  // namespace postprocess

std::thread module_post_process_seg_lcd(postprocess::Module_config&& conf,
                                        postprocess::Module_in&& in,
                                        postprocess::Module_out&& out);
std::thread module_post_process_face_lcd(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out);
std::thread module_post_process_od_lcd(postprocess::Module_config&& conf,
                                       postprocess::Module_in&& in,
                                       postprocess::Module_out&& out);
std::thread module_post_process_pose_lcd(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out);

std::thread module_post_process_seg_hdmi(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out);
std::thread module_post_process_face_hdmi(postprocess::Module_config&& conf,
                                          postprocess::Module_in&& in,
                                          postprocess::Module_out&& out);
std::thread module_post_process_od_hdmi(postprocess::Module_config&& conf,
                                        postprocess::Module_in&& in,
                                        postprocess::Module_out&& out);
std::thread module_post_process_pose_hdmi(postprocess::Module_config&& conf,
                                          postprocess::Module_in&& in,
                                          postprocess::Module_out&& out);
}  // namespace app

#endif
