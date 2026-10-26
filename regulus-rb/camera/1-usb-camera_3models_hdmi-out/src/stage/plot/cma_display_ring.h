#ifndef STAGE_PLOT_CMA_DISPLAY_RING_H_
#define STAGE_PLOT_CMA_DISPLAY_RING_H_

#include <cstddef>
#include <opencv2/opencv.hpp>
#include <vector>

namespace app {

// A ring of dma-heap(CMA) RGBX buffers used as the display frames. plot composites into one with
// the CPU and hands its dmabuf fd to the display stage, which pushes it to kmssink for zero-copy
// scanout -- no 6MB kmssink copy. begin_cpu/end_cpu wrap CPU access with dma-buf cache sync.
// ok() is false if dma-heap is unavailable.
//
// RGBX, 4 bytes per pixel, not 3: the DMA350 rounds its destination address (x * bpp) down to 16
// bytes, and 16 is not a multiple of 3, so at 3 bytes a paste can land between pixels and rotate
// R/G/B. At 4 bytes 16/4 = 4, so any x that is a multiple of 4 lands exactly.
class CmaDisplayRing {
 public:
  CmaDisplayRing(int w, int h, int count = 4);
  ~CmaDisplayRing();

  bool ok() const { return ok_; }
  int count() const { return count_; }
  cv::Mat& mat(int i) { return mats_[i]; }  // wraps buffer i for compositing
  int fd(int i) const { return fds_[i]; }   // dmabuf fd of buffer i (for scanout)

  void begin_cpu(int i);  // before CPU writes (dma-buf sync start, RW)
  void end_cpu(int i);    // after CPU writes (dma-buf sync end, RW -> flush for HW read)

 private:
  int w_ = 0, h_ = 0, count_ = 0;
  size_t bytes_ = 0;
  bool ok_ = false;
  std::vector<int> fds_;
  std::vector<void*> maps_;
  std::vector<cv::Mat> mats_;
};

}  // namespace app

#endif  // STAGE_PLOT_CMA_DISPLAY_RING_H_
