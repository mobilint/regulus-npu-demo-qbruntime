// DSP letterbox -> pinned uint8 NPU inference -> DSP decode/NMS/masks -> CPU drawing.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace demo {
constexpr int kWidth = 640;
constexpr int kHeight = 640;
constexpr int kClasses = 80;
constexpr int kMaskCoefficients = 32;
constexpr int kPrototypeWidth = 160;
constexpr int kPrototypeHeight = 160;
constexpr int kMaxDetections = 32;
constexpr uint32_t kMaskCapacity =
    uint32_t(kMaxDetections) * uint32_t(kWidth) * uint32_t(kHeight);

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
            (box[i] - (x ? geometry.left : geometry.top)) / geometry.scale,
            0.0, double(x ? width : height)));
    }
    return restored;
}

struct HeadPlanes {
    std::array<int, 3> box{{-1, -1, -1}};
    std::array<int, 3> cls{{-1, -1, -1}};
    std::array<int, 3> coeff{{-1, -1, -1}};
    int proto = -1;
};

// Output order is interleaved and is not an API contract. Identify each HWC plane.
HeadPlanes identify_heads(const std::vector<std::vector<int64_t>>& shapes) {
    if (shapes.size() != 10)
        throw std::runtime_error("Expected ten YOLO11s-seg raw output tensors");
    HeadPlanes planes;
    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes[i];
        const size_t offset = shape.size() == 4 && shape[0] == 1 ? 1 : 0;
        if (shape.size() - offset != 3) throw std::runtime_error("Expected HWC output tensors");
        const int64_t height = shape[offset];
        const int64_t width = shape[offset + 1];
        const int64_t channels = shape[offset + 2];
        if (height == kPrototypeHeight && width == kPrototypeWidth &&
            channels == kMaskCoefficients) {
            if (planes.proto >= 0) throw std::runtime_error("Duplicate prototype tensor");
            planes.proto = int(i);
            continue;
        }
        int level = -1;
        for (int j = 0; j < 3; ++j) {
            const int grid = 80 >> j;
            if (height == grid && width == grid) level = j;
        }
        if (level < 0) throw std::runtime_error("Unexpected YOLO output grid");
        int* index = nullptr;
        if (channels == 64) index = &planes.box[level];
        if (channels == kClasses) index = &planes.cls[level];
        if (channels == kMaskCoefficients) index = &planes.coeff[level];
        if (!index) throw std::runtime_error("Unexpected YOLO output channels");
        if (*index >= 0) throw std::runtime_error("Duplicate YOLO output plane");
        *index = int(i);
    }
    if (planes.proto < 0) throw std::runtime_error("Missing prototype tensor");
    for (int i = 0; i < 3; ++i)
        if (planes.box[i] < 0 || planes.cls[i] < 0 || planes.coeff[i] < 0)
            throw std::runtime_error("Missing YOLO output plane");
    return planes;
}

uint32_t tensor_bytes(const std::vector<int64_t>& shape) {
    uint64_t elements = 1;
    for (int64_t dim : shape) {
        if (dim <= 0 || elements > UINT32_MAX / sizeof(float) / uint64_t(dim))
            throw std::runtime_error("Output tensor exceeds shared-memory size limits");
        elements *= uint64_t(dim);
    }
    return uint32_t(elements * sizeof(float));
}

struct MaskSpan {
    int x1, y1, x2, y2;
    uint32_t offset;
    uint32_t bytes;
    bool present;
};

MaskSpan validate_mask_span(const std::array<float, 4>& bounds, uint32_t offset,
                            uint32_t capacity) {
    for (size_t i = 0; i < bounds.size(); ++i) {
        const float value = bounds[i];
        const float limit = i % 2 == 0 ? float(kWidth) : float(kHeight);
        if (!std::isfinite(value) || value != std::floor(value) ||
            value < 0.0f || value > limit)
            throw std::runtime_error("DSP returned invalid mask geometry");
    }
    const int x1 = int(bounds[0]);
    const int y1 = int(bounds[1]);
    const int x2 = int(bounds[2]);
    const int y2 = int(bounds[3]);
    if (x1 % 4 != 0 || y1 % 4 != 0 || x2 % 4 != 0 || y2 % 4 != 0)
        throw std::runtime_error("DSP returned out-of-range mask geometry");
    // MASK_NONE is valid for an empty snapped box or a full mask buffer.
    if (offset == UINT32_MAX) return {x1, y1, x2, y2, offset, 0, false};
    if (x2 <= x1 || y2 <= y1)
        throw std::runtime_error("DSP returned empty geometry for a stored mask");
    const uint64_t bytes = uint64_t(x2 - x1) * uint64_t(y2 - y1);
    if (offset > capacity || bytes > uint64_t(capacity - offset))
        throw std::runtime_error("DSP mask offset or length exceeds capacity");
    return {x1, y1, x2, y2, offset, uint32_t(bytes), true};
}
}  // namespace demo

#ifndef INSTANCE_SEGMENTATION_UNIT_TEST
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
constexpr const char* kNames[kClasses] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork",
    "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli",
    "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant",
    "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard",
    "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book",
    "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"};

const std::array<cv::Scalar, 8> kColors{{
    {255, 56, 56}, {255, 157, 151}, {255, 112, 31}, {255, 178, 29},
    {207, 210, 49}, {72, 249, 10}, {146, 204, 23}, {61, 219, 134}}};

void check_npu(mobilint::StatusCode status, const char* operation) {
    if (!status) throw std::runtime_error(std::string(operation) + ": status=" +
                                         std::to_string(static_cast<int>(status)));
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

struct LaunchedModel {
    mobilint::Model& model;
    ~LaunchedModel() {
        const auto status = model.dispose();
        if (!status) std::cerr << "Model::dispose: status=" << static_cast<int>(status) << '\n';
    }
};

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
        // A timeout does not cancel DSP work. Do not recycle buffers still in use.
        if (!timed_out)
            for (void* data : allocations) sp500_shmem_free(data);
    }
};

void overlay_mask(cv::Mat& image, const uint8_t* mask_data, uint32_t mask_capacity,
                  const sp500_yolo_segmentation& record, const Letterbox& geometry,
                  const cv::Scalar& color) {
    const std::array<float, 4> bounds{record.bx1, record.by1, record.bx2, record.by2};
    const uint32_t mask_offset = sp500_yolo_mask_offset(&record);
    static_assert(SP500_YOLO_MASK_NONE == UINT32_MAX, "Unexpected no-mask sentinel");
    const MaskSpan span = validate_mask_span(bounds, mask_offset, mask_capacity);
    if (!span.present) return;
    cv::Mat network_mask(span.y2 - span.y1, span.x2 - span.x1, CV_8UC1,
                         const_cast<uint8_t*>(mask_data + span.offset));
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(network_mask, &minimum, &maximum);
    if (minimum < 0.0 || maximum > 1.0)
        throw std::runtime_error("DSP returned a non-binary mask");

    // Remove letterbox padding before the nearest-neighbor resize to source pixels.
    const double content_right = geometry.left + image.cols * geometry.scale;
    const double content_bottom = geometry.top + image.rows * geometry.scale;
    const int nx1 = std::max(span.x1, geometry.left);
    const int ny1 = std::max(span.y1, geometry.top);
    const int nx2 = std::min(span.x2, int(std::ceil(content_right)));
    const int ny2 = std::min(span.y2, int(std::ceil(content_bottom)));
    if (nx2 <= nx1 || ny2 <= ny1) return;

    const int sx1 = std::clamp(int(std::floor((nx1 - geometry.left) / geometry.scale)),
                               0, image.cols);
    const int sy1 = std::clamp(int(std::floor((ny1 - geometry.top) / geometry.scale)),
                               0, image.rows);
    const int sx2 = std::clamp(int(std::ceil((nx2 - geometry.left) / geometry.scale)),
                               0, image.cols);
    const int sy2 = std::clamp(int(std::ceil((ny2 - geometry.top) / geometry.scale)),
                               0, image.rows);
    if (sx2 <= sx1 || sy2 <= sy1) return;

    const cv::Mat cropped =
        network_mask(cv::Rect(nx1 - span.x1, ny1 - span.y1, nx2 - nx1, ny2 - ny1));
    cv::Mat source_mask;
    cv::resize(cropped, source_mask, cv::Size(sx2 - sx1, sy2 - sy1),
               0.0, 0.0, cv::INTER_NEAREST);
    source_mask *= 255;
    cv::Mat image_roi = image(cv::Rect(sx1, sy1, sx2 - sx1, sy2 - sy1));
    cv::Mat colored(image_roi.size(), image_roi.type(), color);
    cv::Mat blended;
    cv::addWeighted(image_roi, 0.55, colored, 0.45, 0.0, blended);
    blended.copyTo(image_roi, source_mask);
}

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
        throw std::runtime_error(
            "Expected YOLO11s-seg: uint8 HWC 640x640x3 input, float32 output");
    const auto shapes = model->getModelOutputShape();
    const auto planes = identify_heads(shapes);

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
        void* data = shared.allocate(tensor_bytes(shape), SP500_ALIGN_NPU);
        tensors.emplace_back(shape, sp500_shmem_phys_addr(data), data);
    }

    constexpr uint32_t input_bytes = kHeight * kWidth * 3;
    void* input_data = shared.allocate(input_bytes, SP500_ALIGN_NPU);
    mobilint::PinnedMemory<uint8_t> input_memory(
        {1, kHeight, kWidth, 3}, sp500_shmem_phys_addr(input_data), input_data);
    std::vector<mobilint::PinnedMemory<uint8_t>> pinned_inputs{input_memory};

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
    const auto prepare_status =
        sp500_letterbox(&source, &letterbox_params, &model_input, &letterbox_result);
    shared.timed_out = prepare_status == SP500_ERR_TIMEOUT;
    check_dsp(prepare_status, "sp500_letterbox");
    const auto prepare_end = Clock::now();
    const auto geometry =
        dsp_geometry(letterbox_result.scale, letterbox_result.pad_x, letterbox_result.pad_y);
    check_npu(model->launch(*accelerator), "Model::launch");
    LaunchedModel launched{*model};

    const auto npu_start = Clock::now();
    check_npu(model->inferPinnedMemory(pinned_inputs, tensors, 0), "Model::inferPinnedMemory");
    const auto npu_end = Clock::now();
    if (tensors.size() != shapes.size()) throw std::runtime_error("Output tensor count mismatch");
    for (size_t i = 0; i < tensors.size(); ++i) {
        if (!tensors[i].data() || tensors[i].size() != tensor_bytes(shapes[i]))
            throw std::runtime_error("Output tensor size mismatch");
    }

    sp500_yolo_raw raw;
    sp500_yolo_raw_init(&raw);
    for (int level = 0; level < 3; ++level) {
        auto& box = tensors[planes.box[level]];
        auto& cls = tensors[planes.cls[level]];
        auto& coeff = tensors[planes.coeff[level]];
        raw.box[level] = {box.data(), uint32_t(box.size()), SP500_BUFFER_SHARED};
        raw.cls[level] = {cls.data(), uint32_t(cls.size()), SP500_BUFFER_SHARED};
        raw.extra[level] = {coeff.data(), uint32_t(coeff.size()), SP500_BUFFER_SHARED};
    }
    auto& proto = tensors[planes.proto];
    sp500_yolo_params params;
    sp500_yolo_params_init(&params);
    params.img_w = kWidth;
    params.img_h = kHeight;
    params.num_classes = kClasses;
    params.max_detections = kMaxDetections;
    params.conf_threshold = 0.3f;
    params.iou_threshold = 0.6f;
    params.proto = {proto.data(), uint32_t(proto.size()), SP500_BUFFER_SHARED};
    params.mask_coeffs = kMaskCoefficients;
    params.proto_h = kPrototypeHeight;
    params.proto_w = kPrototypeWidth;
    params.mask_threshold = 0.0f;

    const uint32_t record_bytes =
        sp500_yolo_records_bytes(SP500_YOLO_TASK_SEGMENT, kMaxDetections);
    auto* records = static_cast<sp500_yolo_segmentation*>(
        shared.allocate(record_bytes, SP500_ALIGN_DSP));
    auto* masks = static_cast<uint8_t*>(shared.allocate(kMaskCapacity, SP500_ALIGN_DSP));
    sp500_yolo_output output;
    sp500_yolo_output_init(&output);
    output.records = {records, record_bytes, SP500_BUFFER_SHARED};
    output.mask = {masks, kMaskCapacity, SP500_BUFFER_SHARED};
    sp500_yolo_result result{};
    const auto dsp_start = Clock::now();
    const auto dsp_status = sp500_yolo_decode(
        SP500_YOLO_HEAD_ANCHORLESS, SP500_YOLO_TASK_SEGMENT,
        &raw, &params, &output, &result);
    shared.timed_out = dsp_status == SP500_ERR_TIMEOUT;
    check_dsp(dsp_status, "sp500_yolo_decode");
    const auto dsp_end = Clock::now();
    if (result.count > kMaxDetections || result.record_floats != SP500_YOLO_SEGMENT_RECORD_FLOATS)
        throw std::runtime_error("DSP segmentation result exceeds the output contract");

    std::cout << std::fixed << std::setprecision(3);
    for (uint32_t i = 0; i < result.count; ++i) {
        const auto& record = records[i];
        if (!std::isfinite(record.det.class_id) || record.det.class_id < 0 ||
            record.det.class_id >= kClasses ||
            record.det.class_id != std::floor(record.det.class_id) ||
            !std::isfinite(record.det.score) || record.det.score < 0 || record.det.score > 1)
            throw std::runtime_error("DSP returned invalid class or score");
        const int label = int(record.det.class_id);
        overlay_mask(image, masks, kMaskCapacity, record, geometry,
                     kColors[size_t(label) % kColors.size()]);
        const auto box = restore_box(
            {record.det.x1, record.det.y1, record.det.x2, record.det.y2},
            geometry, image.cols, image.rows);
        if (box[2] <= box[0] || box[3] <= box[1]) continue;
        const cv::Scalar color = kColors[size_t(label) % kColors.size()];
        cv::rectangle(image, cv::Rect(cv::Point(int(box[0]), int(box[1])),
                                      cv::Point(int(box[2]), int(box[3]))), color, 2);
        const std::string text = std::string(kNames[label]) + " " +
                                 cv::format("%.2f", record.det.score);
        cv::putText(image, text, {int(box[0]), std::max(15, int(box[1]) - 5)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
        std::cout << kNames[label] << " score=" << record.det.score << " box="
                  << box[0] << ',' << box[1] << ',' << box[2] << ',' << box[3] << '\n';
    }
    std::filesystem::path destination(image_path);
    destination.replace_filename(destination.stem().string() + "_result.jpg");
    if (!cv::imwrite(destination.string(), image))
        throw std::runtime_error("Cannot save result: " + destination.string());
    std::cout << "DSP letterbox: " << milliseconds(prepare_start, prepare_end) << " ms\n"
              << "NPU inference: " << milliseconds(npu_start, npu_end) << " ms\n"
              << "DSP decode/NMS/masks: " << milliseconds(dsp_start, dsp_end) << " ms\n"
              << "Instances: " << result.count << "\nSaved: " << destination << '\n';
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
#endif  // INSTANCE_SEGMENTATION_UNIT_TEST
