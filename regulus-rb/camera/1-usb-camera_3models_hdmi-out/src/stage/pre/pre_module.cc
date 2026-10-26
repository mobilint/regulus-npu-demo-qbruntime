#include "pre_module.h"

#include <gst/gst.h>  // GstSample, gst_sample_unref

#include <cstdint>
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <stdexcept>

#include "dma350_copier.h"
#include "resize_crop.h"

namespace app {

// Preprocess: take a camera dmabuf frame from the capture stage and, on HW, DMA350-copy
// the center-cropped region into a CMA display buffer (disp wraps it directly, no CPU
// copy). Then copy that RGB frame into the shared pinned NPU input. Outputs the display
// frame to frame_out (plot) and a reposition packet to reposition_out (infer). No CPU
// fallback -- a failed DMA350 copy drops the frame.
std::thread pre_module(PreConfig cfg, Queue& capture_in, Queue& frame_out, Buffer& reposition_out) {
  return std::thread([cfg, &capture_in, &frame_out, &reposition_out]() {
    try {
      // Cover-scale + center-crop geometry. The capture source scaled cam WxH ->
      // scaled WxH (cover); the model window is npu WxH at (crop_x, crop_y) inside
      // that.
      const CoverCrop crop = cover_crop(cfg.cam_w, cfg.cam_h, cfg.npu_w, cfg.npu_h);

      // DMA350 HW copier: camera dmabuf -> CMA display buffer (its ring keeps disp
      // valid while plot/display use it). It crops as it copies, so the display
      // frame comes out packed at model size -- both halves of the crop are free,
      // and nothing downstream needs a stride.
      Dma350Copier dma(crop.scaled_w, crop.scaled_h, cfg.npu_w, cfg.npu_h, crop.crop_x, crop.crop_y, cfg.bpp);
      if (!dma.ok()) {
        std::cerr << "[pre] DMA350 initialization failed\n";
        cfg.run->stop(StopReason::Preprocess);
        return;
      }

      while (cfg.run->running()) {
        Packet captured;
        if (capture_in.pop(captured) == Queue::CLOSED) break;
        if (cfg.tl) cfg.tl->pre_in.addValue(age_ms(captured.time_point));
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample(static_cast<GstSample*>(captured.gst_sample),
                                                                       &gst_sample_unref);

        if (cfg.bc) cfg.bc->start();
        const Dma350Copier::Frame copied = dma.copy(captured.dmabuf_fd);
        const uint8_t* dma_out = copied.data;
        if (!dma_out) {
          std::cerr << "[pre] DMA350 copy failed (no fallback); dropping frame\n";
          if (cfg.drops) (*cfg.drops)++;
          if (cfg.bc) cfg.bc->end();
          cfg.run->stop(StopReason::Preprocess);
          break;
        }
        // The DMA350 already cropped, so this is the whole buffer -- packed, no
        // stride.
        cv::Mat disp(cfg.npu_h, cfg.npu_w, cfg.bpp == 4 ? CV_8UC4 : CV_8UC3, const_cast<uint8_t*>(dma_out));

        // NPU input: the model always takes 3 channels, so a 4-byte capture drops
        // its unused byte right here -- the copy this stage already made is where
        // it costs least.
        const bool input_written = cfg.mm->write_input([&](uint8_t* input) {
          Dma350Copier::CpuReadAccess cpu_read(copied.dmabuf_fd);
          if (!cpu_read.ok()) throw std::runtime_error("DMA350 CPU read START failed");
          cv::Mat pinned(cfg.npu_h, cfg.npu_w, CV_8UC3, input);
          if (cfg.bpp == 4)
            cv::cvtColor(disp, pinned, cv::COLOR_RGBA2RGB);
          else
            disp.copyTo(pinned);
          if (!cpu_read.finish()) throw std::runtime_error("DMA350 CPU read END failed");
        });
        if (!input_written) {
          break;
        }
        if (cfg.bc) cfg.bc->end();

        sample.reset();  // camera dmabuf consumed by the DMA350 copy

        Packet frame;
        frame.frame = disp;
        // plot composes this frame into its display buffer with the DMA350, so it
        // needs the buffer's dmabuf rather than disp.data. Valid until the
        // copier's ring reuses the slot (6 slots at ~30 fps, and plot holds a
        // frame for one iteration).
        frame.frame_dmabuf_fd = copied.dmabuf_fd;
        frame.time_point = captured.time_point;
        if (cfg.tl) cfg.tl->pre_out.addValue(age_ms(captured.time_point));
        frame_out.push(frame);
        if (cfg.tl) cfg.tl->pre_pushed.addValue(age_ms(captured.time_point));

        Packet input_ready;
        input_ready.time_point = captured.time_point;
        reposition_out.put(input_ready);
      }
    } catch (const std::exception& error) {
      std::cerr << "[pre] " << error.what() << "\n";
      cfg.run->stop(StopReason::Preprocess);
    } catch (...) {
      std::cerr << "[pre] unknown exception\n";
      cfg.run->stop(StopReason::Preprocess);
    }
  });
}

}  // namespace app
