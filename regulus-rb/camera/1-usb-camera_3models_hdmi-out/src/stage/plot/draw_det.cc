#include "draw_det.h"

#include <algorithm>
#include <array>

namespace app {

const std::vector<std::string> FACE_LABELS = {"face"};

const std::vector<std::string> COCO_LABELS = {
    "person",        "bicycle",      "car",
    "motorcycle",    "airplane",     "bus",
    "train",         "truck",        "boat",
    "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench",        "bird",
    "cat",           "dog",          "horse",
    "sheep",         "cow",          "elephant",
    "bear",          "zebra",        "giraffe",
    "backpack",      "umbrella",     "handbag",
    "tie",           "suitcase",     "frisbee",
    "skis",          "snowboard",    "sports ball",
    "kite",          "baseball bat", "baseball glove",
    "skateboard",    "surfboard",    "tennis racket",
    "bottle",        "wine glass",   "cup",
    "fork",          "knife",        "spoon",
    "bowl",          "banana",       "apple",
    "sandwich",      "orange",       "broccoli",
    "carrot",        "hot dog",      "pizza",
    "donut",         "cake",         "chair",
    "couch",         "potted plant", "bed",
    "dining table",  "toilet",       "tv",
    "laptop",        "mouse",        "remote",
    "keyboard",      "cell phone",   "microwave",
    "oven",          "toaster",      "sink",
    "refrigerator",  "book",         "clock",
    "vase",          "scissors",     "teddy bear",
    "hair drier",    "toothbrush"};

static const std::array<std::array<int, 3>, 20> COLORS = {
    {{56, 56, 255},  {151, 157, 255}, {31, 112, 255}, {29, 178, 255},  {49, 210, 207},
     {10, 249, 72},  {23, 204, 146},  {134, 219, 61}, {52, 147, 26},   {187, 212, 0},
     {168, 153, 44}, {255, 194, 0},   {147, 69, 52},  {255, 115, 100}, {236, 24, 0},
     {255, 56, 132}, {133, 0, 82},    {255, 56, 203}, {200, 149, 255}, {199, 55, 255}}};

void draw_det(cv::Mat& im, const Detections& d, const std::vector<std::string>& names,
              bool rgb_order) {
    for (size_t i = 0; i < d.boxes.size(); i++) {
        int xmin = std::max((int)d.boxes[i][0], 0);
        int ymin = std::max((int)d.boxes[i][1], 0);
        int xmax = std::min((int)d.boxes[i][2], im.cols);
        int ymax = std::min((int)d.boxes[i][3], im.rows);

        const auto& c = COLORS[d.labels[i] % 20];  // palette is BGR-ordered
        // 4th component keeps the alpha byte opaque on a 4-channel (BGRX) buffer; it is ignored
        // on a 3-channel one.
        const cv::Scalar clr = rgb_order ? cv::Scalar(c[2], c[1], c[0], 255)
                                         : cv::Scalar(c[0], c[1], c[2], 255);
        cv::rectangle(im, cv::Rect(xmin, ymin, xmax - xmin, ymax - ymin), clr, 2);

        double font_scale = std::min(std::max((xmax - xmin) / 500.0, 0.35), 0.99) * 1.3;
        std::string desc =
            names[d.labels[i]] + " " + std::to_string((int)(d.scores[i] * 100)) + "%";
        cv::putText(im, desc, cv::Point(xmin, ymin - 10), cv::FONT_HERSHEY_SIMPLEX,
                    font_scale, clr, 2, false);
    }
}

}  // namespace app
