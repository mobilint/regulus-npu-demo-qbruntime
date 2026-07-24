#ifndef APP_TRANSFER_H_
#define APP_TRANSFER_H_

#include <atomic>
#include <opencv2/opencv.hpp>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"

namespace app {
namespace transfer {
struct Module_config {
    int npu_w;
    int npu_h;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_in {
    Buffer& buffer;
};

struct Module_out {
    Queue& queue;
};
}  // namespace transfer
std::thread module_transfer(transfer::Module_config&& conf, transfer::Module_in&& in,
                            transfer::Module_out&& out);
}  // namespace app

#endif
