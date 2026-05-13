#include "app_module_capture_and_preprocess.h"

#include <qbruntime/model.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "gst_mipisrc_pipeline.h"

static inline cv::Mat resize_frame(cv::Mat frame, int npu_w, int npu_h) {
    int w = frame.cols;
    int h = frame.rows;

    cv::Mat resized_frame;

    int w_resize;
    int h_resize;
    if ((float)npu_w / npu_h > (float)w / h) {
        float w_ratio = (float)npu_w / w;

        w_resize = npu_w;
        h_resize = std::ceil(h * w_ratio);

        cv::resize(frame, resized_frame, cv::Size(w_resize, h_resize));
    } else {
        float h_ratio = (float)npu_h / h;

        w_resize = std::ceil(w * h_ratio);
        h_resize = npu_h;

        cv::resize(frame, resized_frame, cv::Size(w_resize, h_resize));
    }

    cv::Mat frame_out = resized_frame({(int)((w_resize - npu_w) / 2),
                                       (int)((h_resize - npu_h) / 2), npu_w, npu_h})
                            .clone();

    return frame_out;
}

namespace app {
std::thread module_opencv_capture_and_preprocess(
    capture_and_preprocess::Module_config&& conf,
    capture_and_preprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), out = std::move(out)]() {
        cv::VideoCapture cap;
        cap.open(conf.camera_dev_path, cv::CAP_V4L2);

        bool cap_open_failed = false;
        if (!cap.isOpened()) {
            std::cout << "src open failed: " << conf.camera_dev_path << std::endl;
            cap_open_failed = true;
        } else {
            std::cout << "src open succeeded: " << conf.camera_dev_path << std::endl;
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH, conf.camera_width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, conf.camera_height);
            cap.set(cv::CAP_PROP_FPS, conf.camera_fps);
            cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
        }

        while (conf.push_on) {
            if (conf.bc) conf.bc->start();
            auto start_time = std::chrono::steady_clock::now();
            cv::Mat frame;
            if (cap_open_failed) {
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                cap >> frame;
                if (frame.empty()) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
            }

            cv::Mat frame_pre = resize_frame(frame, conf.npu_w, conf.npu_h);
            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);

            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(frame_pre.data[3 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(frame_pre.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(frame_pre.data[3 * i + 0]) / 255.0f;
            }

            if (conf.bc) conf.bc->end();

            out.buffer.put({frame_pre, pre, start_time});
        }
    });
}

std::thread module_opencv_capture_and_preprocess_with_reposition(
    capture_and_preprocess::Module_config_rep&& conf,
    capture_and_preprocess::Module_out&& out) {
    return std::thread([&]() {
        cv::VideoCapture cap;
        cap.open(conf.camera_dev_path, cv::CAP_V4L2);

        bool cap_open_failed = false;
        if (!cap.isOpened()) {
            std::cout << "src open failed: " << conf.camera_dev_path << std::endl;
            cap_open_failed = true;
        } else {
            std::cout << "src open succeeded: " << conf.camera_dev_path << std::endl;
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH, conf.camera_width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, conf.camera_height);
            cap.set(cv::CAP_PROP_FPS, conf.camera_fps);
        }

        while (conf.push_on) {
            cv::Mat frame;
            if (cap_open_failed) {
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                cap >> frame;
                if (frame.empty()) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
            }

            cv::Mat frame_pre = resize_frame(frame, conf.npu_w, conf.npu_h);
            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);

            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(frame_pre.data[3 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(frame_pre.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(frame_pre.data[3 * i + 0]) / 255.0f;
            }

            conf.model->repositionInputs({(float*)pre.data}, *(conf.repositioned_buffer));

            out.buffer.put({frame_pre});
        }
    });
}

std::thread module_gstreamer_mipi_capture_and_preprocess(
    capture_and_preprocess::Module_config&& conf,
    capture_and_preprocess::Module_out&& out) {
    return std::thread([&]() {
        gst_wrapper::MipiSrcAppsinkPipeline gst_pipeline_capture_mipi(
            conf.camera_dev_path.c_str(), "YUY2", 3840, 2160, conf.camera_fps, "RGB",
            conf.camera_width, conf.camera_height, conf.camera_fps);
        gst_pipeline_capture_mipi.start();

        GstState state, pending;
        auto ret = gst_element_get_state(gst_pipeline_capture_mipi.pipeline, &state,
                                         &pending, GST_CLOCK_TIME_NONE);

        bool cap_open_failed = false;
        if (ret == GST_STATE_CHANGE_SUCCESS) {
            std::cout << "src open succeeded: " << conf.camera_dev_path << std::endl;
        } else {
            if (ret == GST_STATE_CHANGE_ASYNC) {
                std::cout << "src open async: " << conf.camera_dev_path << std::endl;
            } else {
                std::cout << "src open failed: " << conf.camera_dev_path << std::endl;
            }
            cap_open_failed = true;
        }

        while (conf.push_on) {
            auto start_time = std::chrono::steady_clock::now();
            cv::Mat frame;

            if (cap_open_failed) {
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                gst_wrapper::process_data_from_appsink(
                    gst_pipeline_capture_mipi.appsink,
                    [&](uint8_t* data, unsigned long size) -> void {
                        frame = cv::Mat(conf.camera_height, conf.camera_width, CV_8UC3);
                        memcpy(frame.data, data, size);
                    });
            }

            cv::Mat resized_frame = resize_frame(frame, conf.npu_w, conf.npu_h);
            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);

            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[3 * i + 0]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[3 * i + 2]) / 255.0f;
            }

            out.buffer.put({resized_frame, pre, start_time});
        }

        gst_pipeline_capture_mipi.stop();
    });
}

std::thread module_gstreamer_mipi_capture_and_preprocess_with_reposition(
    capture_and_preprocess::Module_config_rep&& conf,
    capture_and_preprocess::Module_out&& out) {
    return std::thread([&]() {
        gst_wrapper::MipiSrcAppsinkPipeline gst_pipeline_capture_mipi(
            conf.camera_dev_path.c_str(), "YUY2", 3840, 2160, conf.camera_fps, "RGB",
            conf.camera_width, conf.camera_height, conf.camera_fps);
        gst_pipeline_capture_mipi.start();

        GstState state, pending;
        auto ret = gst_element_get_state(gst_pipeline_capture_mipi.pipeline, &state,
                                         &pending, GST_CLOCK_TIME_NONE);

        bool cap_open_failed = false;
        if (ret == GST_STATE_CHANGE_SUCCESS) {
            std::cout << "src open succeeded: " << conf.camera_dev_path << std::endl;
        } else {
            if (ret == GST_STATE_CHANGE_ASYNC) {
                std::cout << "src open async: " << conf.camera_dev_path << std::endl;
            } else {
                std::cout << "src open failed: " << conf.camera_dev_path << std::endl;
            }
            cap_open_failed = true;
        }

        while (conf.push_on) {
            cv::Mat frame;

            if (cap_open_failed) {
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                gst_wrapper::process_data_from_appsink(
                    gst_pipeline_capture_mipi.appsink,
                    [&](uint8_t* data, unsigned long size) -> void {
                        frame = cv::Mat(conf.camera_height, conf.camera_width, CV_8UC3);
                        memcpy(frame.data, data, size);
                    });
            }

            cv::Mat resized_frame = resize_frame(frame, conf.npu_w, conf.npu_h);
            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);

            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[3 * i + 0]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[3 * i + 2]) / 255.0f;
            }

            conf.model->repositionInputs({(float*)pre.data}, *(conf.repositioned_buffer));
            out.buffer.put({resized_frame});
        }

        gst_pipeline_capture_mipi.stop();
    });
}
}  // namespace app
