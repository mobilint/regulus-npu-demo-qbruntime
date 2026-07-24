#include <qbruntime/qbruntime.h>

#include <cmath>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/config/types.h"
#include "src/model/model.h"
#include "src/postprocess/post_builder.h"
#include "src/preprocess/preprocess.h"

using namespace mobilint::post;

std::string get_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr,
                "User must provide two arguments: 1) mxq model path 2) image path\n");
        exit(1);
    }
    std::string mxq_path = argv[1];
    std::string img_path = argv[2];
    std::string save_path =
        img_path.substr(0, img_path.find_last_of('.')) + "_result.jpg";

    // ImageNet classification preprocessing/postprocessing (torchvision style).
    // Equivalent to the former config.yaml:
    //   Pre-order: [Resize, CenterCrop, Normalize]
    //   Resize     size: 256, interpolation: bilinear
    //   CenterCrop size: [224, 224]
    //   Normalize  style: torch
    //   Post       task: image_classification, nc: 1000
    ModelInfo cfg;
    {
        PreProcessInfo resize;
        resize.op = PreProcessOps::RESIZE;
        resize.style = "bilinear";
        resize.img_size = 256;
        cfg.m_preprocess_list.push_back(resize);

        PreProcessInfo crop;
        crop.op = PreProcessOps::CENTERCROP;
        crop.style.clear();
        crop.img_size = std::make_pair(224, 224);
        cfg.m_preprocess_list.push_back(crop);

        PreProcessInfo norm;
        norm.op = PreProcessOps::NORMALIZE;
        norm.style = "torch";
        norm.img_size = std::monostate{};
        cfg.m_preprocess_list.push_back(norm);
    }
    cfg.m_postprocess.task = Task::CLS;
    cfg.m_postprocess.num_classes = 1000;

    NPUModel model(mxq_path);
    auto input_shape = model.get_input_shape();
    int imh = input_shape[0];
    int imw = input_shape[1];

    auto post_processor = make_postprocessor(imh, imw, cfg);

    PreProcessor pre_processor;
    cv::Mat org_img = cv::imread(img_path);
    auto preprocessed = pre_processor(org_img, cfg);

    auto output = model(std::move(preprocessed));

    std::vector<std::array<float, 4>> boxes;
    std::vector<float> scores;
    std::vector<int> labels;
    std::vector<std::vector<float>> extras;

    uint64_t ticket = post_processor->enqueue(output, boxes, scores, labels, extras);
    post_processor->receive(ticket);

    post_processor->plot_results(org_img, boxes, scores, labels, extras);
    cv::imwrite(save_path, org_img);

    return 0;
}
