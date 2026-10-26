#ifndef STAGE_PLOT_DRAW_POSE_H_
#define STAGE_PLOT_DRAW_POSE_H_

#include <opencv2/opencv.hpp>

#include "types.h"

namespace app {

// Draw 17-keypoint skeletons for each detection onto im (keypoints in im coordinates).
// rgb_order: true for an RGB buffer (HDMI path), false for BGR/BGRX (framebuffer path).
void draw_pose(cv::Mat& im, const Detections& d, bool rgb_order = true);

}  // namespace app

#endif
