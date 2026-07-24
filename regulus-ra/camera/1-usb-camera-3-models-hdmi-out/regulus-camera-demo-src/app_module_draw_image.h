#ifndef APP_DRAW_IMAGE_H
#define APP_DRAW_IMAGE_H

#include <atomic>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "recent_values.h"

namespace app {
namespace draw_image {
struct Module_config {
    std::string format;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_in_3_models {
    Queue& queue_in_org;
    Queue& queue_in_face;
    Queue& queue_in_pose;
    Queue& queue_in_od;
};

struct Module_in_4_models {
    Queue& queue_in_seg;
    Queue& queue_in_face;
    Queue& queue_in_pose;
    Queue& queue_in_od;
};

struct Module_out {
    Queue& queue_out;
};

struct Module_out_fb {
    Benchmarker* bc_frame = nullptr;
    RecentValues<double, 1000>* latencies_ms = nullptr;
};
}  // namespace draw_image

std::thread module_draw_image_3_models_lcd_fb(draw_image::Module_config&& conf,
                                              draw_image::Module_in_3_models&& in,
                                              draw_image::Module_out_fb&& out);

std::thread module_draw_image_3_models_hdmi(draw_image::Module_config&& conf,
                                            draw_image::Module_in_3_models&& in,
                                            draw_image::Module_out&& out);

std::thread module_draw_image_3_models_hdmi_fb(draw_image::Module_config&& conf,
                                               draw_image::Module_in_3_models&& in,
                                               draw_image::Module_out_fb&& out);

std::thread module_draw_image_4_models_hdmi(draw_image::Module_config&& conf,
                                            draw_image::Module_in_4_models&& in,
                                            draw_image::Module_out&& out);
}  // namespace app

#endif
