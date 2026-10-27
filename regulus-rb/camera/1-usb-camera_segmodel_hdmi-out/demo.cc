// USB MJPEG -> CODAJ -> MAPI RGB -> CPU pinned letterbox -> NPU -> DSP masks -> HDMI.
#include <fcntl.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <linux/dma-buf.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <qbruntime/qbruntime.h>
#include <sp500/dsp.h>
#include <sp500/yolo.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace demo {
using Clock = std::chrono::steady_clock;
constexpr int kWidth = 640, kHeight = 480;
constexpr int kDisplayWidth = 1280, kDisplayHeight = 720;
constexpr int kFps = 30, kSlots = 3, kResultSlots = 2, kClasses = 80, kCoefficients = 32, kMaxDetections = 64;
// preprocess, inference, post latency, overlay, DSP call, mask cache, result wait, cache queue wait.
constexpr size_t kTimings = 8;
// Each result slot can store every returned mask, including 64 full-image boxes.
constexpr uint32_t kMaskCapacity = uint32_t(kMaxDetections) * kWidth * kHeight;
volatile std::sig_atomic_t stop_signal = 0;
void on_signal(int) { stop_signal = 1; }
double seconds(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

const char* const kNames[] = {
    "person",         "bicycle",    "car",           "motorcycle",    "airplane",     "bus",           "train",
    "truck",          "boat",       "traffic light", "fire hydrant",  "stop sign",    "parking meter", "bench",
    "bird",           "cat",        "dog",           "horse",         "sheep",        "cow",           "elephant",
    "bear",           "zebra",      "giraffe",       "backpack",      "umbrella",     "handbag",       "tie",
    "suitcase",       "frisbee",    "skis",          "snowboard",     "sports ball",  "kite",          "baseball bat",
    "baseball glove", "skateboard", "surfboard",     "tennis racket", "bottle",       "wine glass",    "cup",
    "fork",           "knife",      "spoon",         "bowl",          "banana",       "apple",         "sandwich",
    "orange",         "broccoli",   "carrot",        "hot dog",       "pizza",        "donut",         "cake",
    "chair",          "couch",      "potted plant",  "bed",           "dining table", "toilet",        "tv",
    "laptop",         "mouse",      "remote",        "keyboard",      "cell phone",   "microwave",     "oven",
    "toaster",        "sink",       "refrigerator",  "book",          "clock",        "vase",          "scissors",
    "teddy bear",     "hair drier", "toothbrush"};

struct Options {
  std::string model;
  int camera = 5;
  float confidence = .25f, iou = .45f, mask = .5f;
  double warmup = 3, duration = 0;
};
Options parse_options(int argc, char** argv) {
  Options options;
  options.model = (std::filesystem::canonical("/proc/self/exe").parent_path() / "mxq/yolo11s-seg.mxq").string();
  auto number = [](const std::string& value) {
    size_t end = 0;
    const double result = std::stod(value, &end);
    if (end != value.size() || !std::isfinite(result)) throw std::runtime_error("Invalid number: " + value);
    return result;
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: demo_usb_hdmi [model.mxq] [-d N] [--conf C] [--iou I] [--mask M] [--warmup SEC] [--duration SEC]\n"
             "MAPI RGB -> CPU pinned letterbox -> NPU -> DSP segmentation -> full-mask HDMI\n";
      std::exit(0);
    }
    if (arg == "-d" || arg == "--conf" || arg == "--iou" || arg == "--mask" || arg == "--warmup" ||
        arg == "--duration" || arg == "-m") {
      if (++i == argc) throw std::runtime_error("Missing value for " + arg);
      if (arg == "-m") {
        options.model = argv[i];
        continue;
      }
      const double value = number(argv[i]);
      if (arg == "-d") {
        if (value < 0 || value > 63 || value != std::floor(value)) throw std::runtime_error("Invalid camera index");
        options.camera = int(value);
      } else if (arg == "--warmup" || arg == "--duration") {
        if (value < 0 || value > 86400) throw std::runtime_error("Invalid duration");
        (arg == "--warmup" ? options.warmup : options.duration) = value;
      } else {
        if (value <= 0 || value >= 1) throw std::runtime_error("Threshold must be between 0 and 1");
        const float threshold = float(value);
        if (threshold <= 0 || threshold >= 1) throw std::runtime_error("Threshold must be between 0 and 1");
        (arg == "--conf" ? options.confidence : arg == "--iou" ? options.iou : options.mask) = threshold;
      }
    } else if (!arg.empty() && arg.front() != '-') {
      options.model = arg;
    } else {
      throw std::runtime_error("Unknown option: " + arg);
    }
  }
  if (!std::filesystem::is_regular_file(options.model)) throw std::runtime_error("Model not found: " + options.model);
  return options;
}
void check(mobilint::StatusCode status, const char* operation) {
  if (!status) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(int(status)));
}
void check(sp500_status status, const char* operation) {
  if (status != SP500_OK) throw std::runtime_error(std::string(operation) + ": " + sp500_status_string(status));
}
void pin_thread(int core) {
  cpu_set_t cores;
  CPU_ZERO(&cores);
  CPU_SET(core, &cores);
  if (pthread_setaffinity_np(pthread_self(), sizeof(cores), &cores) != 0)
    std::cerr << "[warning] Could not set thread affinity\n";
}

template <class T>
class Queue {
 public:
  explicit Queue(size_t capacity) : capacity_(capacity) {}
  bool push(T&& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
    if (closed_) return false;
    queue_.push(std::move(value));
    ready_.notify_all();
    return true;
  }
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop();
    ready_.notify_all();
    return value;
  }
  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    ready_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<T> queue_;
  size_t capacity_;
  bool closed_ = false;
};
template <size_t Count>
class SlotGate {
 public:
  bool acquire(int index) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [&] { return closed_ || !busy_[index]; });
    if (closed_) return false;
    busy_[index] = true;
    return true;
  }
  void release(int index) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    busy_[index] = false;
    ready_.notify_all();
  }
  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    ready_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::array<bool, Count> busy_{};
  bool closed_ = false;
};

struct Geometry {
  int x, y, width, height;
};
Geometry preprocess_rgb(const cv::Mat& rgb, cv::Mat& pinned, cv::Mat* canvas = nullptr) {
  if (rgb.empty() || rgb.type() != CV_8UC3 || pinned.size() != cv::Size(kWidth, kHeight) || pinned.type() != CV_8UC3)
    throw std::runtime_error("Invalid RGB/pinned image");
  if (canvas) canvas->create(kHeight, kWidth, CV_8UC3);
  if (rgb.size() == pinned.size()) {
    // Identity letterbox: read each MAPI byte once and write both final consumers directly.
    // No cached completed tensor is subsequently copied into the pinned input.
    const int bytes = kWidth * 3;
    for (int y = 0; y < kHeight; ++y) {
      const auto* source = rgb.ptr<uint8_t>(y);
      auto* input = pinned.ptr<uint8_t>(y);
      auto* visual = canvas ? canvas->ptr<uint8_t>(y) : nullptr;
      int x = 0;
#ifdef __aarch64__
      for (; x + 16 <= bytes; x += 16) {
        const auto pixels = vld1q_u8(source + x);
        vst1q_u8(input + x, pixels);
        if (visual) vst1q_u8(visual + x, pixels);
      }
#endif
      for (; x < bytes; ++x) {
        input[x] = source[x];
        if (visual) visual[x] = source[x];
      }
    }
    return {0, 0, kWidth, kHeight};
  }
  const double scale = std::min(double(kWidth) / rgb.cols, double(kHeight) / rgb.rows);
  const int width = std::clamp(int(std::lround(rgb.cols * scale)), 1, kWidth);
  const int height = std::clamp(int(std::lround(rgb.rows * scale)), 1, kHeight);
  const int x = int(std::round((kWidth - width) / 2.0 - .1));
  const int y = int(std::round((kHeight - height) / 2.0 - .1));
  if (width != kWidth || height != kHeight) pinned.setTo(cv::Scalar(114, 114, 114));
  cv::Mat destination = pinned(cv::Rect(x, y, width, height));
  uint8_t* address = destination.data;
  cv::resize(rgb, destination, destination.size(), 0, 0, cv::INTER_LINEAR);
  if (destination.data != address) throw std::runtime_error("Preprocessing detached from pinned input");
  if (canvas) preprocess_rgb(rgb, *canvas);
  return {x, y, width, height};
}
void blend_mask(cv::Mat& frame, const cv::Mat& mask, const cv::Rect& box, const cv::Vec3b& color, int band_begin,
                int band_end) {
  const int y0 = std::max(box.y, band_begin), y1 = std::min(box.y + box.height, band_end);
#ifdef __aarch64__
  const uint8x16_t colors[] = {vdupq_n_u8(color[0]), vdupq_n_u8(color[1]), vdupq_n_u8(color[2])};
#endif
  for (int y = y0; y < y1; ++y) {
    const uint8_t* bits = mask.ptr<uint8_t>(y - box.y);
    auto* pixels = frame.ptr<cv::Vec3b>(y) + box.x;
    int x = 0;
#ifdef __aarch64__
    for (; x + 16 <= box.width; x += 16) {
      const uint8x16_t active = vcgtq_u8(vld1q_u8(bits + x), vdupq_n_u8(0));
      auto values = vld3q_u8(reinterpret_cast<uint8_t*>(pixels + x));
      for (int c = 0; c < 3; ++c)
        values.val[c] = vbslq_u8(active, vhaddq_u8(vhaddq_u8(values.val[c], colors[c]), colors[c]), values.val[c]);
      vst3q_u8(reinterpret_cast<uint8_t*>(pixels + x), values);
    }
#endif
    for (; x < box.width; ++x)
      if (bits[x])
        for (int c = 0; c < 3; ++c) pixels[x][c] = (((pixels[x][c] + color[c]) >> 1) + color[c]) >> 1;
  }
}
cv::Rect validate_mask_span(float x1, float y1, float x2, float y2, uint32_t offset, uint32_t capacity) {
  for (const float value : {x1, y1, x2, y2})
    if (!std::isfinite(value) || value < 0 || value > std::max(kWidth, kHeight) || value != std::floor(value))
      throw std::runtime_error("Invalid DSP mask coordinate");
  if (x2 < x1 || y2 < y1 || x2 > kWidth || y2 > kHeight) throw std::runtime_error("DSP mask outside image");
  const cv::Rect box(int(x1), int(y1), int(x2 - x1), int(y2 - y1));
  if (box.x % 4 || box.y % 4 || box.width % 4 || box.height % 4) throw std::runtime_error("DSP mask is not aligned");
  if (box.empty()) {
    if (offset != SP500_YOLO_MASK_NONE) throw std::runtime_error("Stored DSP mask has empty geometry");
    return box;
  }
  const uint64_t bytes = uint64_t(box.width) * box.height;
  if (offset == SP500_YOLO_MASK_NONE || offset > capacity || bytes > capacity - offset)
    throw std::runtime_error("DSP mask missing or outside allocated buffer");
  return box;
}

cv::Mat cache_mask(const uint8_t* source, int width, int height) {
  cv::Mat result(height, width, CV_8UC1);
  auto* destination = result.data;
  const size_t bytes = size_t(width) * height;
  size_t i = 0;
  uint8_t maximum = 0;
  // The DSP window is uncached. Use wide loads instead of the generic small-mask memcpy path.
#ifdef __aarch64__
  for (; i < bytes && (uintptr_t(source + i) & 15); ++i) {
    const uint8_t value = source[i];
    destination[i] = value;
    maximum = std::max(maximum, value);
  }
  uint8x16_t vector_max = vdupq_n_u8(0);
  for (; i + 64 <= bytes; i += 64) {
    const auto a = vld1q_u8(source + i);
    const auto b = vld1q_u8(source + i + 16);
    const auto c = vld1q_u8(source + i + 32);
    const auto d = vld1q_u8(source + i + 48);
    vst1q_u8(destination + i, a);
    vst1q_u8(destination + i + 16, b);
    vst1q_u8(destination + i + 32, c);
    vst1q_u8(destination + i + 48, d);
    vector_max = vmaxq_u8(vector_max, vmaxq_u8(vmaxq_u8(a, b), vmaxq_u8(c, d)));
  }
  for (; i + 16 <= bytes; i += 16) {
    const auto pixels = vld1q_u8(source + i);
    vst1q_u8(destination + i, pixels);
    vector_max = vmaxq_u8(vector_max, pixels);
  }
  maximum = std::max(maximum, vmaxvq_u8(vector_max));
#endif
  for (; i < bytes; ++i) {
    const uint8_t value = source[i];
    destination[i] = value;
    maximum = std::max(maximum, value);
  }
  if (maximum > 1) throw std::runtime_error("DSP returned a non-binary mask");
  return result;
}

class CpuRead {
 public:
  explicit CpuRead(GstBuffer* buffer) {
    if (gst_buffer_n_memory(buffer) != 1) throw std::runtime_error("Expected one packed RGB memory block");
    GstMemory* memory = gst_buffer_n_memory(buffer) ? gst_buffer_peek_memory(buffer, 0) : nullptr;
    if (memory && gst_is_dmabuf_memory(memory)) {
      fd_ = gst_dmabuf_memory_get_fd(memory);
      sync(DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
      active_ = true;
    }
  }
  ~CpuRead() {
    if (active_) {
      try {
        finish();
      } catch (...) {
      }
    }
  }
  void finish() {
    if (active_) {
      sync(DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
      active_ = false;
    }
  }

 private:
  void sync(uint64_t flags) {
    dma_buf_sync request{flags};
    int result;
    do {
      result = ioctl(fd_, DMA_BUF_IOCTL_SYNC, &request);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    if (result < 0) throw std::runtime_error("DMA-BUF CPU access synchronization failed");
  }
  int fd_ = -1;
  bool active_ = false;
};
struct Pipeline {
  GstElement* element = nullptr;
  GstElement* endpoint = nullptr;
  GstBus* bus = nullptr;
  Pipeline(const std::string& description, const char* name) {
    GError* error = nullptr;
    element = gst_parse_launch(description.c_str(), &error);
    if (error || !element) {
      const std::string message = error ? error->message : "Pipeline creation failed";
      g_clear_error(&error);
      if (element) gst_object_unref(element);
      element = nullptr;
      throw std::runtime_error(message);
    }
    endpoint = gst_bin_get_by_name(GST_BIN(element), name);
    bus = gst_element_get_bus(element);
    if (!endpoint || !bus) {
      reset();
      throw std::runtime_error("Missing pipeline endpoint/bus");
    }
  }
  Pipeline(const Pipeline&) = delete;
  ~Pipeline() { reset(); }
  void start() {
    if (gst_element_set_state(element, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
      throw std::runtime_error("Pipeline failed to start");
  }
  void reset() {
    if (element) gst_element_set_state(element, GST_STATE_NULL);
    if (bus) gst_object_unref(bus);
    if (endpoint) gst_object_unref(endpoint);
    if (element) gst_object_unref(element);
    element = endpoint = nullptr;
    bus = nullptr;
  }
  void check_bus() {
    GstMessage* message = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (!message) return;
    std::string text = "Pipeline reached EOS";
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      text = std::string(GST_OBJECT_NAME(GST_MESSAGE_SRC(message))) + ": " + (error ? error->message : "unknown error");
      if (debug) text += std::string("; ") + debug;
      g_clear_error(&error);
      g_free(debug);
    }
    gst_message_unref(message);
    throw std::runtime_error(text);
  }
};
std::string mapi_element() {
  for (int index = 0; index < 64; ++index) {
    const std::string path = "/dev/video" + std::to_string(index);
    const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) continue;
    v4l2_capability capability{};
    const bool matches = ioctl(fd, VIDIOC_QUERYCAP, &capability) == 0 &&
                         std::string(reinterpret_cast<char*>(capability.driver)) == "regulus-mapi";
    close(fd);
    if (matches) return "v4l2video" + std::to_string(index) + "convert";
  }
  throw std::runtime_error("MAPI converter not found");
}

struct SharedMemory {
  std::vector<void*> pointers;
  std::atomic<bool> dsp_timed_out{false};
  ~SharedMemory() {
    // A timeout does not cancel DSP DMA. Do not return in-flight allocations to the allocator.
    // The next application launch must reload firmware before using this memory window.
    if (dsp_timed_out.load()) return;
    for (void* pointer : pointers) sp500_shmem_free(pointer);
  }
  sp500_buffer allocate(uint32_t bytes, uint32_t alignment = SP500_ALIGN_NPU) {
    auto buffer = sp500_buffer_alloc(bytes, alignment);
    if (!buffer.data) throw std::runtime_error("SP500 shared-memory allocation failed");
    pointers.push_back(buffer.data);
    return buffer;
  }
};
struct ModelGuard {
  std::unique_ptr<mobilint::Model> value;
  bool launched = false;
  ~ModelGuard() {
    if (launched) value->dispose();
  }
};
struct Slot {
  std::vector<mobilint::PinnedMemory<uint8_t>> inputs;
  std::vector<mobilint::PinnedMemory<float>> outputs;
  sp500_yolo_raw raw{};
  sp500_buffer proto{};
  cv::Mat canvas;
};
struct Detection {
  cv::Rect box;
  cv::Mat mask;
  sp500_yolo_detection detection;
};
struct PostResult {
  sp500_buffer records{}, masks{};
  sp500_yolo_result info{};
};
struct ReturnResult {
  SlotGate<kResultSlots>* pool = nullptr;
  int index = 0;
  const std::atomic<bool>* timed_out = nullptr;
  void operator()(PostResult*) const noexcept {
    // Timeout does not cancel DSP writes. Quarantine allocations until firmware reload.
    if (!timed_out->load()) pool->release(index);
  }
};
struct Frame {
  int slot = 0;
  uint64_t sequence = 0;
  Clock::time_point captured;
  std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{nullptr, &gst_sample_unref};
  std::unique_ptr<PostResult, ReturnResult> result;
  Clock::time_point post_started{}, post_ready{};
  std::vector<Detection> detections;
  std::array<double, kTimings> times{};
  uint64_t mask_bytes = 0;
};
void allocate_slots(std::array<Slot, kSlots>& slots, mobilint::Model& model, mobilint::Accelerator& accelerator,
                    SharedMemory& memory) {
  if (model.getModelInputDataType() != mobilint::DataType::Uint8 ||
      model.getModelOutputDataType() != mobilint::DataType::Float32 ||
      model.getModelInputShape() != std::vector<std::vector<int64_t>>{{kHeight, kWidth, 3}})
    throw std::runtime_error("Expected RGB uint8 HWC 480x640x3 input and float32 outputs");
  const auto shapes = model.getModelOutputShape();
  if (shapes.size() != 10) throw std::runtime_error("Expected 10 raw segmentation output tensors");
  for (auto& slot : slots) {
    mobilint::StatusCode status;
    auto input = accelerator.allocatePinnedMemory<uint8_t>({1, kHeight, kWidth, 3}, status);
    check(status, "allocatePinnedMemory");
    if (!input.data() || input.size() != size_t(kHeight * kWidth * 3)) throw std::runtime_error("Invalid pinned input");
    slot.inputs.push_back(std::move(input));
    slot.canvas.create(kHeight, kWidth, CV_8UC3);
    sp500_yolo_raw_init(&slot.raw);
    for (const auto& shape : shapes) {
      const size_t base = shape.size() == 4 && shape[0] == 1 ? 1 : 0;
      if (shape.size() - base != 3) throw std::runtime_error("Expected HWC raw tensor");
      const int64_t h = shape[base], w = shape[base + 1], channels = shape[base + 2];
      if (h <= 0 || w <= 0 || channels <= 0 || uint64_t(h) * w * channels > UINT32_MAX / sizeof(float))
        throw std::runtime_error("Invalid raw tensor shape");
      const auto buffer = memory.allocate(uint32_t(h * w * channels * sizeof(float)));
      const uint64_t physical = sp500_shmem_phys_addr(buffer.data);
      if (!physical || physical % SP500_ALIGN_NPU || uintptr_t(buffer.data) % SP500_ALIGN_NPU ||
          !(buffer.flags & SP500_BUFFER_SHARED))
        throw std::runtime_error("Invalid NPU/DSP shared buffer address or alignment");
      slot.outputs.emplace_back(shape, physical, buffer.data);
      sp500_buffer* destination = nullptr;
      if (h == kHeight / 4 && w == kWidth / 4 && channels == kCoefficients)
        destination = &slot.proto;
      else
        for (int level = 0; level < 3; ++level)
          if (h == kHeight / (8 << level) && w == kWidth / (8 << level)) {
            if (channels == 64) destination = &slot.raw.box[level];
            if (channels == kClasses) destination = &slot.raw.cls[level];
            if (channels == kCoefficients) destination = &slot.raw.extra[level];
          }
      if (!destination || destination->data) throw std::runtime_error("Unexpected or duplicate raw tensor shape");
      *destination = buffer;
    }
    for (int level = 0; level < 3; ++level)
      if (!slot.raw.box[level].data || !slot.raw.cls[level].data || !slot.raw.extra[level].data)
        throw std::runtime_error("Missing raw feature level");
    if (!slot.proto.data) throw std::runtime_error("Missing prototype output");
  }
}
struct Statistics {
  std::atomic<uint64_t> captured{0}, inferred{0}, decoded{0}, submitted{0}, displayed{0}, instances{0};
  std::atomic<uint64_t> source_sequence_gaps{0}, source_sequence_unknown{0};
  std::mutex mutex;
  Clock::time_point first{}, last{}, window{};
  GstClockTime previous_pts = GST_CLOCK_TIME_NONE;
  uint64_t measured = 0, dropped = 0;
  std::array<double, kTimings> stage_sums{};
  uint64_t mask_bytes = 0, measured_instances = 0;
  double latency_sum = 0, warmup;
  explicit Statistics(double value) : warmup(value) {}
  struct Pending {
    GstClockTime pts = GST_CLOCK_TIME_NONE;
    Clock::time_point captured;
    std::array<double, kTimings> times{};
    uint64_t mask_bytes = 0, instances = 0;
  };
  std::array<Pending, kSlots> pending{};
  static GstPadProbeReturn probe(GstPad*, GstPadProbeInfo* info, gpointer data) {
    auto& self = *static_cast<Statistics*>(data);
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(self.mutex);
    const auto pts = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info));
    if (pts == GST_CLOCK_TIME_NONE || (self.previous_pts != GST_CLOCK_TIME_NONE && pts <= self.previous_pts))
      return GST_PAD_PROBE_OK;
    if (self.previous_pts != GST_CLOCK_TIME_NONE) {
      const uint64_t current = (pts * kFps + GST_SECOND / 2) / GST_SECOND;
      const uint64_t previous = (self.previous_pts * kFps + GST_SECOND / 2) / GST_SECOND;
      if (current > previous + 1) self.dropped += current - previous - 1;
    }
    self.previous_pts = pts;
    ++self.displayed;
    if (self.first == Clock::time_point{}) self.first = now;
    self.last = now;
    if (seconds(self.first, now) < self.warmup) return GST_PAD_PROBE_OK;
    if (self.window == Clock::time_point{}) self.window = now;
    ++self.measured;
    for (const auto& pending : self.pending)
      if (pending.pts == pts) {
        self.latency_sum += seconds(pending.captured, now);
        for (size_t i = 0; i < kTimings; ++i) self.stage_sums[i] += pending.times[i];
        self.mask_bytes += pending.mask_bytes;
        self.measured_instances += pending.instances;
        break;
      }
    return GST_PAD_PROBE_OK;
  }
};

int run(const Options& options) {
  check(sp500_dsp_open(0), "sp500_dsp_open");
  // The public library owns the DSP link. All shared buffers are released before process teardown.
  SharedMemory memory;
  mobilint::StatusCode status;
  auto accelerator = mobilint::Accelerator::create(status);
  check(status, "Accelerator::create");
  ModelGuard model;
  model.value = mobilint::Model::create(options.model, status);
  check(status, "Model::create");
  if (!accelerator || !model.value) throw std::runtime_error("NPU initialization returned null");
  check(model.value->launch(*accelerator), "Model::launch");
  model.launched = true;
  std::array<Slot, kSlots> slots;
  allocate_slots(slots, *model.value, *accelerator, memory);
  // Three raw-output slots plus two full result slots leave about 7 MiB in the 64 MiB DSP window.
  std::array<PostResult, kResultSlots> results;
  SlotGate<kResultSlots> result_gate;
  for (auto& result : results) {
    result.records = memory.allocate(sp500_yolo_records_bytes(SP500_YOLO_TASK_SEGMENT, kMaxDetections));
    result.masks = memory.allocate(kMaskCapacity);
  }
  SlotGate<kSlots> gate;
  Statistics stats(options.warmup);
  const std::string converter = mapi_element();
  Pipeline capture(
      "v4l2src device=/dev/video" + std::to_string(options.camera) +
          " io-mode=0 ! image/jpeg,width=640,height=480,framerate=30/1 ! v4l2jpegdec capture-io-mode=4 ! " + converter +
          " output-io-mode=5 capture-io-mode=4 ! video/x-raw,format=RGB,width=640,height=480 ! "
          "appsink name=capture max-buffers=8 drop=false sync=false emit-signals=false",
      "capture");
  Pipeline display(
      "appsrc name=display is-live=true format=time max-buffers=2 block=false emit-signals=false ! "
      "video/x-raw,format=RGB,width=640,height=480,framerate=30/1,interlace-mode=progressive,pixel-aspect-ratio=1/1 "
      "! " +
          converter + " disable-passthrough=true ! video/x-raw,format=RGB,width=" + std::to_string(kDisplayWidth) +
          ",height=" + std::to_string(kDisplayHeight) +
          ",framerate=30/1,interlace-mode=progressive,pixel-aspect-ratio=1/1 ! "
          "kmssink name=hdmi driver-name=regulus force-modesetting=true sync=false qos=false",
      "display");
  GstElement* hdmi = gst_bin_get_by_name(GST_BIN(display.element), "hdmi");
  GstPad* pad = gst_element_get_static_pad(hdmi, "sink");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, Statistics::probe, &stats, nullptr);
  gst_object_unref(pad);
  gst_object_unref(hdmi);
  Queue<Frame> captured(1), inferred(kSlots), dsp_done(kResultSlots), decoded(kSlots), overlaid(kSlots);
  std::atomic<bool> running{true}, failed{false};
  auto stop = [&] {
    running.store(false);
    captured.close();
    inferred.close();
    dsp_done.close();
    result_gate.close();
    decoded.close();
    overlaid.close();
    gate.close();
  };
  auto fail = [&](const char* stage, const std::exception& error) {
    std::cerr << '[' << stage << "] " << error.what() << '\n';
    failed.store(true);
    stop();
  };
  std::array<cv::Vec3b, kClasses> palette;
  for (int i = 0; i < kClasses; ++i) {
    // Palette construction is not camera color conversion; MAPI supplies every RGB frame.
    cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar((i * 47) % 180, 200, 255)), rgb;
    cv::cvtColor(hsv, rgb, cv::COLOR_HSV2RGB);
    palette[i] = rgb.at<cv::Vec3b>(0, 0);
  }
  display.start();
  capture.start();
  std::cout << "[demo] qbruntime=" << mobilint::getQbRuntimeVersion() << " SP500=" << sp500_version_string()
            << " model=" << options.model << " camera=/dev/video" << options.camera
            << " input=RGB_uint8_640x480 output=" << kDisplayWidth << 'x' << kDisplayHeight
            << " postprocess=DSP mask=full preprocessing=CPU_direct_pinned post_workers=2 result_slots=" << kResultSlots
            << " frame_slots=" << kSlots << '\n';
  std::vector<std::thread> threads;
  threads.reserve(6);
  auto worker = [&](const char* name, int core, auto function) {
    threads.emplace_back([&, name, core, function] {
      pin_thread(core);
      try {
        function();
      } catch (const std::exception& error) {
        fail(name, error);
      }
    });
  };
  try {
    worker("capture", 0, [&] {
      uint64_t sequence = 0;
      uint64_t previous_offset = GST_BUFFER_OFFSET_NONE;
      while (running.load()) {
        const int index = int(sequence % kSlots);
        if (!gate.acquire(index)) break;
        GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(capture.endpoint), 500 * GST_MSECOND);
        if (!sample) {
          gate.release(index);
          if (!running.load()) break;
          // The main loop owns the pipeline bus; appsink also reports EOS directly.
          GstState state = GST_STATE_NULL;
          gst_element_get_state(capture.endpoint, &state, nullptr, 0);
          if (state >= GST_STATE_PAUSED && gst_app_sink_is_eos(GST_APP_SINK(capture.endpoint)))
            throw std::runtime_error("Camera reached EOS");
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        Frame frame;
        frame.slot = index;
        frame.sequence = sequence++;
        frame.captured = Clock::now();
        frame.sample.reset(sample);
        GstBuffer* source_buffer = gst_sample_get_buffer(sample);
        const uint64_t offset = source_buffer ? GST_BUFFER_OFFSET(source_buffer) : GST_BUFFER_OFFSET_NONE;
        if (offset == GST_BUFFER_OFFSET_NONE)
          ++stats.source_sequence_unknown;
        else {
          if (previous_offset != GST_BUFFER_OFFSET_NONE && offset > previous_offset + 1)
            stats.source_sequence_gaps.fetch_add(offset - previous_offset - 1);
          previous_offset = offset;
        }
        ++stats.captured;
        if (!captured.push(std::move(frame))) break;
      }
      captured.close();
    });
    worker("inference", 1, [&] {
      while (running.load()) {
        auto frame = captured.pop();
        if (!frame) break;
        auto& slot = slots[frame->slot];
        const auto start = Clock::now();
        GstBuffer* buffer = gst_sample_get_buffer(frame->sample.get());
        if (!buffer) throw std::runtime_error("Camera sample has no buffer");
        CpuRead access(buffer);
        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_from_caps(&info, gst_sample_get_caps(frame->sample.get())) ||
            GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_RGB)
          throw std::runtime_error("MAPI did not return RGB");
        GstVideoFrame video;
        if (!gst_video_frame_map(&video, &info, buffer, GST_MAP_READ))
          throw std::runtime_error("Cannot map camera frame");
        try {
          const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&video, 0);
          if (stride < GST_VIDEO_INFO_WIDTH(&info) * 3) throw std::runtime_error("Invalid MAPI RGB stride");
          cv::Mat source(GST_VIDEO_INFO_HEIGHT(&info), GST_VIDEO_INFO_WIDTH(&info), CV_8UC3,
                         GST_VIDEO_FRAME_PLANE_DATA(&video, 0), stride);
          cv::Mat input(kHeight, kWidth, CV_8UC3, slot.inputs[0].data());
          // Cached canvas avoids repeated CPU reads of write-combining NPU input during full overlay.
          preprocess_rgb(source, input, &slot.canvas);
        } catch (...) {
          gst_video_frame_unmap(&video);
          throw;
        }
        gst_video_frame_unmap(&video);
        access.finish();
        frame->sample.reset();
        const auto ready = Clock::now();
        frame->times[0] = seconds(start, ready);
        check(model.value->inferPinnedMemory(slot.inputs, slot.outputs, 0), "inferPinnedMemory");
        frame->times[1] = seconds(ready, Clock::now());
        ++stats.inferred;
        if (!inferred.push(std::move(*frame))) break;
      }
      inferred.close();
    });
    worker("DSP", 0, [&] {
      sp500_yolo_params params;
      sp500_yolo_params_init(&params);
      params.img_w = kWidth;
      params.img_h = kHeight;
      params.num_classes = kClasses;
      params.max_detections = kMaxDetections;
      params.conf_threshold = options.confidence;
      params.iou_threshold = options.iou;
      params.mask_coeffs = kCoefficients;
      params.proto_h = kHeight / 4;
      params.proto_w = kWidth / 4;
      params.mask_threshold = std::log(options.mask / (1 - options.mask));
      sp500_yolo_output output;
      sp500_yolo_output_init(&output);
      int next_result = 0;
      while (running.load()) {
        auto frame = inferred.pop();
        if (!frame) break;
        auto& slot = slots[frame->slot];
        params.proto = slot.proto;
        const auto waiting = Clock::now();
        if (!result_gate.acquire(next_result)) break;
        frame->result = {&results[next_result], ReturnResult{&result_gate, next_result, &memory.dsp_timed_out}};
        next_result = (next_result + 1) % kResultSlots;
        if (!running.load()) break;
        auto& result = *frame->result;
        output.records = result.records;
        output.mask = result.masks;
        const auto start = Clock::now();
        frame->times[6] = seconds(waiting, start);
        frame->post_started = start;
        result.info = {};
        const sp500_status decode_status = sp500_yolo_decode(SP500_YOLO_HEAD_ANCHORLESS, SP500_YOLO_TASK_SEGMENT,
                                                             &slot.raw, &params, &output, &result.info);
        const auto decoded_at = Clock::now();
        frame->times[4] = seconds(start, decoded_at);
        if (decode_status == SP500_ERR_TIMEOUT) {
          memory.dsp_timed_out.store(true);
          std::cerr << "[postprocess] DSP timeout; reload firmware before restarting\n";
        }
        check(decode_status, "sp500_yolo_decode");
        if (result.info.count > kMaxDetections || result.info.record_floats != SP500_YOLO_SEGMENT_RECORD_FLOATS)
          throw std::runtime_error("Invalid DSP record count/layout");
        frame->post_ready = Clock::now();
        ++stats.decoded;
        if (!dsp_done.push(std::move(*frame))) break;
      }
      dsp_done.close();
    });
    worker("mask cache", 2, [&] {
      while (running.load()) {
        auto frame = dsp_done.pop();
        if (!frame) break;
        const auto start = Clock::now();
        frame->times[7] = seconds(frame->post_ready, start);
        const auto& result = *frame->result;
        const auto& records = result.records;
        const auto& masks = result.masks;
        const auto* values = static_cast<const sp500_yolo_segmentation*>(records.data);
        frame->detections.reserve(result.info.count);
        for (uint32_t i = 0; i < result.info.count; ++i) {
          const auto& value = values[i];
          for (float coordinate :
               {value.det.x1, value.det.y1, value.det.x2, value.det.y2, value.det.score, value.det.class_id})
            if (!std::isfinite(coordinate)) throw std::runtime_error("Non-finite DSP detection");
          if (value.det.score < 0 || value.det.score > 1 || value.det.class_id < 0 || value.det.class_id >= kClasses ||
              value.det.class_id != std::floor(value.det.class_id))
            throw std::runtime_error("Invalid DSP class/score");
          const auto box = validate_mask_span(value.bx1, value.by1, value.bx2, value.by2,
                                              sp500_yolo_mask_offset(&value), masks.size);
          cv::Mat mask;
          if (!box.empty()) {
            auto* data = static_cast<uint8_t*>(masks.data) + sp500_yolo_mask_offset(&value);
            // This result lease prevents DSP reuse until every mask has an owning cached copy.
            mask = cache_mask(data, box.width, box.height);
            frame->mask_bytes += uint64_t(box.width) * box.height;
          }
          frame->detections.push_back({box, std::move(mask), value.det});
        }
        const auto copied = Clock::now();
        frame->times[2] = seconds(frame->post_started, copied);
        frame->times[5] = seconds(start, copied);
        stats.instances.fetch_add(frame->detections.size());
        frame->result.reset();  // Return exactly once, before a potentially blocking downstream push.
        if (!decoded.push(std::move(*frame))) break;
      }
      decoded.close();
    });
    worker("overlay", 3, [&] {
      while (running.load()) {
        auto frame = decoded.pop();
        if (!frame) break;
        auto& image = slots[frame->slot].canvas;
        const auto start = Clock::now();
        for (const auto& detection : frame->detections)
          if (!detection.mask.empty())
            blend_mask(image, detection.mask, detection.box, palette[int(detection.detection.class_id)], 0, kHeight);
        for (const auto& detection : frame->detections) {
          const auto& det = detection.detection;
          const auto color = palette[int(det.class_id)];
          const cv::Point first(int(std::clamp(det.x1, 0.0f, float(kWidth - 1))),
                                int(std::clamp(det.y1, 0.0f, float(kHeight - 1))));
          const cv::Point last(int(std::clamp(det.x2, 0.0f, float(kWidth - 1))),
                               int(std::clamp(det.y2, 0.0f, float(kHeight - 1))));
          cv::rectangle(image, first, last, cv::Scalar(color[0], color[1], color[2]), 2);
          cv::putText(image, std::string(kNames[int(det.class_id)]) + cv::format(" %.2f", det.score),
                      cv::Point(first.x, std::max(15, first.y)), cv::FONT_HERSHEY_SIMPLEX, .5,
                      cv::Scalar(color[0], color[1], color[2]), 1);
        }
        frame->times[3] = seconds(start, Clock::now());
        if (!overlaid.push(std::move(*frame))) break;
      }
      overlaid.close();
    });
    worker("display", 0, [&] {
      struct Release {
        SlotGate<kSlots>* gate;
        int slot;
      };
      while (running.load()) {
        auto frame = overlaid.pop();
        if (!frame) break;
        auto& image = slots[frame->slot].canvas;
        auto* context = new Release{&gate, frame->slot};
        GstBuffer* buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, image.data, image.total() * 3, 0,
                                                        image.total() * 3, context, [](gpointer data) {
                                                          auto* value = static_cast<Release*>(data);
                                                          value->gate->release(value->slot);
                                                          delete value;
                                                        });
        if (!buffer) {
          delete context;
          throw std::runtime_error("Cannot wrap display buffer");
        }
        const GstClockTime pts = frame->sequence * GST_SECOND / kFps;
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DURATION(buffer) = GST_SECOND / kFps;
        {
          std::lock_guard<std::mutex> lock(stats.mutex);
          stats.pending[frame->slot] = {pts, frame->captured, frame->times, frame->mask_bytes,
                                        frame->detections.size()};
        }
        if (gst_app_src_push_buffer(GST_APP_SRC(display.endpoint), buffer) != GST_FLOW_OK)
          throw std::runtime_error("HDMI appsrc rejected frame");
        ++stats.submitted;
      }
    });
    const auto started = Clock::now();
    auto report = started;
    uint64_t last_count = 0, last_input = 0, last_measured = 0, last_mask_bytes = 0, last_instances = 0;
    std::array<double, kTimings> last_sums{};
    double last_latency = 0;
    while (running.load() && !stop_signal) {
      capture.check_bus();
      display.check_bus();
      const auto now = Clock::now();
      {
        std::unique_lock<std::mutex> lock(stats.mutex);
        if (stats.first == Clock::time_point{} && seconds(started, now) > 10)
          throw std::runtime_error("No HDMI output within 10 seconds");
        if (stats.first != Clock::time_point{} && seconds(stats.last, now) > 5)
          throw std::runtime_error("Video pipeline stalled");
        if (seconds(report, now) >= 1) {
          const uint64_t count = stats.displayed.load();
          const uint64_t input_count = stats.captured.load();
          const uint64_t measured = stats.measured;
          const uint64_t frames = measured - last_measured;
          const auto sums = stats.stage_sums;
          const double latency = stats.latency_sum;
          const uint64_t mask_bytes = stats.mask_bytes, instances = stats.measured_instances, gaps = stats.dropped;
          const uint64_t unknown = stats.source_sequence_unknown.load();
          const double elapsed = seconds(report, now);
          lock.unlock();  // Terminal/SSH output must not hold up the HDMI measurement probe.
          auto rate = [](const char* name, double fps) {
            std::cout << std::left << std::setw(25) << name << std::right << std::setw(9) << std::fixed
                      << std::setprecision(2) << fps << " FPS  (" << std::setprecision(3) << (fps > 0 ? 1000 / fps : 0)
                      << " ms)\n";
          };
          auto cost = [](const char* name, double milliseconds) {
            std::cout << std::left << std::setw(25) << name << std::right << std::setw(9) << std::fixed
                      << std::setprecision(3) << milliseconds << " ms   (" << std::setw(8) << std::setprecision(2)
                      << (milliseconds > 0 ? 1000 / milliseconds : 0) << " FPS)\n";
          };
          std::cout << "\n--- throughput ---\n";
          rate("input frame rate:", (input_count - last_input) / elapsed);
          rate("output frame rate:", (count - last_count) / elapsed);
          std::cout << "output sequence gaps: " << gaps << "  source sequence unknown: " << unknown << '\n';
          if (frames) {
            std::array<double, kTimings> ms{};
            for (size_t i = 0; i < kTimings; ++i) ms[i] = (sums[i] - last_sums[i]) * 1000 / frames;
            std::cout << "--- latency (appsink -> HDMI; not sensor exposure) ---\n"
                      << "end-to-end: " << std::setprecision(3) << (latency - last_latency) * 1000 / frames
                      << " ms\n--- stage time (per frame; FPS = capacity, not output rate) ---\n";
            cost("infer worker:", ms[0] + ms[1]);
            cost("  CPU preprocess:", ms[0]);
            cost("  NPU inference:", ms[1]);
            cost("DSP round trip:", ms[4]);
            cost("mask cache worker:", ms[5]);
            cost("overlay:", ms[3]);
            std::cout << "--- post latency/wait (workers overlap; not capacity) ---\n"
                      << "DSP start -> cache end: " << ms[2] << " ms\n"
                      << "free result wait: " << ms[6] << " ms\n"
                      << "cache queue wait: " << ms[7] << " ms\n";
            std::cout << "mask work: " << std::setprecision(2) << double(instances - last_instances) / frames
                      << " instances/frame, " << double(mask_bytes - last_mask_bytes) / frames / 1024 << " KiB/frame\n";
          } else
            std::cout << "warmup: waiting for measured HDMI frames\n";
          std::cout << std::flush;
          report = now;
          last_count = count;
          last_input = input_count;
          last_measured = measured;
          last_sums = sums;
          last_latency = latency;
          last_mask_bytes = mask_bytes;
          last_instances = instances;
          lock.lock();
        }
        if (options.duration > 0 && stats.window != Clock::time_point{} &&
            seconds(stats.window, stats.last) >= options.duration)
          break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch (const std::exception& error) {
    fail("main", error);
  }
  stop();
  for (auto& thread : threads)
    if (thread.joinable()) thread.join();
  capture.reset();
  gst_app_src_end_of_stream(GST_APP_SRC(display.endpoint));
  display.reset();  // Complete all callbacks before gate/canvas/pinned memory destruction.
  const auto dispose_status = model.value->dispose();
  model.launched = false;
  if (!dispose_status) {
    std::cerr << "[shutdown] Model::dispose failed\n";
    failed.store(true);
  }
  const double elapsed = stats.window == Clock::time_point{} ? 0 : seconds(stats.window, stats.last);
  const double measured_fps = elapsed > 0 && stats.measured > 1 ? double(stats.measured - 1) / elapsed : 0;
  std::cout << std::fixed << std::setprecision(4) << "MEASUREMENT elapsed_s=" << elapsed << " frames=" << stats.measured
            << " output_fps=" << measured_fps << " capture=" << stats.captured.load()
            << " infer=" << stats.inferred.load() << " dsp=" << stats.decoded.load()
            << " submitted=" << stats.submitted.load() << " displayed=" << stats.displayed.load()
            << " output_sequence_gaps=" << stats.dropped
            << " source_sequence_gaps=" << stats.source_sequence_gaps.load()
            << " source_sequence_unknown=" << stats.source_sequence_unknown.load() << " fatal_error=" << failed.load();
  for (size_t i = 0; i < kTimings; ++i)
    std::cout << ' '
              << std::array<const char*, kTimings>{"pre_ms",     "infer_ms",      "post_ms",        "overlay_ms",
                                                   "dsp_rpc_ms", "mask_cache_ms", "result_wait_ms", "cache_wait_ms"}[i]
              << '=' << (stats.measured ? stats.stage_sums[i] * 1000 / stats.measured : 0);
  std::cout << " latency_ms=" << (stats.measured ? stats.latency_sum * 1000 / stats.measured : 0)
            << " mask_KiB=" << (stats.measured ? double(stats.mask_bytes) / stats.measured / 1024 : 0) << '\n';
  return failed.load() ? 1 : 0;
}
}  // namespace demo

int main(int argc, char** argv) {
  try {
    // Internal launcher protocol: help exits 0; valid arguments and model return 64 before hardware initialization.
    const bool check_args = argc > 1 && std::string(argv[1]) == "--launcher-check";
    const auto options = demo::parse_options(argc - check_args, argv + check_args);
    if (check_args) return 64;
    std::signal(SIGINT, demo::on_signal);
    std::signal(SIGTERM, demo::on_signal);
    gst_init(nullptr, nullptr);
    cv::setNumThreads(1);
    return demo::run(options);
  } catch (const std::exception& error) {
    std::cerr << "[demo] " << error.what() << '\n';
    return 1;
  }
}
