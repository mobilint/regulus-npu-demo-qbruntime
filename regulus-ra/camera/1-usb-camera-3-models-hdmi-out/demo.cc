#include <gst/gst.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <thread>

#include "app_maccel.h"
#include "app_module_capture.h"
#include "app_module_capture_and_preprocess.h"
#include "app_module_display.h"
#include "app_module_draw_image.h"
#include "app_module_infer.h"
#include "app_module_post_process.h"
#include "app_module_preprocess.h"
#include "app_module_transfer.h"
#include "app_parse_argv.h"
#include "app_type.h"

std::atomic<bool> push_on(true);

void sigintHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    push_on.store(false);
    exit(-1);
}

void printUsage() {
    std::cout << "Usage: demo\n";
    std::cout << "    -d 4, USB camera device number \n";
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigintHandler);

    auto [dev_paths, camera_width, camera_height, camera_fps] =
        parse_args(argc, argv, 1, printUsage);

    std::string mxq_dir = "./mxq/";

    auto maccel = app::MaccelMultiModel({mxq_dir + "yolov8n-face_h384_w576_reg.mxq",
                                         mxq_dir + "yolov8s-pose_h384_w576_reg.mxq",
                                         mxq_dir + "yolov8s-det_h384_w576_reg.mxq"});
    auto models = maccel.get_models();
    auto reposition_buffer = maccel.get_reposition_buffer();

    int npu_w = 576, npu_h = 384;

    std::array<app::Buffer, 2> buffers;
    std::array<app::Queue, 8> queues;
    std::vector<std::thread> threads;

    std::array<Benchmarker, 11> bc;

    // Capture
    threads.push_back(app::module_opencv_capture_kmssink(
        {dev_paths[0], camera_width, camera_height, camera_fps, npu_w, npu_h,
         &reposition_buffer, push_on, &bc[0]},
        {buffers[0]}));

    // Preprocess and reposition
    threads.push_back(app::module_preprocess_and_reposition_kmssink(
        {npu_w, npu_h, push_on, &bc[1]}, {buffers[0]}, {buffers[1], queues[0]}));

    // Infer
    // face
    threads.push_back(app::module_infer_reposition_buffer({models[0], push_on, &bc[3]},
                                                          {buffers[1]}, {queues[1]}));

    // pose
    threads.push_back(app::module_infer_reposition_buffer({models[1], push_on, &bc[4]},
                                                          {buffers[1]}, {queues[2]}));

    // od
    threads.push_back(app::module_infer_reposition_buffer({models[2], push_on, &bc[5]},
                                                          {buffers[1]}, {queues[3]}));

    // Postprocess
    // face
    threads.push_back(app::module_post_process_face_hdmi({npu_h, npu_w, push_on, &bc[6]},
                                                         {queues[1]}, {queues[4]}));

    // pose
    threads.push_back(app::module_post_process_pose_hdmi({npu_h, npu_w, push_on, &bc[7]},
                                                         {queues[2]}, {queues[5]}));

    // od
    threads.push_back(app::module_post_process_od_hdmi({npu_h, npu_w, push_on, &bc[8]},
                                                       {queues[3]}, {queues[6]}));

    // construct image
    threads.push_back(app::module_draw_image_3_models_hdmi(
        {"BGR", push_on, &bc[9]}, {queues[0], queues[4], queues[5], queues[6]},
        {queues[7]}));

    // display
    Benchmarker bc_frame;
    RecentValues<double, 1000> latencies_ms;
    threads.push_back(
        app::module_kmssink_display({"BGR", 1920, 1080, 30, push_on, &bc[10]},
                                    {queues[7]}, {&bc_frame, &latencies_ms}));

    auto clk_prev = std::chrono::steady_clock::now();
    while (push_on) {
        auto clk = std::chrono::steady_clock::now();
        int elapse_time = (clk - clk_prev).count() / 1000000000;
        if (elapse_time > 1) {
            std::cout << "Total frame rate: " << bc_frame.getAvgFPS() << " FPS"
                      << std::endl;
            std::cout << "Total frame interval: " << bc_frame.getAvgSec() << " sec"
                      << std::endl;
            std::cout << "Software Latency: " << latencies_ms.getAvg() << " ms"
                      << std::endl;
            std::cout << "Time capture and resize: " << bc[0].getAvgSec() << " sec \n";
            std::cout << "Time pre and reposition: " << bc[1].getAvgSec() << " sec \n";
            std::cout << "Time infer face: " << bc[3].getAvgSec() << " sec \n";
            std::cout << "Time infer pose: " << bc[4].getAvgSec() << " sec \n";
            std::cout << "Time infer od: " << bc[5].getAvgSec() << " sec \n";
            std::cout << "Time post face: " << bc[6].getAvgSec() << " sec \n";
            std::cout << "Time post pose: " << bc[7].getAvgSec() << " sec \n";
            std::cout << "Time post od: " << bc[8].getAvgSec() << " sec \n";
            std::cout << "Time image: " << bc[9].getAvgSec() << " sec \n";
            std::cout << "Time display: " << bc[10].getAvgSec() << " sec \n";
            std::cout << "\n";

            clk_prev = clk;
        }
    }

    for (auto& thread : threads) {
        thread.join();
    }
    for (auto& queue : queues) {
        queue.close();
    }
    for (auto& buffer : buffers) {
        buffer.close();
    }

    return 0;
}
