#ifndef INFER_H_
#define INFER_H_

#include "app_module_infer.h"

#include <qbruntime/model.h>
#include <qbruntime/type.h>

#include <atomic>

#include "app_type.h"
#include "tsqueue.h"

namespace app {
std::thread module_infer(infer::Module_config&& conf, infer::Module_in&& in,
                         infer::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            int64_t index = 0;
            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.buffer.get(data_in, index);
                if (qsc == Buffer::StatusCode::CLOSED) {
                    std::cout << "buffer closed.\n";
                    exit(1);
                }

                if (conf.bc) conf.bc->start();
                std::vector<std::vector<float>> npu_result;
                mobilint::StatusCode sc =
                    conf.model->infer({(float*)data_in.pre.data}, npu_result);
                if (!sc) {
                    std::cout << "infer failed.\n";
                    exit(1);
                }
                if (conf.bc) conf.bc->end();

                out.queue.push(
                    {data_in.frame.clone(), std::move(npu_result), data_in.time_point});
            }
        });
}

std::thread module_infer_reposition_buffer(infer::Module_config&& conf,
                                           infer::Module_in&& in,
                                           infer::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            int64_t index = 0;
            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.buffer.get(data_in, index);
                if (qsc == Buffer::StatusCode::CLOSED) {
                    std::cout << "buffer closed.\n";
                    exit(1);
                }

                if (conf.bc) conf.bc->start();
                std::vector<std::vector<float>> npu_result;
                mobilint::StatusCode sc = conf.model->inferBufferToFloat(
                    data_in.reposition_buffer->buf, npu_result);
                if (!sc) {
                    std::cout << "infer failed.\n";
                    exit(1);
                }
                if (conf.bc) conf.bc->end();

                out.queue.push(
                    {data_in.frame.clone(), std::move(npu_result), data_in.time_point});
            }
        });
}
}  // namespace app

#endif
