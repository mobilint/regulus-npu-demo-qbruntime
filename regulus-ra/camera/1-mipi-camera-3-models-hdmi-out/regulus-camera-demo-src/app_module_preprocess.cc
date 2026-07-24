#ifndef INFER_H_
#define INFER_H_

#include "app_module_preprocess.h"

#include <qbruntime/model.h>
#include <qbruntime/type.h>

#include <atomic>

#include "app_type.h"
#include "tsqueue.h"

namespace app {
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

std::thread module_preprocess_fb(preprocess::Module_config&& conf,
                                 preprocess::Module_in&& in,
                                 preprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            if (conf.bc) conf.bc->start();
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[4 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[4 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[4 * i + 0]) / 255.0f;
            }
            if (conf.bc) conf.bc->end();

            out.buffer.put({resized_frame, pre, data_in.time_point});
        }
    });
}

std::thread module_preprocess_and_reposition_fb(preprocess::Module_config&& conf,
                                                preprocess::Module_in&& in,
                                                preprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            if (conf.bc) conf.bc->start();

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[4 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[4 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[4 * i + 0]) / 255.0f;
            }

            data_in.reposition_buffer->reposition((float*)pre.data);
            if (conf.bc) conf.bc->end();

            out.queue.push({resized_frame, data_in.time_point});
            out.buffer.put(
                {resized_frame, data_in.reposition_buffer, data_in.time_point});
        }
    });
}

std::thread module_preprocess_and_reposition_fb(preprocess::Module_config&& conf,
                                                preprocess::Module_in&& in,
                                                preprocess::Module_out2&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            if (conf.bc) conf.bc->start();

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[4 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[4 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[4 * i + 0]) / 255.0f;
            }

            data_in.reposition_buffer->reposition((float*)pre.data);
            if (conf.bc) conf.bc->end();

            out.buffer.put(
                {resized_frame, data_in.reposition_buffer, data_in.time_point});
        }
    });
}

std::thread module_preprocess_kmssink(preprocess::Module_config&& conf,
                                      preprocess::Module_in&& in,
                                      preprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            if (conf.bc) conf.bc->start();
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[3 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[3 * i + 0]) / 255.0f;
            }
            if (conf.bc) conf.bc->end();

            out.buffer.put({resized_frame, pre, data_in.time_point});
        }
    });
}

std::thread module_preprocess_and_reposition_kmssink(preprocess::Module_config&& conf,
                                                     preprocess::Module_in&& in,
                                                     preprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            if (conf.bc) conf.bc->start();

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[3 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[3 * i + 0]) / 255.0f;
            }

            data_in.reposition_buffer->reposition((float*)pre.data);
            if (conf.bc) conf.bc->end();

            out.queue.push({resized_frame, data_in.time_point});
            out.buffer.put(
                {resized_frame, data_in.reposition_buffer, data_in.time_point});
        }
    });
}

std::thread module_preprocess_and_reposition_kmssink(preprocess::Module_config&& conf,
                                                     preprocess::Module_in&& in,
                                                     preprocess::Module_out2&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }

            if (conf.bc) conf.bc->start();

            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);

            cv::Mat pre(conf.npu_h, conf.npu_w, CV_32FC3);
            for (int i = 0; i < conf.npu_w * conf.npu_h; i++) {
                // convert BGR to RGB
                ((float*)pre.data)[3 * i + 0] =
                    (float)(resized_frame.data[3 * i + 2]) / 255.0f;
                ((float*)pre.data)[3 * i + 1] =
                    (float)(resized_frame.data[3 * i + 1]) / 255.0f;
                ((float*)pre.data)[3 * i + 2] =
                    (float)(resized_frame.data[3 * i + 0]) / 255.0f;
            }

            data_in.reposition_buffer->reposition((float*)pre.data);
            if (conf.bc) conf.bc->end();

            out.buffer.put(
                {resized_frame, data_in.reposition_buffer, data_in.time_point});
        }
    });
}
}  // namespace app

#endif
