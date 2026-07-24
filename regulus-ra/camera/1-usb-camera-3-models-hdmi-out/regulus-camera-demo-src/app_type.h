#ifndef TYPE_H_
#define TYPE_H_

#include <qbruntime/type.h>

#include <opencv2/opencv.hpp>

#include "app_maccel.h"
#include "tsqueue.h"

namespace app {
struct InterThreadData {
    InterThreadData() {}
    InterThreadData(cv::Mat frame) : frame(frame) {}
    InterThreadData(cv::Mat frame,
                    std::chrono::time_point<std::chrono::steady_clock> time_point)
        : frame(frame), time_point(time_point) {}
    InterThreadData(cv::Mat frame, MaccelRepositionBuffer* reposition_buffer)
        : frame(frame), reposition_buffer(reposition_buffer) {}
    InterThreadData(cv::Mat frame, MaccelRepositionBuffer* reposition_buffer,
                    std::chrono::time_point<std::chrono::steady_clock> time_point)
        : frame(frame), reposition_buffer(reposition_buffer), time_point(time_point) {}
    InterThreadData(cv::Mat frame, std::vector<std::vector<float>>&& npu_result)
        : frame(frame), npu_result(std::move(npu_result)) {}
    InterThreadData(cv::Mat frame, cv::Mat pre) : frame(frame), pre(pre) {}
    InterThreadData(std::chrono::time_point<std::chrono::steady_clock> time_point)
        : time_point(time_point) {}
    InterThreadData(cv::Mat frame, std::vector<std::vector<float>>&& npu_result,
                    std::chrono::time_point<std::chrono::steady_clock> time_point)
        : frame(frame), npu_result(std::move(npu_result)), time_point(time_point) {}
    InterThreadData(cv::Mat frame, cv::Mat pre,
                    std::chrono::time_point<std::chrono::steady_clock> time_point)
        : frame(frame), pre(pre), time_point(time_point) {}

    cv::Mat frame;
    cv::Mat pre;
    std::vector<std::vector<float>> npu_result;
    std::chrono::time_point<std::chrono::steady_clock> time_point;
    MaccelRepositionBuffer* reposition_buffer = nullptr;
};

using Buffer = ThreadSafeBuffer<InterThreadData>;
using Queue = ThreadSafeQueue<InterThreadData>;
}  // namespace app

#endif
