#include "app_module_post_process.h"

#include <atomic>

#include "app_type.h"
#include "benchmarker.h"
#include "post_hdmi/post_yolov8.h"
#include "post_hdmi/post_yolov8_face.h"
#include "post_hdmi/post_yolov8_pose.h"
#include "post_hdmi/post_yolov8_seg.h"
#include "post_lcd/post_yolov8.h"
#include "post_lcd/post_yolov8_pose.h"
#include "post_lcd/post_yolov8_seg.h"

namespace app {
std::thread module_post_process_seg_lcd(postprocess::Module_config&& conf,
                                        postprocess::Module_in&& in,
                                        postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float seg_conf_thres = 0.2;
            float seg_iou_thres = 0.55;
            int seg_nc = 80;  // number of classes
            int seg_imh = conf.npu_h;
            int seg_imw = conf.npu_w;

            mobilint::post_lcd::YOLOv8SegPostProcessor post(
                seg_nc, seg_imh, seg_imw, seg_conf_thres, seg_iou_thres, false);
            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    break;
                }

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> keypoints;
                uint64_t ticket = post.enqueue(data_in.frame, data_in.npu_result, boxes,
                                               scores, labels, keypoints);
                post.receive(ticket);
                out.queue_out.push({data_in.frame});
            }
        });
}

std::thread module_post_process_face_lcd(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        float face_conf_thres = 0.3;
        float face_iou_thres = 0.5;
        int face_nc = 1;  // number of classes
        int face_imh = conf.npu_h;
        int face_imw = conf.npu_w;

        mobilint::post_lcd::YOLOv8PostProcessor post(
            face_nc, face_imh, face_imw, face_conf_thres, face_iou_thres, false);

        while (conf.push_on) {
            app::InterThreadData item;
            auto qsc = in.queue_in.pop(item);
            if (qsc == Queue::StatusCode::CLOSED) {
                break;
            }

            if (conf.bc) conf.bc->start();
            std::vector<std::array<float, 4>> boxes;
            std::vector<float> scores;
            std::vector<int> labels;
            std::vector<std::vector<float>> extras;

            uint64_t ticket =
                post.enqueue(item.frame, item.npu_result, boxes, scores, labels, extras);
            post.receive(ticket);

            std::vector<std::array<float, 4>> rescaled_boxes;

            int srcWidth = item.frame.cols;
            int srcHeight = item.frame.rows;
            post.rescaleImgWithPaddingV2(rescaled_boxes, srcHeight, srcWidth);
            auto result_label = post.get_result_label();
            auto result_score = post.get_result_score();

            for (int j = 0; j < rescaled_boxes.size(); j++) {
                cv::rectangle(item.frame,
                              cv::Point(rescaled_boxes[j][0], rescaled_boxes[j][1]),
                              cv::Point(rescaled_boxes[j][0] + rescaled_boxes[j][2],
                                        rescaled_boxes[j][1] + rescaled_boxes[j][3]),
                              cv::Scalar(0, 255, 0), 2);
            }
            if (conf.bc) conf.bc->end();

            out.queue_out.push({item.frame});
        }
    });
}

std::thread module_post_process_od_lcd(postprocess::Module_config&& conf,
                                       postprocess::Module_in&& in,
                                       postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float od_conf_thres = 0.2;  // 0.5
            float od_iou_thres = 0.15;  // 0.45
            int od_nc = 80;             // number of classes
            int od_imh = conf.npu_h;
            int od_imw = conf.npu_w;

            mobilint::post_lcd::YOLOv8PostProcessor post(
                od_nc, od_imh, od_imw, od_conf_thres, od_iou_thres, false);

            while (conf.push_on) {
                if (conf.bc) conf.bc->start();
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    std::cout << "queue closed.\n";
                    exit(1);
                }

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> keypoints;

                uint64_t ticket = post.enqueue(data_in.frame, data_in.npu_result, boxes,
                                               scores, labels, keypoints);
                post.receive(ticket);
                if (conf.bc) conf.bc->end();

                out.queue_out.push({data_in.frame});
            }
        });
}

std::thread module_post_process_pose_lcd(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float pose_conf_thres = 0.25;
            float pose_iou_thres = 0.65;
            int pose_nc = 1;  // number of classes
            int pose_imh = conf.npu_h;
            int pose_imw = conf.npu_w;

            mobilint::post_lcd::YOLOv8PosePostProcessor post(
                pose_nc, pose_imh, pose_imw, pose_conf_thres, pose_iou_thres, false);

            while (conf.push_on) {
                if (conf.bc) conf.bc->start();
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) break;

                std::vector<std::vector<float>> result;
                result.push_back(std::move(data_in.npu_result[0]));
                result.push_back(std::move(data_in.npu_result[2]));
                result.push_back(std::move(data_in.npu_result[4]));
                result.push_back(std::move(data_in.npu_result[1]));
                result.push_back(std::move(data_in.npu_result[3]));
                result.push_back(std::move(data_in.npu_result[5]));

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> keypoints;
                uint64_t ticket =
                    post.enqueue(data_in.frame, result, boxes, scores, labels, keypoints);

                post.receive(ticket);
                if (conf.bc) conf.bc->end();

                out.queue_out.push({data_in.frame});
            }
        });
}

std::thread module_post_process_seg_hdmi(postprocess::Module_config&& conf,
                                         postprocess::Module_in&& in,
                                         postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float seg_conf_thres = 0.3;
            float seg_iou_thres = 0.6;
            int seg_nc = 80;  // number of classes
            int seg_imh = conf.npu_h;
            int seg_imw = conf.npu_w;

            mobilint::post_hdmi::YOLOv8SegPostProcessor post(
                seg_nc, seg_imh, seg_imw, seg_conf_thres, seg_iou_thres, false);

            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    std::cout << "queue closed.\n";
                    exit(1);
                }
                if (conf.bc) conf.bc->start();

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> extras;

                uint64_t ticket = post.enqueue(data_in.frame, data_in.npu_result, boxes,
                                               scores, labels, extras);
                post.receive(ticket);

                if (conf.bc) conf.bc->end();

                out.queue_out.push({data_in.frame, data_in.time_point});
            }
        });
}

std::thread module_post_process_face_hdmi(postprocess::Module_config&& conf,
                                          postprocess::Module_in&& in,
                                          postprocess::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        float face_conf_thres = 0.15;
        float face_iou_thres = 0.5;
        int face_nc = 1;  // number of classes
        int face_imh = conf.npu_h;
        int face_imw = conf.npu_w;

        mobilint::postface_hdmi::YOLOv8FacePostProcessor post(
            face_nc, face_imh, face_imw, face_conf_thres, face_iou_thres, 1, false);

        while (conf.push_on) {
            InterThreadData data_in;
            auto qsc = in.queue_in.pop(data_in);
            if (qsc == Queue::StatusCode::CLOSED) {
                std::cout << "queue closed.\n";
                exit(1);
            }

            if (conf.bc) conf.bc->start();

            std::vector<std::array<float, 4>> boxes;
            std::vector<float> scores;
            std::vector<int> labels;

            std::vector<mobilint::NDArray<float>> result;

            for (size_t i = 0; i < data_in.npu_result.size(); ++i) {
                std::vector<int64_t> shape = {(signed long)data_in.npu_result[i].size()};
                result.push_back(mobilint::NDArray(data_in.npu_result[i].data(), shape));
            }

            uint64_t ticket = post.enqueue(data_in.frame, result, boxes, scores, labels);
            post.receive(ticket);

            if (conf.bc) conf.bc->end();

            out.queue_out.push({data_in.frame, data_in.time_point});
        }
    });
}

std::thread module_post_process_od_hdmi(postprocess::Module_config&& conf,
                                        postprocess::Module_in&& in,
                                        postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float od_conf_thres = 0.2;  // 0.5
            float od_iou_thres = 0.15;  // 0.45
            int od_nc = 80;             // number of classes
            int od_imh = conf.npu_h;
            int od_imw = conf.npu_w;

            mobilint::post_hdmi::YOLOv8PostProcessor post(
                od_nc, od_imh, od_imw, od_conf_thres, od_iou_thres, false);

            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    std::cout << "queue closed.\n";
                    exit(1);
                }

                if (conf.bc) conf.bc->start();

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> keypoints;

                uint64_t ticket = post.enqueue(data_in.frame, data_in.npu_result, boxes,
                                               scores, labels, keypoints);
                post.receive(ticket);

                if (conf.bc) conf.bc->end();

                out.queue_out.push({data_in.frame, data_in.time_point});
            }
        });
}

std::thread module_post_process_pose_hdmi(postprocess::Module_config&& conf,
                                          postprocess::Module_in&& in,
                                          postprocess::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            float pose_conf_thres = 0.25;
            float pose_iou_thres = 0.65;
            int pose_nc = 1;  // number of classes
            int pose_imh = conf.npu_h;
            int pose_imw = conf.npu_w;

            mobilint::post_hdmi::YOLOv8PosePostProcessor post(
                pose_nc, pose_imh, pose_imw, pose_conf_thres, pose_iou_thres, false);

            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.queue_in.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) break;

                if (conf.bc) conf.bc->start();

                std::vector<std::array<float, 4>> boxes;
                std::vector<float> scores;
                std::vector<int> labels;
                std::vector<std::vector<float>> keypoints;
                uint64_t ticket = post.enqueue(data_in.frame, data_in.npu_result, boxes,
                                               scores, labels, keypoints);

                post.receive(ticket);

                if (conf.bc) conf.bc->end();

                out.queue_out.push({data_in.frame, data_in.time_point});
            }
        });
}
}  // namespace app
