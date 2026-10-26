#include <qbruntime/type.h>
#include <sp500/dsp.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "argparse.h"
#include "benchmarker.h"
#include "capture_module.h"
#include "decode_module.h"
#include "display_module.h"
#include "infer_module.h"
#include "plot_module.h"
#include "pre_module.h"
#include "qbmodel.h"
#include "recent_values.h"
#include "resize_crop.h"
#include "run_state.h"
#include "types.h"
#include "usb_capture_source.h"

// Pipeline: pre -> infer -> decode+post (DSP) -> plot -> display, per model
// (face/pose/od). The fixed YOLO11s models run raw heads; the SP500 DSP does DFL
// decode + NMS in one call.

volatile std::sig_atomic_t stop_signal = 0;

void signal_handler(int) { stop_signal = 1; }

bool reload_sp500_firmware() {
    const pid_t child = fork();
    if (child == 0) {
        execlp("dsp_firmware_load", "dsp_firmware_load", static_cast<char*>(nullptr));
        _exit(127);
    }
    if (child < 0) return false;
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

void printUsage() {
    std::cout << "Usage: demo\n";
    std::cout << "    -d 4, USB camera device number\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto [dev_paths, cam_w, cam_h, cam_fps] = parse_args(argc, argv, 1, printUsage);

    const sp500_status dsp_status = sp500_dsp_open(0);
    if (dsp_status != SP500_OK) {
        std::cerr << "sp500_dsp_open() failed: " << sp500_status_string(dsp_status)
                  << "\n";
        return 1;
    }
    std::cout << "qbruntime version=" << mobilint::getQbRuntimeVersion()
              << " git=" << mobilint::getQbRuntimeGitVersion() << "\n";
    std::cout << "SP500 header=" << SP500_VERSION_MAJOR << "." << SP500_VERSION_MINOR
              << "." << SP500_VERSION_PATCH << " library=" << sp500_version_string()
              << "\n";

    uint64_t start_ns = 0, end_ns = 0, input_start = 0, input_end = 0;
    uint64_t output_start = 0, output_end = 0;
    uint64_t dropped_final = 0, latency_count = 0, infer_count[3]{}, decode_count[3]{};
    double latency_mean = 0, latency_min = 0, latency_max = 0;
    double infer_ms[3]{}, decode_ms[3]{};
    bool fatal_result = false;
    bool timeout_result = false;
    auto print_measurement = [&] {
        const uint64_t elapsed_ns = end_ns - start_ns;
        const double elapsed_s = static_cast<double>(elapsed_ns) / 1000000000.0;
        const uint64_t input_delta = input_end - input_start;
        const uint64_t output_delta = output_end - output_start;
        std::cout
            << "MEASUREMENT elapsed_ns=" << elapsed_ns << " start_ns=" << start_ns
            << " end_ns=" << end_ns << " input_start=" << input_start
            << " input_end=" << input_end << " input_delta=" << input_delta
            << " input_fps=" << (elapsed_s > 0.0 ? input_delta / elapsed_s : 0.0)
            << " output_start=" << output_start << " output_end=" << output_end
            << " output_delta=" << output_delta
            << " output_fps=" << (elapsed_s > 0.0 ? output_delta / elapsed_s : 0.0)
            << " dropped_final=" << dropped_final << " dropped_max=" << dropped_final
            << " latency_count=" << latency_count << " latency_mean_ms=" << latency_mean
            << " latency_min_ms=" << latency_min << " latency_max_ms=" << latency_max
            << " face_infer_count=" << infer_count[0] << " face_infer_ms=" << infer_ms[0]
            << " face_decode_count=" << decode_count[0]
            << " face_decode_ms=" << decode_ms[0]
            << " pose_infer_count=" << infer_count[1] << " pose_infer_ms=" << infer_ms[1]
            << " pose_decode_count=" << decode_count[1]
            << " pose_decode_ms=" << decode_ms[1] << " od_infer_count=" << infer_count[2]
            << " od_infer_ms=" << infer_ms[2] << " od_decode_count=" << decode_count[2]
            << " od_decode_ms=" << decode_ms[2]
            << " fatal_error=" << (fatal_result ? 1 : 0)
            << " timeout_error=" << (timeout_result ? 1 : 0) << std::endl;
    };
    auto set_failure_timestamp = [&] {
        if (end_ns != 0) return;
        const auto now = std::chrono::steady_clock::now();
        start_ns = end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                now.time_since_epoch())
                                .count();
    };
    try {
        app::RunState run;
        auto maccel = app::QbMultiModel({"./mxq/yolov11s-face.mxq",  // models[0]
                                         "./mxq/yolo11s-pose.mxq",  // models[1]
                                         "./mxq/yolo11s.mxq"});     // models[2]

        const int npu_w = 480, npu_h = 320;
        const float conf_thres = 0.5f;  // default confidence threshold (all models)

        app::Queue capture_q, frame_q, display_q;
        app::Buffer reposition_buf;
        app::Queue raw_q[3],
            post_q[3];  // 0=face, 1=pose, 2=od (decode writes final dets to post_q)

        Benchmarker bc_capture, bc_pre, bc_infer[3], bc_decode[3], bc_plot_prep, bc_display,
            bc_plot_cpy4, bc_plot_draw3;
        // The only two real throughputs: frames arriving from the camera, and frames
        // leaving to the display. Everything else below is a per-frame cost, not a rate.
        Benchmarker bc_camera, bc_frame;
        RecentValues<double, 1000> latencies_ms;
        // Frame age at each handoff, so the latency breakdown below adds up to the total.
        app::LatencyTimeline timeline;
        // Dropped frames never reach the display, so the output rate alone cannot reveal
        // them.
        std::atomic<uint64_t> drop_capture{0}, drop_pre{0}, drop_plot{0}, drop_display{0};
        std::atomic<uint64_t> input_frames{0}, output_frames{0};
        std::atomic<uint64_t> infer_activity[3]{}, decode_activity[3]{};
        bool measurement_started = false;

        std::vector<std::thread> threads;
        threads.reserve(10);
        auto stop_and_join = [&] {
            maccel.cancel_input();
            capture_q.close();
            frame_q.close();
            display_q.close();
            reposition_buf.close();
            for (auto& queue : raw_q) queue.close();
            for (auto& queue : post_q) queue.close();
            for (auto& thread : threads)
                if (thread.joinable()) thread.join();
        };
        auto capture_measurement_end = [&] {
            const auto measurement_end = std::chrono::steady_clock::now();
            if (!measurement_started) {
                input_start = input_frames.load(std::memory_order_relaxed);
                output_start = output_frames.load(std::memory_order_relaxed);
                start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               measurement_end.time_since_epoch())
                               .count();
            }
            input_end = input_frames.load(std::memory_order_relaxed);
            output_end = output_frames.load(std::memory_order_relaxed);
            end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         measurement_end.time_since_epoch())
                         .count();
        };
        auto collect_results = [&] {
            fatal_result = run.failed();
            // Drop counters are monotonic. Their final sum is also the maximum observed
            // sum.
            dropped_final = drop_capture.load() + drop_pre.load() + drop_plot.load() +
                            drop_display.load();
            latency_count = static_cast<uint64_t>(latencies_ms.getCount());
            latency_mean = latencies_ms.getAvg();
            latency_min = latencies_ms.getMin();
            latency_max = latencies_ms.getMax();
            for (int i = 0; i < 3; ++i) {
                infer_count[i] = infer_activity[i].load();
                decode_count[i] = decode_activity[i].load();
                infer_ms[i] = bc_infer[i].getAvgSec() * 1000.0;
                decode_ms[i] = bc_decode[i].getAvgSec() * 1000.0;
            }
        };
        auto finalize_timeout = [&] {
            timeout_result = run.timed_out();
            if (!timeout_result) return;
            std::cerr << "SP500_TIMEOUT_RELOAD_REQUIRED\n";
            if (!reload_sp500_firmware()) {
                std::cerr
                    << "SP500 firmware reload failed; preserving DSP-exposed buffers.\n";
                fatal_result = true;
                print_measurement();
                std::_Exit(2);
            }
            run.release_timeout_buffers_after_reload();
        };

        try {
            // capture (own stage/thread) -> pre. Source is fixed here at compile time
            // (USB); add another input by constructing a different CaptureSource. MAPI
            // scales cam WxH -> cover-scaled WxH.
            app::CoverCrop cc = app::cover_crop(cam_w, cam_h, npu_w, npu_h);
            // MAPI converts to RGBx: the whole path runs at 4 bytes per pixel so the
            // DMA350's 16-byte destination rounding lands on a pixel boundary (see
            // CmaDisplayRing).
            auto usb_src = std::make_unique<app::UsbCaptureSource>(
                dev_paths[0].c_str(), cam_w, cam_h, cam_fps, cc.scaled_w, cc.scaled_h,
                "RGBx");
            threads.push_back(app::capture_module(
                {&run, &bc_capture, &bc_camera, &drop_capture, &input_frames, &timeline},
                std::move(usb_src), capture_q));

            // pre
            // bpp 4: pre drops the unused byte on the way into the 3-channel NPU input.
            threads.push_back(app::pre_module({cam_w, cam_h, npu_w, npu_h, &maccel, &run,
                                               &bc_pre, &drop_pre, 4, &timeline},
                                              capture_q, frame_q, reposition_buf));

            // infer (one per model, all read the shared pinned input)
            const char* branches[] = {"face", "pose", "object"};
            for (int i = 0; i < 3; i++)
                threads.push_back(
                    app::infer_module({&maccel, i, &run, branches[i], &infer_activity[i],
                                       &bc_infer[i], i == 2 ? &timeline : nullptr},
                                      reposition_buf, raw_q[i]));

            // decode + NMS on the DSP in one call (per-model iou: face 0.45, pose 0.65,
            // od 0.70). Replaces the former host DFL decode + host NMS stages; output is
            // final dets -> post_q.
            threads.push_back(app::decode_module(
                {app::DecodeKind::Det, 1, npu_h, npu_w, conf_thres, 0.45f, &run, "face",
                 &decode_activity[0], &bc_decode[0]},
                raw_q[0], post_q[0]));
            threads.push_back(app::decode_module(
                {app::DecodeKind::Pose, 1, npu_h, npu_w, conf_thres, 0.65f, &run, "pose",
                 &decode_activity[1], &bc_decode[1]},
                raw_q[1], post_q[1]));
            threads.push_back(app::decode_module(
                {app::DecodeKind::Det, 80, npu_h, npu_w, conf_thres, 0.70f, &run,
                 "object", &decode_activity[2], &bc_decode[2], &timeline},
                raw_q[2], post_q[2]));

            // plot (compose 4-panel dashboard) + display (HDMI)
            threads.push_back(app::plot_module(
                {&run, &bc_plot_prep, &bc_plot_cpy4, &bc_plot_draw3, &drop_plot, &timeline},
                frame_q, post_q[0], post_q[1], post_q[2], display_q));
            threads.push_back(app::display_module(
                {"RGBx", 1920, 1080, 30, &run, &bc_display, &bc_frame, &latencies_ms,
                 &output_frames, &drop_display, &timeline},
                display_q));

            auto clk_prev = std::chrono::steady_clock::now();
            while (run.running()) {
                // Sleep between checks. The report below runs once a second, so spinning
                // here buys nothing and costs a whole core of the four -- measured as one
                // core pinned at ~99%%, which starves the pipeline threads and shows up
                // as multi-ms waits on frame_q.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (stop_signal != 0) run.stop(app::StopReason::Signal);
                auto clk = std::chrono::steady_clock::now();
                if ((clk - clk_prev).count() / 1000000000 > 1) {
                    // Three blocks, in the order the question is usually asked:
                    //   throughput  -- how often a frame moves (frames per second, and
                    //   the interval) latency     -- how long ONE frame takes from the
                    //   camera to the screen stage time  -- where that time went, stage
                    //   by stage
                    // Throughput and latency are independent: one frame can leave every
                    // 33 ms while each one takes 100 ms end to end, because the stages
                    // hold different frames at the same time. So latency is NOT
                    // 1/throughput and the two must not be mixed.
                    auto ms = [](const Benchmarker& b) { return b.getAvgSec() * 1000.0; };
                    auto rate = [](const char* name, double fps, double interval_ms) {
                        std::cout << std::left << std::setw(22) << name << std::right
                                  << std::setw(8) << std::fixed << std::setprecision(2)
                                  << fps << " FPS   (" << std::setprecision(3)
                                  << interval_ms << " ms)\n";
                    };
                    // indent > 0 marks a sub-item of the stage line above it.
                    auto cost = [](const char* name, double v, int indent) {
                        std::cout << std::string(indent, ' ') << std::left
                                  << std::setw(22 - indent) << name << std::right
                                  << std::setw(8) << std::fixed << std::setprecision(3)
                                  << v << " ms   (" << std::setw(9)
                                  << std::setprecision(2) << (v > 0 ? 1000.0 / v : 0.0)
                                  << " FPS)\n";
                    };

                    const double lat = latencies_ms.getAvg();
                    const uint64_t dc = drop_capture.load(), dp = drop_pre.load(),
                                   dl = drop_plot.load(), dd = drop_display.load();

                    std::cout << "--- throughput ---\n";
                    rate("input frame rate:", bc_camera.getAvgFPS(), ms(bc_camera));
                    rate("output frame rate:", bc_frame.getAvgFPS(), ms(bc_frame));
                    std::cout << std::left << std::setw(22) << "dropped:" << std::right
                              << std::setw(8) << (dc + dp + dl + dd) << "       (capture "
                              << dc << ", pre " << dp << ", plot " << dl << ", display "
                              << dd << ")\n";

                    std::cout << "--- latency ---\n";
                    // Measured from the moment appsink handed the frame over, not from
                    // the sensor: exposure, USB transfer, CODAJ decode and MAPI convert
                    // all happen before that and are not included. True glass-to-glass is
                    // larger by an unmeasured amount.
                    std::cout << std::left << std::setw(22)
                              << "appsink to screen:" << std::right << std::setw(8)
                              << std::fixed << std::setprecision(3) << lat << " ms\n";

                    // Where that time went. These lines ADD UP to the total, unlike the
                    // stage times below: they are checkpoints on the frame's own path, so
                    // every millisecond falls in exactly one line -- including the waits
                    // that no stage records.
                    auto part = [](const char* name, double v) {
                        std::cout << "    " << std::left << std::setw(18) << name
                                  << std::right << std::setw(8) << std::fixed
                                  << std::setprecision(3) << v << " ms\n";
                    };
                    // Two nesting levels below `part`. A line's children add up to it, so
                    // `ready for plot` is printed as the parent of the six steps that
                    // make it.
                    auto sub = [](const char* name, double v) {
                        std::cout << "        " << std::left << std::setw(18) << name
                                  << std::right << std::setw(6) << std::fixed
                                  << std::setprecision(3) << v << " ms\n";
                    };
                    auto sub2 = [](const char* name, double v) {
                        std::cout << "            " << std::left << std::setw(18) << name
                                  << std::right << std::setw(2) << std::fixed
                                  << std::setprecision(3) << v << " ms\n";
                    };
                    const double t_cap_out = timeline.capture_out.getAvg();
                    const double t_cap_pushed = timeline.capture_pushed.getAvg();
                    const double t_pre_in = timeline.pre_in.getAvg();
                    const double t_pre_out = timeline.pre_out.getAvg();
                    const double t_pre_pushed = timeline.pre_pushed.getAvg();
                    const double t_plot_in = timeline.plot_in.getAvg();
                    const double t_dets_in = timeline.dets_in.getAvg();
                    const double t_plot_out = timeline.plot_out.getAvg();
                    // Named after the code: a line with a queue name is time the frame
                    // sat there with nobody working on it. work / queue / work / queue /
                    // queue / work / queue / work.
                    const double t_disp_in = timeline.display_in.getAvg();
                    part("capture:", t_cap_out);
                    // A queue splits in two: (push) is the producer blocked because the
                    // queue was still full, (wake) is the frame sitting there until the
                    // consumer woke up and took it.
                    part("capture_q (push):", t_cap_pushed - t_cap_out);
                    part("capture_q (wake):", t_pre_in - t_cap_pushed);
                    part("pre:", t_pre_out - t_pre_in);
                    part("frame_q (push):", t_pre_pushed - t_pre_out);
                    part("frame_q (wake):", t_plot_in - t_pre_pushed);
                    part("post_q x3:", t_dets_in - t_plot_in);
                    // Nested under post_q x3: the detection branch for od, the model plot
                    // waits longest for. These are in the source frame's clock and do NOT
                    // add into the total -- the frame never travels this path, a copy of
                    // it does. They say where that wait comes from.
                    const double d_inf_in = timeline.det_infer_in.getAvg();
                    const double d_inf_out = timeline.det_infer_out.getAvg();
                    const double d_inf_pushed = timeline.det_infer_pushed.getAvg();
                    const double d_dec_in = timeline.det_decode_in.getAvg();
                    const double d_dec_out = timeline.det_decode_out.getAvg();
                    const double d_dec_pushed = timeline.det_decode_pushed.getAvg();
                    // The parent first: when the coordinates were finished and put in
                    // post_q. The six lines under it are the steps that add up to it.
                    sub("ready for plot:", d_dec_pushed);
                    sub2("pre -> infer:", d_inf_in);
                    sub2("infer (NPU):", d_inf_out - d_inf_in);
                    sub2("raw_q (push):", d_inf_pushed - d_inf_out);
                    sub2("raw_q (wake):", d_dec_in - d_inf_pushed);
                    sub2("decode (DSP):", d_dec_out - d_dec_in);
                    sub2("post_q (push):", d_dec_pushed - d_dec_out);
                    part("plot:", t_plot_out - t_dets_in);
                    // The LCD demo has no display lines: plot writes the scanout buffer
                    // itself, so its last checkpoint IS the total. Here plot hands the
                    // frame to a display stage first.
                    part("display_q:", t_disp_in - t_plot_out);
                    part("display:", lat - t_disp_in);

                    // infer / decode+post parents are the SUM of their sub-items: three
                    // models share one piece of hardware, so the sum is the work that
                    // hardware does per frame. plot is a sum too: cpy4 and prep run before
                    // it waits for the detections and draw after, so it is three spans with
                    // a wait between them rather than one measured stage. The FPS here
                    // is 1/time: what the stage could sustain if it never waited. It is a
                    // capacity, not a frame rate, and the FPS column cannot be summed.
                    std::cout
                        << "--- stage time (per frame; FPS = stage capacity, not frame "
                           "rate) ---\n";
                    cost("capture:", ms(bc_capture), 0);
                    cost("pre:", ms(bc_pre), 0);
                    cost("infer:", ms(bc_infer[0]) + ms(bc_infer[1]) + ms(bc_infer[2]),
                         0);
                    cost("face:", ms(bc_infer[0]), 4);
                    cost("pose:", ms(bc_infer[1]), 4);
                    cost("od:", ms(bc_infer[2]), 4);
                    cost("decode+post:",
                         ms(bc_decode[0]) + ms(bc_decode[1]) + ms(bc_decode[2]), 0);
                    cost("face:", ms(bc_decode[0]), 4);
                    cost("pose:", ms(bc_decode[1]), 4);
                    cost("od:", ms(bc_decode[2]), 4);
                    cost("plot:",
                         ms(bc_plot_cpy4) + ms(bc_plot_prep) + ms(bc_plot_draw3), 0);
                    cost("cpy4:", ms(bc_plot_cpy4), 4);
                    cost("prep:", ms(bc_plot_prep), 4);
                    cost("draw:", ms(bc_plot_draw3), 4);
                    cost("display:", ms(bc_display), 0);
                    std::cout << "\n";
                    if (!measurement_started &&
                        output_frames.load(std::memory_order_relaxed) >= 1) {
                        const auto measurement_start = std::chrono::steady_clock::now();
                        measurement_started = true;
                        input_start = input_frames.load(std::memory_order_relaxed);
                        output_start = output_frames.load(std::memory_order_relaxed);
                        start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       measurement_start.time_since_epoch())
                                       .count();
                    }
                    clk_prev = clk;
                }
            }
        } catch (...) {
            run.stop(app::StopReason::Initialization);
            stop_and_join();
            capture_measurement_end();
            collect_results();
            finalize_timeout();
            throw;
        }

        capture_measurement_end();
        stop_and_join();
        collect_results();
        finalize_timeout();
    } catch (const std::exception& error) {
        std::cerr << "Initialization failed: " << error.what() << "\n";
        set_failure_timestamp();
        fatal_result = true;
    } catch (...) {
        std::cerr << "Initialization failed with an unknown exception.\n";
        set_failure_timestamp();
        fatal_result = true;
    }

    print_measurement();
    return fatal_result ? 1 : 0;
}
