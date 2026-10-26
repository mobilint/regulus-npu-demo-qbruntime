// CPU resize/crop -> pinned RGB uint8 NPU inference -> CPU top-5 and drawing.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace demo {
constexpr int kSize = 224;
constexpr int kClasses = 1000;
struct Prediction {
  int label;
  double probability;
};

std::array<Prediction, 5> top_five(const float* logits, size_t count) {
  if (!logits || count != kClasses) throw std::runtime_error("Expected 1000 classification logits");
  for (size_t i = 0; i < count; ++i)
    if (!std::isfinite(logits[i])) throw std::runtime_error("Non-finite classification logit");
  std::array<int, kClasses> indices{};
  std::iota(indices.begin(), indices.end(), 0);
  std::partial_sort(indices.begin(), indices.begin() + 5, indices.end(),
                    [&](int a, int b) { return logits[a] != logits[b] ? logits[a] > logits[b] : a < b; });
  const double maximum = logits[indices[0]];
  double total = 0;
  for (size_t i = 0; i < count; ++i) total += std::exp(double(logits[i]) - maximum);
  std::array<Prediction, 5> result{};
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = {indices[i], std::exp(double(logits[indices[i]]) - maximum) / total};
  return result;
}
}  // namespace demo

#ifndef IMAGE_CLASSIFICATION_UNIT_TEST
#include <qbruntime/qbruntime.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

namespace demo {
using Clock = std::chrono::steady_clock;

void check(mobilint::StatusCode status, const char* operation) {
  if (!status)
    throw std::runtime_error(std::string(operation) + ": status=" + std::to_string(static_cast<int>(status)));
}

struct LaunchedModel {
  mobilint::Model& model;
  ~LaunchedModel() {
    const auto status = model.dispose();
    if (!status) std::cerr << "Model::dispose: status=" << static_cast<int>(status) << '\n';
  }
};

std::vector<std::string> read_labels(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("Cannot read labels: " + path.string());
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) throw std::runtime_error("Empty ImageNet label");
    labels.push_back(line);
  }
  if (file.bad() || labels.size() != kClasses) throw std::runtime_error("Expected 1000 ImageNet labels");
  return labels;
}

void run(const std::string& model_path, const std::string& image_path) {
  if (!std::filesystem::is_regular_file(model_path)) throw std::runtime_error("Model not found: " + model_path);
  const auto labels = read_labels(std::filesystem::path(model_path).parent_path() / "imagenet.txt");
  cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
  if (image.empty()) throw std::runtime_error("Cannot read image: " + image_path);

  // Matches this MXQ's calibration: 256x256 resize, then 224x224 center crop.
  // The DSP letterbox API is not equivalent to this classification transform.
  const auto pre_start = Clock::now();
  cv::Mat resized, input;
  cv::resize(image, resized, {256, 256}, 0, 0, cv::INTER_LINEAR);
  cv::cvtColor(resized(cv::Rect(16, 16, kSize, kSize)), input, cv::COLOR_BGR2RGB);
  if (!input.isContinuous()) input = input.clone();
  // ImageNet mean/std and /255 are already fused into the MXQ. Keep raw bytes.
  const auto pre_end = Clock::now();

  mobilint::StatusCode status;
  auto model = mobilint::Model::create(model_path, status);
  check(status, "Model::create");
  if (!model) throw std::runtime_error("Model::create returned null");
  if (model->getModelInputDataType() != mobilint::DataType::Uint8 ||
      model->getModelOutputDataType() != mobilint::DataType::Float32 ||
      model->getModelInputShape() != std::vector<std::vector<int64_t>>{{kSize, kSize, 3}} ||
      model->getModelOutputShape() != std::vector<std::vector<int64_t>>{{1, 1, kClasses}})
    throw std::runtime_error("Expected ResNet-18: RGB uint8 224x224x3 input, 1000 float32 logits");
  auto accelerator = mobilint::Accelerator::create(status);
  check(status, "Accelerator::create");
  if (!accelerator) throw std::runtime_error("Accelerator::create returned null");
  auto input_memory = accelerator->allocatePinnedMemory<uint8_t>({1, kSize, kSize, 3}, status);
  check(status, "allocatePinnedMemory(uint8)");
  if (!input_memory.data() || input_memory.size() != input.total() * input.elemSize())
    throw std::runtime_error("Invalid pinned input");
  std::memcpy(input_memory.data(), input.data, input_memory.size());
  std::vector<mobilint::PinnedMemory<uint8_t>> inputs{input_memory};
  auto output_memory = accelerator->allocatePinnedMemory<float>({1, 1, kClasses}, status);
  check(status, "allocatePinnedMemory(float)");
  if (!output_memory.data() || output_memory.size() != kClasses * sizeof(float))
    throw std::runtime_error("Invalid pinned output");
  std::vector<mobilint::PinnedMemory<float>> outputs{output_memory};
  check(model->launch(*accelerator), "Model::launch");
  LaunchedModel launched{*model};

  std::cout << "Input: RGB uint8 NHWC 224x224x3; inferPinnedMemory\n"
            << "CPU: resize/center-crop and top-5 (no classification DSP API)" << std::endl;
  const auto npu_start = Clock::now();
  check(model->inferPinnedMemory(inputs, outputs, 0), "Model::inferPinnedMemory");
  const auto npu_end = Clock::now();
  if (outputs.size() != 1 || outputs[0].size() != kClasses * sizeof(float))
    throw std::runtime_error("Unexpected classification output size");
  const auto predictions = top_five(outputs[0].data(), kClasses);
  const auto post_end = Clock::now();
  std::cout << std::fixed << std::setprecision(4);
  for (size_t i = 0; i < predictions.size(); ++i) {
    const auto& prediction = predictions[i];
    const std::string text =
        std::to_string(i + 1) + ". " + labels[prediction.label] + cv::format(" %.2f%%", prediction.probability * 100);
    std::cout << text << " (class=" << prediction.label << ")\n";
    cv::putText(image, text, {10, 25 + int(i) * 25}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {0, 255, 0}, 1);
  }
  std::filesystem::path destination(image_path);
  destination.replace_filename(destination.stem().string() + "_result.jpg");
  if (!cv::imwrite(destination.string(), image))
    throw std::runtime_error("Cannot save result: " + destination.string());
  const auto ms = [](auto start, auto end) { return std::chrono::duration<double, std::milli>(end - start).count(); };
  std::cout << "CPU preprocess: " << ms(pre_start, pre_end) << " ms\n"
            << "NPU inference: " << ms(npu_start, npu_end) << " ms\n"
            << "CPU top-5: " << ms(npu_end, post_end) << " ms\n"
            << "Saved: " << destination << '\n';
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
#endif  // IMAGE_CLASSIFICATION_UNIT_TEST
