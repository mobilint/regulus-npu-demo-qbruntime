#include "capture_module.h"

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace app {
namespace {
bool stop_on_bus_message(GstBus* bus, RunState& run) {
  std::unique_ptr<GstMessage, decltype(&gst_message_unref)> message(
      gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)), &gst_message_unref);
  if (!message) return false;
  if (!run.running()) return true;
  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
    GError* error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message.get(), &error, &debug);
    const auto* source = GST_MESSAGE_SRC(message.get());
    std::cerr << "[capture] pipeline error from " << (source ? GST_OBJECT_NAME(source) : "unknown") << ": "
              << (error ? error->message : "unknown error") << '\n';
    if (debug) std::cerr << "[capture] " << debug << '\n';
    g_clear_error(&error);
    g_free(debug);
  } else {
    std::cerr << "[capture] input stream reached EOS\n";
  }
  run.stop(StopReason::Capture);
  return true;
}
}  // namespace

std::thread capture_module(CaptureConfig cfg, std::unique_ptr<CaptureSource> src, Queue& out) {
  return std::thread([cfg, src = std::move(src), &out]() mutable {
    try {
      std::unique_ptr<GstBus, decltype(&gst_object_unref)> bus(src->bus(), &gst_object_unref);
      if (!bus) throw std::runtime_error("Capture pipeline has no bus");
      src->start();
      std::cout << "capture stage started\n";

      while (cfg.run->running()) {
        if (stop_on_bus_message(bus.get(), *cfg.run)) break;
        // Blocking wait for the next frame -- deliberately NOT timed (it is
        // camera pacing, not a cost we can optimize). "Time capture" starts only
        // once a frame is in hand.
        GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(src->appsink()), 500 * GST_MSECOND);
        if (!sample) {
          if (!cfg.run->running()) break;  // Normal shutdown, not a capture failure.
          if (stop_on_bus_message(bus.get(), *cfg.run)) break;
          GstState state = GST_STATE_NULL;
          const auto change = gst_element_get_state(src->appsink(), &state, nullptr, 0);
          if (change == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "[capture] appsink state change failed\n";
            cfg.run->stop(StopReason::Capture);
            break;
          }
          // is_eos() is also true before appsink starts. Do not mistake startup for EOS.
          if (state >= GST_STATE_PAUSED && gst_app_sink_is_eos(GST_APP_SINK(src->appsink()))) {
            std::cerr << "[capture] appsink reached EOS\n";
            cfg.run->stop(StopReason::Capture);
            break;
          }
          if (state < GST_STATE_PAUSED) std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;  // A real timeout or a pending asynchronous start.
        }

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
        p.dmabuf_fd = gst_dmabuf_memory_get_fd(mem);  // camera dmabuf, consumed by pre's DMA350
        p.gst_sample = sample;                        // pre unrefs after use
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
