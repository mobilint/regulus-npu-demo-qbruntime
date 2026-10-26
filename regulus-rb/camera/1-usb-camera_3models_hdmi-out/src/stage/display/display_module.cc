#include "display_module.h"

#include <chrono>
#include <iostream>

#include "gst_base_pipeline.h"
#include "gst_kmssink_pipeline.h"

namespace app {
namespace {

struct SinkProbeContext {
  DisplayConfig cfg;
  uint64_t last_sequence = 0;
};

GstPadProbeReturn on_kmssink_buffer(GstPad*, GstPadProbeInfo* info, gpointer user_data) {
  auto* context = static_cast<SinkProbeContext*>(user_data);
  GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buffer) return GST_PAD_PROBE_OK;

  const uint64_t sequence = GST_BUFFER_OFFSET(buffer);
  if (sequence != GST_BUFFER_OFFSET_NONE && context->cfg.drops && sequence > context->last_sequence + 1)
    context->cfg.drops->fetch_add(sequence - context->last_sequence - 1, std::memory_order_relaxed);
  if (sequence != GST_BUFFER_OFFSET_NONE) context->last_sequence = sequence;

  if (context->cfg.frame_bc) {
    context->cfg.frame_bc->end();
    context->cfg.frame_bc->start();
  }
  if (context->cfg.frames) context->cfg.frames->fetch_add(1, std::memory_order_relaxed);

  const GstClockTime timestamp_ns = GST_BUFFER_PTS(buffer);
  if (context->cfg.latencies && GST_CLOCK_TIME_IS_VALID(timestamp_ns)) {
    const auto timestamp =
        SteadyTime{std::chrono::duration_cast<SteadyTime::duration>(std::chrono::nanoseconds(timestamp_ns))};
    context->cfg.latencies->addValue(
        std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - timestamp}.count());
  }
  return GST_PAD_PROBE_OK;
}

}  // namespace

std::thread display_module(DisplayConfig cfg, Queue& in) {
  return std::thread([cfg, &in]() {
    try {
      SinkProbeContext probe_context{cfg};
      gst_wrapper::AppsrcKmssinkPipeline pipeline(cfg.format, cfg.width, cfg.height, cfg.fps);
      pipeline.add_sink_probe(on_kmssink_buffer, &probe_context);
      pipeline.start();
      uint64_t sequence = 0;

      while (cfg.run->running()) {
        Packet p;
        if (in.pop(p) == Queue::CLOSED) break;
        if (cfg.tl) cfg.tl->display_in.addValue(age_ms(p.time_point));

        if (cfg.bc) cfg.bc->start();
        const uint64_t timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(p.time_point.time_since_epoch()).count();
        ++sequence;
        const bool submitted =
            p.dmabuf_fd >= 0 ? gst_wrapper::push_dmabuf_to_appsrc(pipeline.appsrc, p.dmabuf_fd,
                                                                  cfg.width * cfg.height * 4, timestamp_ns, sequence)
                             : gst_wrapper::push_data_to_appsrc(pipeline.appsrc, p.frame.data,
                                                                cfg.width * cfg.height * 4, timestamp_ns, sequence);
        if (!submitted) {
          std::cerr << "[display] appsrc rejected the frame\n";
          cfg.run->stop(StopReason::Display);
          break;
        }
        if (cfg.bc) cfg.bc->end();
      }

      pipeline.stop();
      pipeline.remove_sink_probe();
    } catch (const std::exception& error) {
      std::cerr << "[display] " << error.what() << "\n";
      cfg.run->stop(StopReason::Display);
    } catch (...) {
      std::cerr << "[display] unknown exception\n";
      cfg.run->stop(StopReason::Display);
    }
  });
}

}  // namespace app
