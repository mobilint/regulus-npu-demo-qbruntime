#include "plot_module.h"

#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

#include "cma_display_ring.h"
#include "dma350_composer.h"
#include "draw_det.h"
#include "draw_pose.h"

namespace app {

// 2x2 panels in the background's left block, each exactly 480x320 (measured from
// Demo_260804.jpg at 1080p), so a frame fills its panel with no margin. All four x are
// multiples of 4, which is what the DMA350 needs at 4 bytes per pixel (see
// CmaDisplayRing). At 3 bytes these same coordinates came out wrong: 120 landed at 117
// with R/G/B rotated, 644 at 640.
static const cv::Rect kPanelOrg{120, 280, 480, 320};
static const cv::Rect kPanelOd{644, 280, 480, 320};
static const cv::Rect kPanelFace{120, 644, 480, 320};
static const cv::Rect kPanelPose{644, 644, 480, 320};

// The background carries a label pill inside each panel; the frame paste covers it, so
// draw_pill redraws it every frame. Offset within the panel is (26, 238) -- taken from
// the three pills the design places consistently (the baked pose pill sits 11px high, but
// it is never visible because the frame covers the whole panel, so all four are drawn
// aligned here).
static const cv::Point kLabelOrg{146, 518};
static const cv::Point kLabelOd{670, 518};
static const cv::Point kLabelFace{146, 882};
static const cv::Point kLabelPose{670, 882};

// Panels have ~26px rounded corners. The frame paste is a plain rectangle, so the four
// corner notches would come out squared against the dark background. Restore just those
// notches from the background: 4 corners x 32x32 per panel is ~2% of a panel copy, unlike
// a full masked copy.
static const int kCornerSz = 32;
static const cv::Rect kCornerOff[4] = {
    {0, 0, kCornerSz, kCornerSz},
    {480 - kCornerSz, 0, kCornerSz, kCornerSz},
    {0, 320 - kCornerSz, kCornerSz, kCornerSz},
    {480 - kCornerSz, 320 - kCornerSz, kCornerSz, kCornerSz},
};

// A section header ("label pill") drawn over the frame every plot iteration. Of its
// pixels 93% are fully opaque, 5% fully transparent and only 1.4% are the antialiased
// outline, so a masked copy handles the opaque body and per-pixel blending is needed for
// the outline alone. Output is bit-identical to blending every pixel; measured on the
// board this is 2.19ms -> ~0.44ms for the four pills, which matters because plot has
// under 1ms of headroom against the 30fps camera.
struct Pill {
    cv::Mat color;   // RGB, pill-sized
    cv::Mat opaque;  // 8U mask, 255 where alpha == 255
    struct Edge {
        int r, c;
        float a;
    };
    std::vector<Edge> edge;  // 0 < alpha < 255, in row order
    cv::Point at;            // top-left on the 1920x1080 display
};

static Pill make_pill(const std::string& path, cv::Point at) {
    Pill p;
    p.at = at;
    const cv::Mat rgba = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (rgba.empty() || rgba.channels() != 4) {
        std::cerr << "[plot] pill load failed (need RGBA): " << path << "\n";
        return p;
    }
    std::vector<cv::Mat> ch;
    cv::split(rgba, ch);
    const cv::Mat& alpha = ch[3];
    cv::cvtColor(rgba, p.color, cv::COLOR_BGRA2RGBA);  // display buffer is RGBX
    cv::threshold(alpha, p.opaque, 254, 255, cv::THRESH_BINARY);
    for (int r = 0; r < alpha.rows; r++)
        for (int c = 0; c < alpha.cols; c++) {
            const uchar a = alpha.at<uchar>(r, c);
            if (a != 0 && a != 255) p.edge.push_back({r, c, a / 255.0f});
        }
    return p;
}

static void draw_pill(cv::Mat& display, const Pill& p) {
    if (p.color.empty()) return;
    cv::Mat area = display({p.at.x, p.at.y, p.color.cols, p.color.rows});
    p.color.copyTo(area, p.opaque);  // opaque body: plain overwrite
    for (const Pill::Edge& e : p.edge) {
        cv::Vec4b& dst = area.at<cv::Vec4b>(e.r, e.c);
        const cv::Vec4b src = p.color.at<cv::Vec4b>(e.r, e.c);
        for (int i = 0; i < 3; i++) dst[i] = dst[i] * (1 - e.a) + src[i] * e.a;
    }
}

std::thread plot_module(PlotConfig cfg, Queue& frame_in, Queue& face_in, Queue& pose_in,
                        Queue& od_in, Queue& out) {
    return std::thread([cfg, &frame_in, &face_in, &pose_in, &od_in, &out]() {
        try {
            const std::string bg_path = "./img/[Regulus] Smart Camera_Demo_260804.jpg";
            cv::Mat background = cv::imread(bg_path, cv::IMREAD_COLOR);
            if (background.empty()) {
                std::cerr << "[plot] cannot read background " << bg_path << " -> stopping\n";
                if (cfg.run) cfg.run->stop(StopReason::Plot);
                return;
            }
            std::cout << "[plot] background " << bg_path << " (" << background.cols << "x"
                      << background.rows << ")\n";
            cv::resize(background, background, cv::Size(1920, 1080), 0, 0,
                       background.cols >= 1920 ? cv::INTER_AREA : cv::INTER_CUBIC);
            cv::cvtColor(background, background,
                         cv::COLOR_BGR2RGBA);  // display buffer is RGBX

            // Corner notch masks: inside a panel the background is >=83 (gray 180, label
            // pill 83..226) and outside it is 34, so 60 splits them. Same shape for all
            // four panels.
            cv::Mat corner_mask[4];
            {
                cv::Mat panel_gray;
                cv::cvtColor(background(kPanelOrg), panel_gray, cv::COLOR_RGBA2GRAY);
                for (int i = 0; i < 4; i++)
                    cv::threshold(panel_gray(kCornerOff[i]), corner_mask[i], 60, 255,
                                  cv::THRESH_BINARY_INV);
            }

            // Display frames live in a CMA(dma-heap) ring: kmssink scans them out with
            // zero copy (no 6MB/frame kmssink copy) and the DMA350 writes the panels into
            // them. Both need a dma-buf, so unlike before there is no plain-heap fallback
            // -- if the ring cannot be allocated the stage stops the demo, the same
            // policy as pre_module and plot_fb_module. Pre-bake the background into every
            // buffer (panels overwrite their region every frame; the background parts
            // persist, since a compose touches only its rectangle).
            const int W = 1920, H = 1080;
            CmaDisplayRing ring(W, H, 4);
            if (!ring.ok()) {
                std::cerr << "[plot] CMA display ring unavailable -> stopping\n";
                if (cfg.run) cfg.run->stop(StopReason::Plot);
                return;
            }
            const int ndisp = ring.count();
            for (int i = 0; i < ndisp; ++i) {
                ring.begin_cpu(i);
                background.copyTo(ring.mat(i));
                ring.end_cpu(i);
            }

            // Panel writes run on the DMA350 instead of the CPU. Source is the frame pre
            // produced (480x320 packed RGB24 in its own CMA buffer, so offset 0),
            // destination is a ring buffer.
            Dma350Composer composer(kPanelOrg.width, kPanelOrg.height, W, H, 4);
            if (!composer.ok()) {
                std::cerr << "[plot] DMA350 composer unavailable -> stopping\n";
                if (cfg.run) cfg.run->stop(StopReason::Plot);
                return;
            }

            const std::vector<Pill> pills = {
                make_pill("./img/@Section_org.png", kLabelOrg),
                make_pill("./img/@Section_face.png", kLabelFace),
                make_pill("./img/@Section_od.png", kLabelOd),
                make_pill("./img/@Section_pose.png", kLabelPose),
            };

            int frame_i = 0;
            double ratio = 0;
            bool ratio_inc = true;

            // Panels index the pills too: org, face, od, pose.
            const cv::Rect panels[4] = {kPanelOrg, kPanelFace, kPanelOd, kPanelPose};

            while (cfg.run->running()) {
                Packet frame, face, pose, od;
                if (frame_in.pop(frame) == Queue::CLOSED) break;
                if (cfg.tl) cfg.tl->plot_in.addValue(age_ms(frame.time_point));

                const int slot = frame_i++ % ndisp;
                cv::Mat& display = ring.mat(slot);

                // Write the frame into the four panels on the DMA350. Each compose blocks
                // until the hardware reports the job done, so the four are serial, but
                // they cost ~0.9 ms each instead of the ~6 ms per panel the CPU needed,
                // and no core is busy meanwhile. Measured on the board: 24.2 ms of CPU on
                // two cores -> 3.5 ms of DMA350.
                // The panels need only the frame, not its detections, so they are composed
                // before waiting for those: the DMA350 runs while the NPU is still on this
                // frame, and the compose leaves the latency path.
                if (cfg.bc_cpy4) cfg.bc_cpy4->start();
                bool composed = true;
                for (const cv::Rect& p : panels)
                    composed = composer.compose(frame.frame_dmabuf_fd, 0, ring.fd(slot),
                                                p.x, p.y) &&
                               composed;
                if (cfg.bc_cpy4) cfg.bc_cpy4->end();
                if (!composed) {
                    std::cerr
                        << "[plot] DMA350 compose failed (no fallback); dropping frame\n";
                    if (cfg.drops) (*cfg.drops)++;
                    cfg.run->stop(StopReason::Plot);
                    break;
                }

                // Rounded corner notches (also trimming any overlay that spilled into
                // them), then the label pill, for one panel.
                auto restore_panel = [&](int k) {
                    for (int i = 0; i < 4; i++) {
                        const cv::Rect r{panels[k].x + kCornerOff[i].x,
                                         panels[k].y + kCornerOff[i].y, kCornerSz,
                                         kCornerSz};
                        background(r).copyTo(display(r), corner_mask[i]);
                    }
                    draw_pill(display, pills[k]);
                };

                // Everything that does not depend on the detections also runs before
                // waiting for them, while the NPU and DSP are still on this frame: taking
                // the buffer back for the CPU, the org panel's corners and pill (it gets
                // no overlay), and the live dot.
                if (cfg.bc_prep) cfg.bc_prep->start();
                // Invalidates the CPU's stale copy of the lines the DMA350 just wrote, so
                // the CPU draws on top of the new frame rather than on what the cache
                // still held.
                ring.begin_cpu(slot);
                restore_panel(0);

                // animated "live" indicator dot (RGB: red = 223,33,33)
                cv::Scalar color = cv::Scalar(255, 255, 255) * (1 - ratio) +
                                   cv::Scalar(223, 33, 33) * ratio;
                if (ratio_inc) {
                    ratio += 0.066;
                    if (ratio >= 1) ratio_inc = false;
                } else {
                    ratio -= 0.066;
                    if (ratio <= 0) ratio_inc = true;
                }
                cv::Mat dot(cv::Size(20, 20), CV_8UC4, cv::Scalar(255, 255, 255));
                cv::circle(dot, cv::Point(10, 10), 7, color, cv::FILLED, cv::LINE_AA, 0);
                // centered on the "Demo Live" pill's red dot in the background (center
                // 1682,89)
                dot.copyTo(display(cv::Rect(1672, 79, 20, 20)));
                if (cfg.bc_prep) cfg.bc_prep->end();

                if (face_in.pop(face) == Queue::CLOSED) break;
                if (pose_in.pop(pose) == Queue::CLOSED) break;
                if (od_in.pop(od) == Queue::CLOSED) break;
                if (cfg.tl) cfg.tl->dets_in.addValue(age_ms(frame.time_point));

                // From here on the work waits for the detections, so it is timed as
                // `draw`: the three overlays (drawn in place through a display ROI, which
                // clips them to their panel), their panels' corners and pills put back
                // over them, and the cache flush that makes it all visible (the flush
                // scales with how many cache lines were dirtied).
                if (cfg.bc_draw3) cfg.bc_draw3->start();
                // One task per overlaid panel. The panels are disjoint and drawing is
                // glyph/line rasterisation, so three cores take them. Three, not four: a
                // fourth busy thread under SCHED_FIFO competes with the pipeline's own on
                // this four-core board (measured 28 vs 22 ms end to end).
#pragma omp parallel sections num_threads(3)
                {
#pragma omp section
                    {
                        cv::Mat roi = display(kPanelFace);
                        draw_det(roi, face.dets, FACE_LABELS);
                        restore_panel(1);
                    }
#pragma omp section
                    {
                        cv::Mat roi = display(kPanelOd);
                        draw_det(roi, od.dets, COCO_LABELS);
                        restore_panel(2);
                    }
#pragma omp section
                    {
                        cv::Mat roi = display(kPanelPose);
                        draw_pose(roi, pose.dets);
                        restore_panel(3);
                    }
                }
                ring.end_cpu(slot);  // flush CPU writes so display HW sees them
                if (cfg.bc_draw3) cfg.bc_draw3->end();

                if (cfg.tl) cfg.tl->plot_out.addValue(age_ms(frame.time_point));

                Packet o;
                o.frame = display;
                o.dmabuf_fd = ring.fd(slot);
                o.time_point = frame.time_point;
                out.push(std::move(o));
            }
        } catch (const std::exception& error) {
            std::cerr << "[plot] " << error.what() << "\n";
            cfg.run->stop(StopReason::Plot);
        } catch (...) {
            std::cerr << "[plot] unknown exception\n";
            cfg.run->stop(StopReason::Plot);
        }
    });
}

}  // namespace app
