#include "app_module_capture.h"

#include <qbruntime/model.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "gst_base_pipeline.h"
#include "gst_mipisrc_pipeline.h"

namespace app {
std::thread module_opencv_capture_fb(capture::Module_config&& conf,
                                     capture::Module_out&& out) {
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
            auto start_time = std::chrono::steady_clock::now();

            cv::Mat frame;
            cv::Mat frame_bgra;

            if (cap_open_failed) {
                if (conf.bc) conf.bc->start();
                frame_bgra = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC4);
                usleep(10000);
            } else {
                cap.grab();
                if (conf.bc) conf.bc->start();
                cap.retrieve(frame);
                if (frame.empty()) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
                cv::cvtColor(frame, frame_bgra, cv::COLOR_BGR2BGRA);
            }

            if (conf.bc) conf.bc->end();

            if (conf.reposition_buffer)
                out.buffer.put({frame_bgra, conf.reposition_buffer, start_time});
            else
                out.buffer.put({frame_bgra, start_time});
        }
    });
}

std::thread module_opencv_capture_kmssink(capture::Module_config&& conf,
                                          capture::Module_out&& out) {
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
            auto start_time = std::chrono::steady_clock::now();

            cv::Mat frame;

            if (cap_open_failed) {
                if (conf.bc) conf.bc->start();
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                cap.grab();
                if (conf.bc) conf.bc->start();
                cap.retrieve(frame);
                if (frame.empty()) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
            }

            if (conf.bc) conf.bc->end();

            if (conf.reposition_buffer)
                out.buffer.put({frame, conf.reposition_buffer, start_time});
            else
                out.buffer.put({frame, start_time});
        }
    });
}

std::thread module_gstreamer_mipi_capture_fb(capture::Module_config&& conf,
                                             capture::Module_out&& out) {
    return std::thread([conf = std::move(conf), out = std::move(out)]() {
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
            cv::Mat frame_bgra;

            if (cap_open_failed) {
                if (conf.bc) conf.bc->start();
                frame_bgra = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC4);
                usleep(10000);
            } else {
                gst_wrapper::process_data_from_appsink(
                    gst_pipeline_capture_mipi.appsink,
                    [&](uint8_t* data, unsigned long size) -> void {
                        if (size != conf.camera_width * conf.camera_height * 3) {
                            std::cout << "size mismatch in mipi capture\n";
                            return;
                        }
                        if (conf.bc) conf.bc->start();
                        frame = cv::Mat(conf.camera_height, conf.camera_width, CV_8UC3);
                        memcpy(frame.data, data, size);
                    });
                if (frame.empty()) {
                    if (conf.bc) conf.bc->end();
                    continue;
                }
                cv::cvtColor(frame, frame_bgra, cv::COLOR_RGB2BGRA);
            }

            if (conf.bc) conf.bc->end();

            if (conf.reposition_buffer)
                out.buffer.put({frame_bgra, conf.reposition_buffer, start_time});
            else
                out.buffer.put({frame_bgra, start_time});
        }

        gst_pipeline_capture_mipi.stop();
    });
}

std::thread module_gstreamer_mipi_capture_kmssink(capture::Module_config&& conf,
                                                  capture::Module_out&& out) {
    return std::thread([conf = std::move(conf), out = std::move(out)]() {
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
                if (conf.bc) conf.bc->start();
                frame = cv::Mat::zeros(cv::Size(conf.npu_w, conf.npu_h), CV_8UC3);
                usleep(10000);
            } else {
                gst_wrapper::process_data_from_appsink(
                    gst_pipeline_capture_mipi.appsink,
                    [&](uint8_t* data, unsigned long size) -> void {
                        if (size != conf.camera_width * conf.camera_height * 3) {
                            std::cout << "size mismatch in mipi capture\n";
                            return;
                        }
                        if (conf.bc) conf.bc->start();
                        frame = cv::Mat(conf.camera_height, conf.camera_width, CV_8UC3);
                        memcpy(frame.data, data, size);
                    });
                if (frame.empty()) {
                    if (conf.bc) conf.bc->end();
                    continue;
                }
                cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
            }

            if (conf.bc) conf.bc->end();

            if (conf.reposition_buffer)
                out.buffer.put({frame, conf.reposition_buffer, start_time});
            else
                out.buffer.put({frame, start_time});
        }

        gst_pipeline_capture_mipi.stop();
    });
}
}  // namespace app
