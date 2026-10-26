#ifndef STAGE_PLOT_DRAW_DET_H_
#define STAGE_PLOT_DRAW_DET_H_

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "types.h"

namespace app {

extern const std::vector<std::string> COCO_LABELS;  // 80 classes (od)
extern const std::vector<std::string> FACE_LABELS;  // {"face"}

// Draw detection boxes + "name score%" onto im (boxes are in im coordinates).
// rgb_order: true for an RGB buffer (HDMI path), false for BGR/BGRX (framebuffer path). The
// palette is stored BGR, so this only decides whether the channels are swapped on the way out.
void draw_det(cv::Mat& im, const Detections& d, const std::vector<std::string>& names,
              bool rgb_order = true);

}  // namespace app

#endif
