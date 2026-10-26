#include "capture_module.h"

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <chrono>
#include <iostream>

namespace app {

std::thread capture_module(CaptureConfig cfg, std::unique_ptr<CaptureSource> src,
                           Queue& out) {
    return std::thread([cfg, src = std::move(src), &out]() mutable {
        try {
            src->start();
            std::cout << "capture stage started\n";

            while (cfg.run->running()) {
                // Blocking wait for the next frame -- deliberately NOT timed (it is
                // camera pacing, not a cost we can optimize). "Time capture" starts only
                // once a frame is in hand.
                GstSample* sample = gst_app_sink_try_pull_sample(
                    GST_APP_SINK(src->appsink()), 500 * GST_MSECOND);
                if (!sample) continue;  // Timeout: recheck the run state.

                // "Camera in": interval between arriving frames. Measured here, around
                // the blocking pull, because that wait IS the camera's pacing -- the
                // number this produces is the camera's real delivery rate, not a cost of
                // ours.
                if (cfg.frame_bc) {
                    cfg.frame_bc->end();
                    cfg.frame_bc->start();
                }

                if (cfg.bc) cfg.bc->start();
                const auto t0 = std::chrono::steady_clock::now();

                GstBuffer* buf = gst_sample_get_buffer(sample);
                GstMemory* mem = buf ? gst_buffer_peek_memory(buf, 0) : nullptr;
                if (!mem || !gst_is_dmabuf_memory(mem)) {
                    std::cerr << "[capture] frame is not dmabuf-backed; dropping (no "
                                 "fallback)\n";
                    gst_sample_unref(sample);
                    if (cfg.drops) (*cfg.drops)++;
                    if (cfg.bc) cfg.bc->end();
                    cfg.run->stop(StopReason::Capture);
                    break;
                }

                Packet p;
                p.dmabuf_fd = gst_dmabuf_memory_get_fd(
                    mem);               // camera dmabuf, consumed by pre's DMA350
                p.gst_sample = sample;  // pre unrefs after use
                p.time_point = t0;
                if (cfg.bc)
                    cfg.bc->end();  // receipt cost ends here; the push below may block
                                    // (backpressure)
                if (cfg.tl) cfg.tl->capture_out.addValue(age_ms(t0));

                if (out.push(std::move(p)) == Queue::CLOSED) {
                    gst_sample_unref(sample);
                    break;
                }
                if (cfg.frames) cfg.frames->fetch_add(1, std::memory_order_relaxed);
                if (cfg.tl) cfg.tl->capture_pushed.addValue(age_ms(t0));
            }
            src->stop();
        } catch (const std::exception& error) {
            std::cerr << "[capture] " << error.what() << "\n";
            src->stop();
            cfg.run->stop(StopReason::Capture);
        } catch (...) {
            std::cerr << "[capture] unknown exception\n";
            src->stop();
            cfg.run->stop(StopReason::Capture);
        }
    });
}

}  // namespace app
