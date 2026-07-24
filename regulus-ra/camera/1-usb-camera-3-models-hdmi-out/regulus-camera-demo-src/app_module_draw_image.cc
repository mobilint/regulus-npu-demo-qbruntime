#include "app_module_draw_image.h"

#include <atomic>
#include <thread>

#include "app_image.h"
#include "app_type.h"
#include "benchmarker.h"
#include "opencv2/core/mat.hpp"
#include "regulus_fb.h"

namespace app {
std::thread module_draw_image_3_models_lcd_fb(draw_image::Module_config&& conf,
                                              draw_image::Module_in_3_models&& in,
                                              draw_image::Module_out_fb&& out) {
    auto func = [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
        RegulusFramebuffer fb(RegulusDisplayType::LCD);
        cv::Mat fb_display = fb.get_display_mat();

        cv::Mat display = cv::Mat::zeros(cv::Size(1200, 1920), CV_8UC3);
        cv::Mat background =
            cv::imread("./img/regulus_smart_camera_demo.png", cv::IMREAD_UNCHANGED);
        cv::resize(background, background, cv::Size(1200, 1920));

        cv::Mat mask = cv::Mat::zeros(cv::Size(1200, 1920), CV_8UC1);
        int from_to[] = {3, 0};
        cv::mixChannels(&background, 1, &mask, 1, from_to, 1);
        mask = mask <= 0;

        cv::Mat background_color =
            cv::imread("./img/regulus_smart_camera_demo.png", cv::IMREAD_COLOR);
        background_color.copyTo(display({0, 0, 1200, 1920}));

        fb.draw_frame(display, 0, 0);

        while (conf.push_on) {
            if (out.bc_frame) {
                out.bc_frame->end();
                out.bc_frame->start();
            }

            app::InterThreadData data_org, data_face, data_od, data_pose;

            in.queue_in_face.pop(data_face);
            in.queue_in_pose.pop(data_pose);
            in.queue_in_od.pop(data_od);
            in.queue_in_org.pop(data_org);

            if (conf.bc) conf.bc->start();

            cv::Mat frame_face = data_face.frame({0, 0, 512, 640});
            cv::Mat frame_pose = data_pose.frame({0, 0, 512, 640});
            cv::Mat frame_od = data_od.frame({0, 0, 512, 640});
            cv::Mat frame_org = data_org.frame({0, 0, 512, 640});

            frame_org.copyTo(fb_display({73, 537, 512, 640}), mask({73, 537, 512, 640}));
            frame_face.copyTo(fb_display({615, 537, 512, 640}),
                              mask({615, 537, 512, 640}));
            frame_pose.copyTo(fb_display({73, 1207, 512, 640}),
                              mask({73, 1207, 512, 640}));
            frame_od.copyTo(fb_display({615, 1207, 512, 640}),
                            mask({615, 1207, 512, 640}));

            // fb.draw_frame(frame_org, 73, 537);
            // fb.draw_frame(frame_face, 615, 537);
            // fb.draw_frame(frame_pose, 73, 1207);
            // fb.draw_frame(frame_od, 615, 1207);

            std::chrono::milliseconds ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch());
            double milli = double(ms.count() % 1000) / 1000.f;
            double ratio = abs(sin(milli * 3.141592));
            cv::Scalar color =
                cv::Scalar(0, 0, 255) * ratio + cv::Scalar(37, 37, 37) * (1 - ratio);
            cv::circle(fb_display, cv::Point(119, 466), 11, color, cv::FILLED, 8, 0);
            if (conf.bc) conf.bc->end();

            if (out.latencies_ms)
                out.latencies_ms->addValue(std::chrono::duration<double, std::milli>{
                    std::chrono::steady_clock::now() - data_org.time_point}
                                               .count());
        }
    };

    return std::thread(func);
}

std::thread module_draw_image_3_models_hdmi(draw_image::Module_config&& conf,
                                            draw_image::Module_in_3_models&& in,
                                            draw_image::Module_out&& out) {
    auto func = [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
        cv::Mat background =
            cv::imread("./img/[Regulus] Smart Camera_Demo_250226.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(background, background, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        std::vector<cv::Mat> displays;
        for (int i = 0; i < 3; i++) {
            cv::Mat display = cv::Mat::zeros(cv::Size(1920, 1080), CV_8UC3);
            background.copyTo(display({0, 0, 1920, 1080}));
            displays.push_back(display);
        }

        cv::Mat mask = img::get_mask(background);

        cv::Mat section_org = cv::imread("./img/@Section_org.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_face =
            cv::imread("./img/@Section_face.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_od = cv::imread("./img/@Section_od.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_pose =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_UNCHANGED);

        cv::Mat section_org_alpha = img::get_alpha(section_org);
        cv::Mat section_face_alpha = img::get_alpha(section_face);
        cv::Mat section_od_alpha = img::get_alpha(section_od);
        cv::Mat section_pose_alpha = img::get_alpha(section_pose);

        cv::Mat section_org_color =
            cv::imread("./img/@Section_org.png", cv::IMREAD_COLOR);
        cv::Mat section_face_color =
            cv::imread("./img/@Section_face.png", cv::IMREAD_COLOR);
        cv::Mat section_od_color = cv::imread("./img/@Section_od.png", cv::IMREAD_COLOR);
        cv::Mat section_pose_color =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(section_org_color, section_org_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_face_color, section_face_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_od_color, section_od_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_pose_color, section_pose_color, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        int frame_i = 0;
        double ratio = 0;
        bool ratio_inc = true;
        Benchmarker bc;

        while (conf.push_on) {
            if (frame_i == 3) {
                frame_i = 0;
            }
            cv::Mat display = displays[frame_i++];

            app::InterThreadData data_org, data_face, data_od, data_pose;
            in.queue_in_org.pop(data_org);
            in.queue_in_face.pop(data_face);
            in.queue_in_od.pop(data_od);
            in.queue_in_pose.pop(data_pose);

            if (conf.bc) conf.bc->start();

            cv::Mat frame_org = data_org.frame;
            cv::Mat frame_face = data_face.frame;
            cv::Mat frame_od = data_od.frame;
            cv::Mat frame_pose = data_pose.frame;

            frame_org.copyTo(display({70, 212, 576, 384}), mask({70, 212, 576, 384}));
            frame_face.copyTo(display({70, 212 + 384 + 30, 576, 384}),
                              mask({70, 212, 576, 384}));
            frame_od.copyTo(display({70 + 576 + 30, 212, 576, 384}),
                            mask({70, 212, 576, 384}));
            frame_pose.copyTo(display({70 + 576 + 30, 212 + 384 + 30, 576, 384}),
                              mask({70, 212, 576, 384}));

            // draw live circle
            cv::Scalar color;

            if (conf.format == "BGR") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(33, 33, 223) * ratio;
            } else if (conf.format == "RGB") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(223, 33, 33) * ratio;
            }

            if (ratio_inc) {
                ratio += 0.066;
                if (ratio >= 1) ratio_inc = false;
            } else {
                ratio -= 0.066;
                if (ratio <= 0) ratio_inc = true;
            }

            cv::Mat circle_area(cv::Size(20, 20), CV_8UC3, cv::Scalar(255, 255, 255));
            cv::circle(circle_area, cv::Point(10, 10), 7, color, cv::FILLED, cv::LINE_AA,
                       0);

            circle_area.copyTo(display(cv::Rect(1643 + 29, 57 + 22, 20, 20)));

            auto draw_section = [&display](cv::Mat section_color, cv::Mat section_alpha,
                                           int x, int y) {
                cv::Mat section_area_org =
                    display({x, y, section_color.cols, section_color.rows});
                for (int y = 0; y < section_color.rows; y++) {
                    for (int x = 0; x < section_color.cols; x++) {
                        cv::Vec3b disp = section_area_org.at<cv::Vec3b>(y, x);
                        cv::Vec3b sect = section_color.at<cv::Vec3b>(y, x);
                        float alpha = section_alpha.at<uchar>(y, x) / 255.0f;

                        for (int c = 0; c < 3; c++) {
                            disp[c] = disp[c] * (1 - alpha) + sect[c] * alpha;
                        }

                        section_area_org.at<cv::Vec3b>(y, x) = disp;
                    }
                }
            };

            draw_section(section_org_color, section_org_alpha, 70 + 30, 212 + 300);
            draw_section(section_face_color, section_face_alpha, 70 + 30,
                         212 + 384 + 30 + 300);
            draw_section(section_od_color, section_od_alpha, 70 + 576 + 30 + 30,
                         212 + 300);
            draw_section(section_pose_color, section_pose_alpha, 70 + 576 + 30 + 30,
                         212 + 384 + 30 + 300);

            auto times = {data_face.time_point, data_pose.time_point, data_od.time_point,
                          data_org.time_point};

            if (conf.bc) conf.bc->end();

            out.queue_out.push({display, *std::min_element(times.begin(), times.end())});
        }
    };

    return std::thread(func);
}

static inline void alpha_blit_bgra(const cv::Mat& srcBGRA, cv::Mat& dstBGRA, int x0,
                                   int y0) {
    cv::Rect dstR(x0, y0, srcBGRA.cols, srcBGRA.rows);
    cv::Rect canvas(0, 0, dstBGRA.cols, dstBGRA.rows);
    dstR &= canvas;

    cv::Rect srcR(0, 0, dstR.width, dstR.height);

    cv::Mat dst = dstBGRA(dstR);
    cv::Mat src = srcBGRA(srcR);

    for (int y = 0; y < dstR.height; ++y) {
        const cv::Vec4b* s = src.ptr<cv::Vec4b>(y);
        cv::Vec4b* d = dst.ptr<cv::Vec4b>(y);

        for (int x = 0; x < dstR.width; ++x) {
            float a = s[x][3] * (1.0f / 255.0f);  // src alpha
            d[x][0] = static_cast<uchar>(d[x][0] * (1 - a) + s[x][0] * a);
            d[x][1] = static_cast<uchar>(d[x][1] * (1 - a) + s[x][1] * a);
            d[x][2] = static_cast<uchar>(d[x][2] * (1 - a) + s[x][2] * a);
            // keep dst alpha as-is (or set to 255 if you want)
        }
    }
}

std::thread module_draw_image_3_models_hdmi_fb(draw_image::Module_config&& conf,
                                               draw_image::Module_in_3_models&& in,
                                               draw_image::Module_out_fb&& out) {
    auto func = [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
        RegulusFramebuffer fb(RegulusDisplayType::HDMI);
        cv::Mat fb_display = fb.get_display_mat();

        cv::Mat background =
            cv::imread("./img/[Regulus] Smart Camera_Demo_250226.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(background, background, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        cv::Mat display = cv::Mat::zeros(cv::Size(1920, 1080), CV_8UC3);
        background.copyTo(display({0, 0, 1920, 1080}));

        cv::Mat mask = img::get_mask(background);

        cv::Mat section_org = cv::imread("./img/@Section_org.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_face =
            cv::imread("./img/@Section_face.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_od = cv::imread("./img/@Section_od.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_pose =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_UNCHANGED);

        cv::Mat section_org_alpha = img::get_alpha(section_org);
        cv::Mat section_face_alpha = img::get_alpha(section_face);
        cv::Mat section_od_alpha = img::get_alpha(section_od);
        cv::Mat section_pose_alpha = img::get_alpha(section_pose);

        cv::Mat section_org_color =
            cv::imread("./img/@Section_org.png", cv::IMREAD_COLOR);
        cv::Mat section_face_color =
            cv::imread("./img/@Section_face.png", cv::IMREAD_COLOR);
        cv::Mat section_od_color = cv::imread("./img/@Section_od.png", cv::IMREAD_COLOR);
        cv::Mat section_pose_color =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(section_org_color, section_org_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_face_color, section_face_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_od_color, section_od_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_pose_color, section_pose_color, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        fb.draw_frame(display, 0, 0);

        double ratio = 0;
        bool ratio_inc = true;
        Benchmarker bc;

        while (conf.push_on) {
            if (out.bc_frame) {
                out.bc_frame->end();
                out.bc_frame->start();
            }
            app::InterThreadData data_org, data_face, data_od, data_pose;
            in.queue_in_org.pop(data_org);
            in.queue_in_face.pop(data_face);
            in.queue_in_od.pop(data_od);
            in.queue_in_pose.pop(data_pose);

            if (conf.bc) conf.bc->start();

            cv::Mat frame_org = data_org.frame({0, 0, 576, 384});
            cv::Mat frame_face = data_face.frame({0, 0, 576, 384});
            cv::Mat frame_od = data_od.frame({0, 0, 576, 384});
            cv::Mat frame_pose = data_pose.frame({0, 0, 576, 384});

            CV_Assert(section_org.type() == CV_8UC4);  // BGRA

            alpha_blit_bgra(section_org, frame_org, 20, 312);
            alpha_blit_bgra(section_face, frame_face, 20, 312);
            alpha_blit_bgra(section_od, frame_od, 20, 312);
            alpha_blit_bgra(section_pose, frame_pose, 20, 312);

            auto blit = [&](const cv::Mat& src, int x, int y) {
                cv::Rect roi(x, y, src.cols, src.rows);
                src.copyTo(fb_display(roi), mask(roi));  // masked copy onto section
            };

            blit(frame_org, 70, 212);
            blit(frame_face, 70, 212 + 384 + 30);
            blit(frame_od, 70 + 576 + 30, 212);
            blit(frame_pose, 70 + 576 + 30, 212 + 384 + 30);

            // draw live circle
            cv::Scalar color;

            if (conf.format == "BGR") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(33, 33, 223) * ratio;
            } else if (conf.format == "RGB") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(223, 33, 33) * ratio;
            }

            if (ratio_inc) {
                ratio += 0.066;
                if (ratio >= 1) ratio_inc = false;
            } else {
                ratio -= 0.066;
                if (ratio <= 0) ratio_inc = true;
            }

            cv::Mat circle_area(cv::Size(20, 20), CV_8UC4, cv::Scalar(255, 255, 255));
            cv::circle(circle_area, cv::Point(10, 10), 7, color, cv::FILLED, cv::LINE_AA,
                       0);

            circle_area.copyTo(fb_display(cv::Rect(1643 + 29, 57 + 22, 20, 20)));

            auto times = {data_face.time_point, data_pose.time_point, data_od.time_point,
                          data_org.time_point};
            auto min_time = *std::min_element(times.begin(), times.end());

            if (out.latencies_ms)
                out.latencies_ms->addValue(std::chrono::duration<double, std::milli>{
                    std::chrono::steady_clock::now() - min_time}
                                               .count());

            if (conf.bc) conf.bc->end();
        }
    };

    return std::thread(func);
}

std::thread module_draw_image_4_models_hdmi(draw_image::Module_config&& conf,
                                            draw_image::Module_in_4_models&& in,
                                            draw_image::Module_out&& out) {
    auto func = [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
        cv::Mat background =
            cv::imread("./img/[Regulus] Smart Camera_Demo_250226.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(background, background, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        std::vector<cv::Mat> displays;
        for (int i = 0; i < 3; i++) {
            cv::Mat display = cv::Mat::zeros(cv::Size(1920, 1080), CV_8UC3);
            background.copyTo(display({0, 0, 1920, 1080}));
            displays.push_back(display);
        }

        cv::Mat mask = img::get_mask(background);

        cv::Mat section_seg = cv::imread("./img/@Section_seg.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_face =
            cv::imread("./img/@Section_face.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_od = cv::imread("./img/@Section_od.png", cv::IMREAD_UNCHANGED);
        cv::Mat section_pose =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_UNCHANGED);

        cv::Mat section_seg_alpha = img::get_alpha(section_seg);
        cv::Mat section_face_alpha = img::get_alpha(section_face);
        cv::Mat section_od_alpha = img::get_alpha(section_od);
        cv::Mat section_pose_alpha = img::get_alpha(section_pose);

        cv::Mat section_seg_color =
            cv::imread("./img/@Section_seg.png", cv::IMREAD_COLOR);
        cv::Mat section_face_color =
            cv::imread("./img/@Section_face.png", cv::IMREAD_COLOR);
        cv::Mat section_od_color = cv::imread("./img/@Section_od.png", cv::IMREAD_COLOR);
        cv::Mat section_pose_color =
            cv::imread("./img/@Section_pose.png", cv::IMREAD_COLOR);

        if (conf.format == "BGR") {
        } else if (conf.format == "RGB") {
            cv::cvtColor(section_seg_color, section_seg_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_face_color, section_face_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_od_color, section_od_color, cv::COLOR_BGR2RGB);
            cv::cvtColor(section_pose_color, section_pose_color, cv::COLOR_BGR2RGB);
        } else {
            std::cout << "module_draw_image: unsupported format " << conf.format << "\n";
        }

        int frame_i = 0;
        double ratio = 0;
        bool ratio_inc = true;
        Benchmarker bc;
        while (conf.push_on) {
            if (frame_i == 3) {
                frame_i = 0;
            }
            cv::Mat display = displays[frame_i++];

            app::InterThreadData data_seg, data_face, data_od, data_pose;
            in.queue_in_seg.pop(data_seg);
            in.queue_in_face.pop(data_face);
            in.queue_in_od.pop(data_od);
            in.queue_in_pose.pop(data_pose);

            cv::Mat frame_seg = data_seg.frame;
            cv::Mat frame_face = data_face.frame;
            cv::Mat frame_od = data_od.frame;
            cv::Mat frame_pose = data_pose.frame;

            frame_seg.copyTo(display({70, 212, 576, 384}), mask({70, 212, 576, 384}));
            frame_face.copyTo(display({70, 212 + 384 + 30, 576, 384}),
                              mask({70, 212, 576, 384}));
            frame_od.copyTo(display({70 + 576 + 30, 212, 576, 384}),
                            mask({70, 212, 576, 384}));
            frame_pose.copyTo(display({70 + 576 + 30, 212 + 384 + 30, 576, 384}),
                              mask({70, 212, 576, 384}));

            // draw live circle
            cv::Scalar color;

            if (conf.format == "BGR") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(33, 33, 223) * ratio;
            } else if (conf.format == "RGB") {
                color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                        cv::Scalar(223, 33, 33) * ratio;
            }

            if (ratio_inc) {
                ratio += 0.066;
                if (ratio >= 1) ratio_inc = false;
            } else {
                ratio -= 0.066;
                if (ratio <= 0) ratio_inc = true;
            }

            cv::Mat circle_area(cv::Size(20, 20), CV_8UC3, cv::Scalar(255, 255, 255));
            cv::circle(circle_area, cv::Point(10, 10), 7, color, cv::FILLED, cv::LINE_AA,
                       0);

            circle_area.copyTo(display(cv::Rect(1643 + 29, 57 + 22, 20, 20)));

            auto draw_section = [&display](cv::Mat section_color, cv::Mat section_alpha,
                                           int x, int y) {
                cv::Mat section_area_seg =
                    display({x, y, section_color.cols, section_color.rows});
                for (int y = 0; y < section_color.rows; y++) {
                    for (int x = 0; x < section_color.cols; x++) {
                        cv::Vec3b disp = section_area_seg.at<cv::Vec3b>(y, x);
                        cv::Vec3b sect = section_color.at<cv::Vec3b>(y, x);
                        float alpha = section_alpha.at<uchar>(y, x) / 255.0f;

                        for (int c = 0; c < 3; c++) {
                            disp[c] = disp[c] * (1 - alpha) + sect[c] * alpha;
                        }

                        section_area_seg.at<cv::Vec3b>(y, x) = disp;
                    }
                }
            };

            draw_section(section_seg_color, section_seg_alpha, 70 + 30, 212 + 300 + 20);
            draw_section(section_face_color, section_face_alpha, 70 + 30,
                         212 + 384 + 30 + 300 + 20);
            draw_section(section_od_color, section_od_alpha, 70 + 576 + 30 + 30,
                         212 + 300 + 20);
            draw_section(section_pose_color, section_pose_alpha, 70 + 576 + 30 + 30,
                         212 + 384 + 30 + 300 + 20);

            auto times = {data_face.time_point, data_pose.time_point, data_od.time_point,
                          data_seg.time_point};
        }
    };

    return std::thread(func);
}
}  // namespace app
