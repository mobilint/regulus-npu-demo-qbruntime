#include "draw_pose.h"

#include <array>
#include <vector>

namespace app {

// COCO 17-keypoint skeleton (1-indexed pairs) and palettes, from the original demo.
static const std::vector<std::array<int, 2>> SKELETON = {
    {16, 14}, {14, 12}, {17, 15}, {15, 13}, {12, 13}, {6, 12}, {7, 13}, {6, 7}, {6, 8}, {7, 9},
    {8, 10},  {9, 11},  {2, 3},   {1, 2},   {1, 3},   {2, 4},  {3, 5},  {4, 6}, {5, 7}};

static const std::vector<std::array<int, 3>> LIMB_COLOR = {
    {51, 153, 255}, {51, 153, 255}, {51, 153, 255}, {51, 153, 255}, {255, 51, 255}, {255, 51, 255}, {255, 51, 255},
    {255, 128, 0},  {255, 128, 0},  {255, 128, 0},  {255, 128, 0},  {255, 128, 0},  {0, 255, 0},    {0, 255, 0},
    {0, 255, 0},    {0, 255, 0},    {0, 255, 0},    {0, 255, 0},    {0, 255, 0}};

static const std::vector<std::array<int, 3>> KPT_COLOR = {
    {0, 255, 0},    {0, 255, 0},    {0, 255, 0},    {0, 255, 0},    {0, 255, 0},   {255, 128, 0},
    {255, 128, 0},  {255, 128, 0},  {255, 128, 0},  {255, 128, 0},  {255, 128, 0}, {51, 153, 255},
    {51, 153, 255}, {51, 153, 255}, {51, 153, 255}, {51, 153, 255}, {51, 153, 255}};

void draw_pose(cv::Mat& im, const Detections& d, bool rgb_order) {
  // Palettes below are BGR-ordered; swap only when the target buffer is RGB. The 4th component
  // keeps the alpha byte opaque on a 4-channel (BGRX) buffer and is ignored on a 3-channel one.
  auto pick = [rgb_order](const std::array<int, 3>& c) {
    return rgb_order ? cv::Scalar(c[2], c[1], c[0], 255) : cv::Scalar(c[0], c[1], c[2], 255);
  };
  const int radius = 5, steps = 3, num_kpts = 17;
  const float kpt_thres = 0.4f;   // draw a keypoint
  const float limb_thres = 0.5f;  // draw a skeleton line

  for (const auto& kp : d.keypoints) {
    for (int j = 0; j < num_kpts; j++) {
      if (kp[steps * j + 2] < kpt_thres) continue;
      int x = (int)kp[steps * j + 0], y = (int)kp[steps * j + 1];
      if (x <= 0 || y <= 0 || x >= im.cols || y >= im.rows) continue;
      cv::circle(im, cv::Point(x, y), radius, pick(KPT_COLOR[j]), -1);
    }
    for (size_t s = 0; s < SKELETON.size(); s++) {
      const auto& sk = SKELETON[s];
      if (kp[(sk[0] - 1) * steps + 2] < limb_thres || kp[(sk[1] - 1) * steps + 2] < limb_thres) continue;
      cv::Point p1((int)kp[(sk[0] - 1) * steps], (int)kp[(sk[0] - 1) * steps + 1]);
      cv::Point p2((int)kp[(sk[1] - 1) * steps], (int)kp[(sk[1] - 1) * steps + 1]);
      if (p1.x <= 0 || p1.y <= 0 || p2.x <= 0 || p2.y <= 0) continue;
      cv::line(im, p1, p2, pick(LIMB_COLOR[s]), 2);
    }
  }
}

}  // namespace app
