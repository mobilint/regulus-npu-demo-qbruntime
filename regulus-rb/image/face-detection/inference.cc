// DSP letterbox -> pinned uint8 NPU inference -> DSP decode/NMS -> CPU drawing.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace demo {
constexpr int kWidth = 640;
constexpr int kHeight = 640;
constexpr int kClasses = 1;
constexpr int kMaxDetections = 100;

struct Letterbox {
    int left, top;
    double scale;
};

Letterbox dsp_geometry(float scale, int left, int top) {
    if (!std::isfinite(scale) || scale <= 0 || left < 0 || left >= kWidth ||
        top < 0 || top >= kHeight)
        throw std::runtime_error("Invalid DSP letterbox geometry");
    return {left, top, scale};
}

uint32_t image_bytes(int width, int height) {
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535 ||
        uint64_t(width) * uint64_t(height) * 3 > UINT32_MAX)
        throw std::runtime_error("Image exceeds DSP letterbox size limits");
    return uint32_t(width) * uint32_t(height) * 3;
}

std::array<float, 4> restore_box(const std::array<float, 4>& box,
                                 const Letterbox& geometry, int width, int height) {
    std::array<float, 4> restored{};
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(box[i])) throw std::runtime_error("DSP returned a non-finite box");
        const bool x = i % 2 == 0;
        restored[i] = float(std::clamp(
            (box[i] - (x ? geometry.left : geometry.top)) /
                geometry.scale,
            0.0, double(x ? width : height)));
    }
    return restored;
}

// Output order is not fixed. Identify HWC planes by grid size and channels.
struct HeadPlanes {
    std::array<int, 3> box{{-1, -1, -1}};
    std::array<int, 3> cls{{-1, -1, -1}};
};

HeadPlanes identify_heads(const std::vector<std::vector<int64_t>>& shapes) {
    if (shapes.size() != 6) throw std::runtime_error("Expected six YOLO11s-face raw output tensors");
    HeadPlanes planes;
    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes[i];
        const size_t offset = shape.size() == 4 && shape[0] == 1 ? 1 : 0;
        if (shape.size() - offset != 3) throw std::runtime_error("Expected HWC output tensors");
        int level = -1;
        for (int j = 0; j < 3; ++j)
            if (shape[offset] == (80 >> j) && shape[offset + 1] == (80 >> j)) level = j;
        if (level < 0) throw std::runtime_error("Unexpected YOLO output grid");
        const int64_t channels = shape[offset + 2];
        if (channels != 64 && channels != kClasses)
            throw std::runtime_error("Expected DFL box=64 or class=1 channels");
        int& index = channels == 64 ? planes.box[level] : planes.cls[level];
        if (index >= 0) throw std::runtime_error("Duplicate YOLO output plane");
        index = int(i);
    }
    for (int i = 0; i < 3; ++i)
        if (planes.box[i] < 0 || planes.cls[i] < 0)
            throw std::runtime_error("Missing YOLO output plane");
    return planes;
}
}  // namespace demo

// External tests can compile the pure helpers above without board SDKs.
#ifndef FACE_DETECTION_UNIT_TEST
#include <qbruntime/qbruntime.h>
#include <sp500/dsp.h>
#include <sp500/letterbox.h>
#include <sp500/yolo.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace demo {
using Clock = std::chrono::steady_clock;
constexpr const char* kNames[kClasses] = {"face"};
static_assert(SP500_ALIGN_NPU == 4096, "NPU shared memory must use 4096-byte alignment");

void check_npu(mobilint::StatusCode status, const char* operation) {
    if (!status) throw std::runtime_error(std::string(operation) + ": " +
                                        "status=" + std::to_string(static_cast<int>(status)));
}

void check_dsp(sp500_status status, const char* operation) {
    if (status == SP500_OK) return;
    if (status == SP500_ERR_TIMEOUT)
        std::cerr << "DSP timeout: stop here. Reload DSP firmware before another run.\n";
    throw std::runtime_error(std::string(operation) + ": " + sp500_status_string(status));
}

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Keeps the accelerator alive until the launched model has been disposed.
struct LaunchedModel {
    mobilint::Model& model;
    ~LaunchedModel() {
        const auto status = model.dispose();
        if (!status) std::cerr << "Model::dispose: status=" << static_cast<int>(status) << '\n';
    }
};

// Own the external DSP memory until NPU handles and model relocation state end.
struct SharedMemory {
    std::vector<void*> allocations;
    bool timed_out = false;
    void* allocate(uint32_t bytes, uint32_t alignment) {
        void* data = sp500_shmem_alloc(bytes, alignment);
        if (!data) throw std::runtime_error("Cannot allocate SP500 shared memory");
        try {
            allocations.push_back(data);
        } catch (...) {
            sp500_shmem_free(data);
            throw;
        }
        if (sp500_shmem_contains(data) != 1 || sp500_shmem_phys_addr(data) == 0 ||
            sp500_shmem_phys_addr(data) % alignment != 0)
            throw std::runtime_error("Invalid SP500 shared memory mapping/alignment");
        return data;
    }
    ~SharedMemory() {
        // A timeout does not cancel DSP work. Do not recycle its buffers.
        if (!timed_out)
            for (void* data : allocations) sp500_shmem_free(data);
    }
};

void run(const std::string& model_path, const std::string& image_path) {
    if (!std::filesystem::is_regular_file(model_path))
        throw std::runtime_error("Model file not found: " + model_path);
    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (image.empty()) throw std::runtime_error("Cannot read image: " + image_path);
    const uint32_t source_bytes = image_bytes(image.cols, image.rows);
    if (image.type() != CV_8UC3) throw std::runtime_error("Expected BGR uint8 image");
    if (!image.isContinuous()) image = image.clone();

    mobilint::StatusCode status;
    auto model = mobilint::Model::create(model_path, status);
    check_npu(status, "Model::create");
    if (!model) throw std::runtime_error("Model::create returned null");
    const auto& inputs = model->getModelInputShape();
    if (model->getModelInputDataType() != mobilint::DataType::Uint8 ||
        model->getModelOutputDataType() != mobilint::DataType::Float32 ||
        inputs != std::vector<std::vector<int64_t>>{{kHeight, kWidth, 3}})
        throw std::runtime_error("Expected YOLO11s-face: uint8 HWC 640x640x3 input, float32 output");
    const auto shapes = model->getModelOutputShape();
    const auto planes = identify_heads(shapes);

    // Attach only. Firmware is loaded separately, not reset by this demo.
    std::cout << "Input: RGB uint8 NHWC 640x640x3; SP500 " << sp500_version_string()
              << "\nDSP: attach (load firmware first if not running)" << std::endl;
    check_dsp(sp500_dsp_open(0), "sp500_dsp_open (load firmware with dsp_firmware_load)");
    auto accelerator = mobilint::Accelerator::create(status);
    check_npu(status, "Accelerator::create");
    if (!accelerator) throw std::runtime_error("Accelerator::create returned null");
    SharedMemory shared;
    std::vector<mobilint::PinnedMemory<float>> tensors;
    tensors.reserve(shapes.size());
    for (const auto& shape : shapes) {
        // Shapes have already been checked against the six fixed raw heads.
        uint32_t bytes = sizeof(float);
        for (int64_t dim : shape) bytes *= uint32_t(dim);
        void* data = shared.allocate(bytes, SP500_ALIGN_NPU);
        tensors.emplace_back(shape, sp500_shmem_phys_addr(data), data);
    }
    // DSP writes the RGB uint8 model input directly into NPU-accessible memory.
    constexpr uint32_t input_bytes = kHeight * kWidth * 3;
    void* input_data = shared.allocate(input_bytes, SP500_ALIGN_NPU);
    mobilint::PinnedMemory<uint8_t> input_memory(
        {1, kHeight, kWidth, 3}, sp500_shmem_phys_addr(input_data), input_data);
    std::vector<mobilint::PinnedMemory<uint8_t>> pinned_inputs{input_memory};

    // The only host copy is the decoded BGR image into DSP source memory.
    void* source_data = shared.allocate(source_bytes, SP500_ALIGN_DSP);
    std::memcpy(source_data, image.data, source_bytes);
    sp500_buffer source{source_data, source_bytes, SP500_BUFFER_SHARED};
    sp500_buffer model_input{input_data, input_bytes, SP500_BUFFER_SHARED};
    sp500_letterbox_params letterbox_params;
    sp500_letterbox_params_init(&letterbox_params);
    letterbox_params.src_w = image.cols;
    letterbox_params.src_h = image.rows;
    letterbox_params.dst_w = kWidth;
    letterbox_params.dst_h = kHeight;
    letterbox_params.channels = 3;
    letterbox_params.pad_value = 114;
    letterbox_params.interp = SP500_RESIZE_BILINEAR;
    letterbox_params.center = 1;
    letterbox_params.scale_up = 1;
    letterbox_params.swap_rb = 1;
    sp500_letterbox_result letterbox_result{};
    const auto prepare_start = Clock::now();
    const auto prepare_status = sp500_letterbox(&source, &letterbox_params, &model_input, &letterbox_result);
    shared.timed_out = prepare_status == SP500_ERR_TIMEOUT;
    check_dsp(prepare_status, "sp500_letterbox");
    const auto prepare_end = Clock::now();
    // Use DSP-reported scale and padding, not a separate CPU geometry estimate.
    const auto geometry = dsp_geometry(letterbox_result.scale, letterbox_result.pad_x, letterbox_result.pad_y);
    check_npu(model->launch(*accelerator), "Model::launch");
    LaunchedModel launched{*model};

    // NPU reads pinned uint8 input and writes float heads into DSP shared memory.
    const auto npu_start = Clock::now();
    check_npu(model->inferPinnedMemory(pinned_inputs, tensors, 0), "Model::inferPinnedMemory");
    const auto npu_end = Clock::now();
    if (tensors.size() != shapes.size()) throw std::runtime_error("Output tensor count mismatch");
    for (size_t i = 0; i < tensors.size(); ++i) {
        uint64_t expected = 1;
        for (int64_t dim : shapes[i]) expected *= uint64_t(dim);
        if (!tensors[i].data() || tensors[i].size() != expected * sizeof(float))
            throw std::runtime_error("Output tensor size mismatch");
    }

    // DSP decode, confidence filtering and per-class NMS. No CPU fallback.
    // Both devices use the same physical output buffers. No output staging copy.
    sp500_yolo_raw raw;
    sp500_yolo_raw_init(&raw);
    for (int level = 0; level < 3; ++level) {
        auto& box = tensors[planes.box[level]];
        auto& cls = tensors[planes.cls[level]];
        raw.box[level] = {box.data(), uint32_t(box.size()), SP500_BUFFER_SHARED};
        raw.cls[level] = {cls.data(), uint32_t(cls.size()), SP500_BUFFER_SHARED};
    }
    sp500_yolo_params params;
    sp500_yolo_params_init(&params);
    params.img_w = kWidth;
    params.img_h = kHeight;
    params.num_classes = kClasses;
    params.max_detections = kMaxDetections;
    params.conf_threshold = 0.3f;
    params.iou_threshold = 0.6f;
    const uint32_t record_bytes = sp500_yolo_records_bytes(SP500_YOLO_TASK_DETECT, kMaxDetections);
    auto* records = static_cast<sp500_yolo_detection*>(shared.allocate(record_bytes, SP500_ALIGN_DSP));
    sp500_yolo_output output;
    sp500_yolo_output_init(&output);
    output.records = {records, record_bytes, SP500_BUFFER_SHARED};
    sp500_yolo_result result{};
    const auto dsp_start = Clock::now();
    const auto dsp_status = sp500_yolo_decode(SP500_YOLO_HEAD_ANCHORLESS, SP500_YOLO_TASK_DETECT,
                                             &raw, &params, &output, &result);
    shared.timed_out = dsp_status == SP500_ERR_TIMEOUT;
    check_dsp(dsp_status, "sp500_yolo_decode");
    const auto dsp_end = Clock::now();
    if (result.count > kMaxDetections) throw std::runtime_error("DSP detection count exceeds capacity");

    // CPU restores source coordinates and draws only the DSP's final records.
    std::cout << std::fixed << std::setprecision(3);
    for (uint32_t i = 0; i < result.count; ++i) {
        const auto& record = records[i];
        if (!std::isfinite(record.class_id) || record.class_id < 0 || record.class_id >= kClasses ||
            record.class_id != std::floor(record.class_id) || !std::isfinite(record.score) ||
            record.score < 0 || record.score > 1)
            throw std::runtime_error("DSP returned invalid class or score");
        const int label = int(record.class_id);
        const auto box = restore_box({record.x1, record.y1, record.x2, record.y2},
                                     geometry, image.cols, image.rows);
        if (box[2] <= box[0] || box[3] <= box[1]) continue;
        cv::rectangle(image, cv::Rect(cv::Point(int(box[0]), int(box[1])),
                                      cv::Point(int(box[2]), int(box[3]))), {0, 255, 0}, 2);
        const std::string text = std::string(kNames[label]) + " " + cv::format("%.2f", record.score);
        cv::putText(image, text, {int(box[0]), std::max(15, int(box[1]) - 5)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, {0, 255, 0}, 1);
        std::cout << kNames[label] << " score=" << record.score << " box="
                  << box[0] << ',' << box[1] << ',' << box[2] << ',' << box[3] << '\n';
    }
    std::filesystem::path destination(image_path);
    destination.replace_filename(destination.stem().string() + "_result.jpg");
    if (!cv::imwrite(destination.string(), image))
        throw std::runtime_error("Cannot save result: " + destination.string());
    std::cout << "DSP letterbox: " << milliseconds(prepare_start, prepare_end) << " ms\n"
              << "NPU inference: " << milliseconds(npu_start, npu_end) << " ms\n"
              << "DSP decode/NMS: " << milliseconds(dsp_start, dsp_end) << " ms\n"
              << "Detections: " << result.count << "\nSaved: " << destination << '\n';
}
}  // namespace demo

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <model.mxq> <image-path>\n";
        return 1;
    }
    try {
        demo::run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
#endif  // FACE_DETECTION_UNIT_TEST
