#include "infer_module.h"

#include <iostream>

namespace app {

std::thread infer_module(InferConfig cfg, Buffer& in, Queue& out) {
    return std::thread([cfg, &in, &out]() {
        try {
            int64_t index = 0;
            while (cfg.run->running()) {
                Packet p;
                if (in.get(p, index) == Buffer::CLOSED) break;
                if (cfg.tl) cfg.tl->det_infer_in.addValue(age_ms(p.time_point));

                if (cfg.bc) cfg.bc->start();
                std::vector<RawTensor> raw;
                StatusCode sc = cfg.mm->infer(cfg.model_idx, raw);
                if (!sc) {
                    std::cerr << "[infer:" << cfg.branch << "] inference failed\n";
                    cfg.run->stop(StopReason::Inference);
                    break;
                }
                if (cfg.bc) cfg.bc->end();
                if (cfg.activity) cfg.activity->fetch_add(1, std::memory_order_relaxed);
                if (cfg.tl) cfg.tl->det_infer_out.addValue(age_ms(p.time_point));

                Packet o;
                o.raw = std::move(raw);
                o.time_point = p.time_point;
                out.push(std::move(o));
                if (cfg.tl) cfg.tl->det_infer_pushed.addValue(age_ms(p.time_point));
            }
        } catch (const std::exception& error) {
            std::cerr << "[infer:" << cfg.branch << "] " << error.what() << "\n";
            cfg.run->stop(StopReason::Inference);
        } catch (...) {
            std::cerr << "[infer:" << cfg.branch << "] unknown exception\n";
            cfg.run->stop(StopReason::Inference);
        }
    });
}

}  // namespace app
