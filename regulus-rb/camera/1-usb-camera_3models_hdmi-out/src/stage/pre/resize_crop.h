#ifndef STAGE_PRE_RESIZE_CROP_H_
#define STAGE_PRE_RESIZE_CROP_H_

namespace app {

// Cover-scale + center-crop geometry (no pixels touched, just the numbers). Scales src to the
// smallest size that covers dst while preserving aspect (scaled_w x scaled_h), then crop_x/crop_y
// is the top-left of the centered dst WxH region inside it. Used to drive a HW scale (MAPI) plus
// a crop instead of a CPU resize.
//
// scaled_w is rounded up to a multiple of 16: MAPI returns a requested width unchanged only when
// width*bytes_per_pixel is 16-byte aligned, and a silently widened frame breaks the capture caps.
// 16 covers both 3- and 4-byte pixels.
// A landscape camera feeding a portrait model always yields crop_x != 0.
struct CoverCrop {
  int scaled_w, scaled_h;
  int crop_x, crop_y;
};

CoverCrop cover_crop(int src_w, int src_h, int dst_w, int dst_h);

}  // namespace app

#endif
