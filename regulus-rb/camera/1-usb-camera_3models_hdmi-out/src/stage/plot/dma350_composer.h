#ifndef STAGE_PLOT_DMA350_COMPOSER_H_
#define STAGE_PLOT_DMA350_COMPOSER_H_

#include <cstddef>

namespace app {

// DMA350 V4L2 M2M compositor: writes a small source frame into a rectangle of a large
// destination, in hardware. This replaces plot's four CPU panel copies (measured 24.2 ms of CPU on
// two cores -> 3.5 ms of DMA350 with the CPU free).
//
// Both queues are V4L2_MEMORY_DMABUF: the source is the frame buffer pre's Dma350Copier produced
// and the destination is a CmaDisplayRing buffer, so nothing is copied in or out by the CPU.
// The rectangle is set per job with VIDIOC_S_SELECTION / V4L2_SEL_TGT_COMPOSE, and only that
// rectangle is written -- the rest of the destination (plot's background) is left alone.
//
// Separate from Dma350Copier on purpose: that one copies a whole buffer into its own CMA ring,
// this one writes a sub-rectangle of a caller-owned destination. Both open the dma350 node
// independently; two contexts on the one engine were measured to coexist (4 composes went 3.04 ->
// 3.54 ms while pre was also copying, with no frames dropped).
//
// IMPORTANT, measured on the board: the driver rounds the destination *byte* address (x * bpp)
// down to 16 bytes. At 3 bytes per pixel 16 is not a multiple of 3, so a misaligned x breaks the
// pixel boundary and R/G/B come out rotated -- exact only at x mod 16 in {0, 2, 6, 10, 14}. At 4
// bytes 16/4 = 4, so the rounding can only shift the paste whole pixels; exact when x is a multiple
// of 4. 4 bytes also measured 2.75x faster for the same four pastes (13.3 -> 4.7 ms).
//
// S_SELECTION reports left/top rounded down to a multiple of 2; the 16-byte rounding is not
// reported, so compose() cannot detect it.
class Dma350Composer {
   public:
    // src_w/src_h describe the WHOLE source buffer, dst_w/dst_h the whole destination. bpp is
    // bytes per pixel and applies to both: 3 for RGB24, 4 for RGBX (R,G,B,unused).
    Dma350Composer(int src_w, int src_h, int dst_w, int dst_h, int bpp = 3);
    ~Dma350Composer();

    Dma350Composer(const Dma350Composer&) = delete;
    Dma350Composer& operator=(const Dma350Composer&) = delete;

    bool ok() const { return ok_; }

    // Take only this rectangle of the source. Needed when the frame is narrower than the buffer
    // it sits in: the DMA350 ignores a bytesperline wider than the width, so such a window has to
    // be a crop rather than a stride. Both demos hand over a packed frame and do not need it.
    // Call once before the first compose; without it the whole source is used.
    bool set_source_crop(int x, int y, int w, int h);

    // Write the source window into dst_fd at (x, y). Blocks until the DMA350 reports the job done.
    // False on any failure (the caller drops the frame).
    bool compose(int src_fd, size_t src_offset, int dst_fd, int x, int y);

   private:
    int fd_ = -1;
    int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
    int win_w_ = 0, win_h_ = 0;  // the source window: the whole source unless cropped
    int bpp_ = 3;
    size_t src_bytes_ = 0, dst_bytes_ = 0;
    bool ok_ = false;
    bool streaming_ = false;
    bool warned_align_ = false;
};

}  // namespace app

#endif  // STAGE_PLOT_DMA350_COMPOSER_H_
