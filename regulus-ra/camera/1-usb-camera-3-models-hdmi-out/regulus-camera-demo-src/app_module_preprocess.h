#ifndef APP_PREPROCESS_H_
#define APP_PREPROCESS_H_

#include <qbruntime/model.h>

#include <atomic>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "tsqueue.h"

namespace app {
namespace preprocess {
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
    Buffer& buffer;
    Queue& queue;
};
struct Module_out2 {
    Buffer& buffer;
};
}  // namespace preprocess

std::thread module_preprocess_fb(preprocess::Module_config&& conf,
                                 preprocess::Module_in&& in,
                                 preprocess::Module_out&& out);
std::thread module_preprocess_and_reposition_fb(preprocess::Module_config&& conf,
                                                preprocess::Module_in&& in,
                                                preprocess::Module_out&& out);
std::thread module_preprocess_and_reposition_fb(preprocess::Module_config&& conf,
                                                preprocess::Module_in&& in,
                                                preprocess::Module_out2&& out);

std::thread module_preprocess_kmssink(preprocess::Module_config&& conf,
                                      preprocess::Module_in&& in,
                                      preprocess::Module_out&& out);
std::thread module_preprocess_and_reposition_kmssink(preprocess::Module_config&& conf,
                                                     preprocess::Module_in&& in,
                                                     preprocess::Module_out&& out);
std::thread module_preprocess_and_reposition_kmssink(preprocess::Module_config&& conf,
                                                     preprocess::Module_in&& in,
                                                     preprocess::Module_out2&& out);
}  // namespace app

#endif
