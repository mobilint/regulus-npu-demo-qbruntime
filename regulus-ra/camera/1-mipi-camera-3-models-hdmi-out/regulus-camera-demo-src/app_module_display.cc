#include "app_module_display.h"

#include <atomic>
#include <opencv2/opencv.hpp>
#include <thread>

#include "app_type.h"
#include "benchmarker.h"
#include "gst_base_pipeline.h"
#include "gst_kmssink_pipeline.h"
#include "gst_v4l2jpegenc_pipeline.h"
#include "recent_values.h"
#include "udp_sender.h"

namespace app {
std::thread module_kmssink_display(display::Module_config_kmssink&& conf,
                                   display::Module_in&& in, display::Module_out&& out) {
    return std::thread(
        [conf = std::move(conf), in = std::move(in), out = std::move(out)]() {
            gst_wrapper::AppsrcKmssinkPipeline gst_pipeline_display(
                conf.format, conf.width, conf.height, conf.fps);
            gst_pipeline_display.start();

            while (conf.push_on) {
                if (out.bc_frame) {
                    out.bc_frame->end();
                    out.bc_frame->start();
                }

                InterThreadData data_in;
                auto qsc = in.queue.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    std::cout << "queue closed.\n";
                    exit(1);
                }

                if (conf.bc) conf.bc->start();
                gst_wrapper::push_data_to_appsrc(gst_pipeline_display.appsrc,
                                                 data_in.frame.data,
                                                 conf.width * conf.height * 3);
                if (conf.bc) conf.bc->end();

                if (out.latencies_ms)
                    out.latencies_ms->addValue(std::chrono::duration<double, std::milli>{
                        std::chrono::steady_clock::now() - data_in.time_point}
                                                   .count());
            }

            gst_pipeline_display.stop();
        });
}

std::thread module_udp_display(display::Module_config_udp&& conf,
                               display::Module_in&& in) {
    return std::thread([conf = std::move(conf), in = std::move(in)]() {
        gst_wrapper::AppsrcV4l2jpegencAppsinkPipeline gst_pipeline_jpegenc(
            conf.format, conf.width, conf.height, conf.fps, "NV21", conf.width,
            conf.height, conf.fps);

        gst_pipeline_jpegenc.start();

        std::thread push_to_encoder([&]() {
            while (conf.push_on) {
                InterThreadData data_in;
                auto qsc = in.queue.pop(data_in);
                if (qsc == Queue::StatusCode::CLOSED) {
                    std::cout << "queue closed.\n";
                    exit(1);
                }

                gst_wrapper::push_data_to_appsrc(gst_pipeline_jpegenc.appsrc,
                                                 data_in.frame.data,
                                                 conf.width * conf.height * 3);
            }

            gst_pipeline_jpegenc.stop();
        });

        udp_sender sender(conf.ip, conf.port, conf.bandwidth);
        auto send = [&sender](uint8_t* data, unsigned long size) -> void {
            int cnt = sender.send_buffer(data, size);
        };

        Benchmarker bc;
        int prev_sec = 0;
        while (conf.push_on) {
            bc.end();
            bc.start();

            int curr_sec = bc.getRunningTime();
            if (curr_sec - prev_sec > 1) {
                std::cout << "Average FPS: " << bc.getFPS() << std::endl;
                prev_sec = curr_sec;
            }

            gst_wrapper::process_data_from_appsink(gst_pipeline_jpegenc.appsink, send);
        }

        push_to_encoder.join();
    });
}
}  // namespace app
