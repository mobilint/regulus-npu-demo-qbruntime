#ifndef APP_INFER_H_
#define APP_INFER_H_

#include <qbruntime/model.h>

#include <atomic>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "tsqueue.h"

namespace app {
namespace infer {
struct Module_config {
    mobilint::Model* model;
    std::atomic<bool>& push_on;
    Benchmarker* bc = nullptr;
};

struct Module_in {
    Buffer& buffer;
};

struct Module_out {
    Queue& queue;
};
}  // namespace infer

std::thread module_infer(infer::Module_config&& conf, infer::Module_in&& in,
                         infer::Module_out&& out);
std::thread module_infer_reposition_buffer(infer::Module_config&& conf,
                                           infer::Module_in&& in,
                                           infer::Module_out&& out);
}  // namespace app

#endif
