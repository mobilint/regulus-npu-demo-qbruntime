#ifndef APP_DISPLAY_H_
#define APP_DISPLAY_H_

#include <atomic>
#include <opencv2/opencv.hpp>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "recent_values.h"

namespace app {
namespace display {
struct Module_config_kmssink {
    const char* format;
    int width;
    int height;
    int fps;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_config_udp {
    const char* format;
    int width;
    int height;
    int fps;
    const char* ip;
    int port;
    int bandwidth;
    std::atomic<bool>& push_on;
};

struct Module_in {
    Queue& queue;
};

struct Module_out {
    Benchmarker* bc_frame = nullptr;
    RecentValues<double, 1000>* latencies_ms = nullptr;
};
}  // namespace display

std::thread module_kmssink_display(display::Module_config_kmssink&& conf,
                                   display::Module_in&& in, display::Module_out&& out);

std::thread module_udp_display(display::Module_config_udp&& conf,
                               display::Module_in&& in);
}  // namespace app

#endif
