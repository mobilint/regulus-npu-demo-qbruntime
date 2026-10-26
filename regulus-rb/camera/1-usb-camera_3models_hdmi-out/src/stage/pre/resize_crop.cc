#include "resize_crop.h"

#include <cmath>

namespace app {

CoverCrop cover_crop(int src_w, int src_h, int dst_w, int dst_h) {
    CoverCrop c;
    if ((float)dst_w / dst_h > (float)src_w / src_h) {
        // target wider: match width, crop top/bottom
        c.scaled_w = dst_w;
        c.scaled_h = std::ceil(src_h * ((float)dst_w / src_w));
    } else {
        // target taller: match height, crop left/right
        c.scaled_w = std::ceil(src_w * ((float)dst_h / src_h));
        c.scaled_h = dst_h;
    }
    // MAPI keeps a requested width only when width*bytes_per_pixel is 16-byte aligned; otherwise
    // it silently rounds the width up and pads the stride, and the capture caps then disagree with
    // what the driver produces (every frame gets dropped). A multiple of 16 satisfies that for both
    // pixel sizes in use -- 16*3 and 16*4 are both 16-byte aligned -- so this stays the strict rule
    // rather than being relaxed to 4 for the 4-byte path. The extra columns widen the horizontal
    // crop by a few pixels; the scale stays within ~1% of exact cover.
    c.scaled_w = (c.scaled_w + 15) / 16 * 16;

    c.crop_x = (c.scaled_w - dst_w) / 2;
    c.crop_y = (c.scaled_h - dst_h) / 2;
    return c;
}

}  // namespace app
