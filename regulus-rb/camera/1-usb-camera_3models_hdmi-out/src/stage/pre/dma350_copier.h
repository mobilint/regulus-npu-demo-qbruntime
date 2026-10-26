#ifndef STAGE_PRE_DMA350_COPIER_H_
#define STAGE_PRE_DMA350_COPIER_H_

#include <cstddef>
#include <cstdint>

namespace app {

// DMA350 V4L2 M2M copier. Copies the camera dmabuf -> a dma-heap(CMA) buffer in hardware, replacing
// the CPU clone (camera->heap). The output CMA buffer is CPU-readable (cache-synced), so it serves
// as the display frame directly. A ring of output buffers lets plot hold a frame while pre fills the
// next. If setup fails, ok() is false and copy() returns nullptr (caller falls back to CPU).
//
// The copy takes only the crop window (V4L2_SEL_TGT_CROP on the source), so the destination comes
// out packed at the model size. Measured on the LCD demo, where the window is 384x480 of a 640x480
// frame: the copy itself moves 0.6x the bytes (3.71 -> 2.2 ms), and because the result is packed
// rather than a 384-wide view of a 640-wide buffer, the RGBA->RGB convert that follows it in pre
// stops paying a stride penalty (7.99 -> ~5.2 ms). A byte offset alone could only skip whole
// rows, which is why the horizontal half of the crop used to be left to a strided cv::Mat.
class Dma350Copier {
 public:
  // One copied frame: the same CMA buffer seen two ways. `data` is for the CPU, `dmabuf_fd` is
  // for other hardware (plot's DMA350 compose reads the frame straight out of this buffer, so it
  // needs the fd rather than an address). Both stay valid until the ring reuses the slot.
  struct Frame {
    const uint8_t* data = nullptr;
    int dmabuf_fd = -1;
  };

  // src_w/src_h is the camera frame as it arrives; dst_w/dst_h at (crop_x, crop_y) is the
  // window to keep, and also the size of every ring buffer. bpp is bytes per pixel: 3 for RGB24,
  // 4 for RGBX (R,G,B,unused). The LCD panel only scans out 4-byte formats, so that demo runs the
  // whole path at 4.
  Dma350Copier(int src_w, int src_h, int dst_w, int dst_h, int crop_x, int crop_y, int bpp = 3, int ring = 6);
  ~Dma350Copier();

  bool ok() const { return ok_; }

  // HW-copy the crop window out of camera_dmabuf_fd into the next ring output buffer and
  // cache-sync it for CPU read. Returns a default (data == nullptr) Frame on failure.
  Frame copy(int camera_dmabuf_fd);

 private:
  static const int kMaxRing = 8;
  int fd_ = -1;
  int ring_ = 0, next_ = 0;
  int bpp_ = 3;
  size_t src_bytes_ = 0, dst_bytes_ = 0;
  int out_fd_[kMaxRing] = {};     // dma-heap CMA dmabuf fds (output)
  void* out_map_[kMaxRing] = {};  // their CPU mmaps
  bool ok_ = false;
  bool streaming_ = false;
};

}  // namespace app

#endif  // STAGE_PRE_DMA350_COPIER_H_
