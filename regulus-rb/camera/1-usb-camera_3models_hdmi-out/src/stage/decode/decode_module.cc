#include "decode_module.h"

#include <sp500/dsp.h>
#include <sp500/yolo.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>

namespace app {
namespace {
constexpr int kMaxDets = 64;
constexpr int kDflBoxChannels = 64;  // YOLO11: 4 x 16 DFL logits per box
constexpr int kPoseChannels = 51;
constexpr int kLevels[] = {8, 16, 32};

bool tensor_shape(const RawTensor& tensor, int64_t& h, int64_t& w, int64_t& c) {
  if (tensor.shape.size() == 3) {
    h = tensor.shape[0];
    w = tensor.shape[1];
    c = tensor.shape[2];
    return true;
  }
  if (tensor.shape.size() == 4 && tensor.shape[0] == 1) {
    h = tensor.shape[1];
    w = tensor.shape[2];
    c = tensor.shape[3];
    return true;
  }
  return false;
}

// Raw head planes at each level (stride 8, 16, 32).
struct Planes {
  const RawTensor* box[3] = {};
  const RawTensor* cls[3] = {};
  const RawTensor* extra[3] = {};  // pose keypoints
};

// Identify the fixed YOLO11s box, class, and keypoint planes by shape.
bool identify(const std::vector<RawTensor>& raw, const DecodeConfig& cfg, Planes& planes) {
  const bool pose = cfg.kind == DecodeKind::Pose;
  for (const RawTensor& tensor : raw) {
    int64_t h = 0, w = 0, c = 0;
    if (!tensor_shape(tensor, h, w, c)) return false;
    int level_index = -1;
    for (int i = 0; i < 3; ++i)
      if (h == cfg.imh / kLevels[i] && w == cfg.imw / kLevels[i]) level_index = i;
    if (level_index < 0) return false;
    if (c == kDflBoxChannels) {
      planes.box[level_index] = &tensor;
    } else if (c == cfg.nc) {
      planes.cls[level_index] = &tensor;
    } else if (pose && c == kPoseChannels) {
      planes.extra[level_index] = &tensor;
    }
  }
  for (int i = 0; i < 3; ++i)
    if (!planes.box[i] || !planes.cls[i] || (pose && !planes.extra[i])) return false;
  return true;
}

bool validate_tensor(const RawTensor& tensor, int level) {
  int64_t h = 0, w = 0, c = 0;
  tensor_shape(tensor, h, w, c);
  const uint64_t elements = static_cast<uint64_t>(h) * static_cast<uint64_t>(w) * static_cast<uint64_t>(c);
  if (tensor.data == nullptr || elements > std::numeric_limits<uint64_t>::max() / sizeof(float) ||
      tensor.bytes != elements * sizeof(float) || tensor.bytes > UINT32_MAX) {
    std::cerr << "[decode] invalid tensor byte size at level " << level << "\n";
    return false;
  }
  if (sp500_shmem_contains(tensor.data) != 1 || sp500_shmem_phys_addr(tensor.data) == 0) {
    std::cerr << "[decode] tensor is not in SP500 shared memory at level " << level << "\n";
    return false;
  }
  return true;
}

sp500_buffer shared_buffer(const RawTensor& tensor) {
  return {const_cast<float*>(tensor.data), static_cast<uint32_t>(tensor.bytes), SP500_BUFFER_SHARED};
}

void request_fatal_stop(const DecodeConfig& cfg, const char* reason) {
  std::cerr << "[decode:" << cfg.branch << "] " << reason << "\n";
  if (cfg.run) cfg.run->stop(StopReason::Decode);
}
}  // namespace

std::thread decode_module(DecodeConfig cfg, Queue& in, Queue& out) {
  return std::thread([cfg, &in, &out]() {
    const bool pose = cfg.kind == DecodeKind::Pose;
    const int task = pose ? SP500_YOLO_TASK_POSE : SP500_YOLO_TASK_DETECT;
    bool head_reported = false;

    sp500_yolo_params params;
    sp500_yolo_params_init(&params);
    params.img_w = cfg.imw;
    params.img_h = cfg.imh;
    params.num_classes = cfg.nc;
    params.max_detections = kMaxDets;
    params.conf_threshold = cfg.conf_thres;
    params.iou_threshold = cfg.iou_thres;

    sp500_yolo_output output;
    sp500_yolo_output_init(&output);
    output.records = sp500_buffer_alloc(sp500_yolo_records_bytes(task, kMaxDets), SP500_ALIGN_DSP);
    if (output.records.data == nullptr) {
      request_fatal_stop(cfg, "cannot allocate result records");
      return;
    }

    try {
      while (cfg.run->running()) {
        Packet packet;
        if (in.pop(packet) == Queue::CLOSED) break;
        if (cfg.tl) cfg.tl->det_decode_in.addValue(age_ms(packet.time_point));
        if (cfg.bc) cfg.bc->start();

        Planes planes;
        if (!identify(packet.raw, cfg, planes)) {
          request_fatal_stop(cfg, "outputs are not a YOLO11s head for this model");
          break;
        }
        constexpr int32_t head = SP500_YOLO_HEAD_ANCHORLESS;
        if (!head_reported) {
          std::cout << "[decode:" << cfg.branch << "] YOLO11s" << (pose ? " pose" : " det") << " head, " << cfg.nc
                    << " classes -> DSP anchorless\n";
          head_reported = true;
        }

        sp500_yolo_raw raw;
        sp500_yolo_raw_init(&raw);
        bool valid = true;
        for (size_t level_index = 0; level_index < 3; ++level_index) {
          const int level = kLevels[level_index];
          valid = validate_tensor(*planes.box[level_index], level) &&
                  validate_tensor(*planes.cls[level_index], level) && valid;
          raw.box[level_index] = shared_buffer(*planes.box[level_index]);
          raw.cls[level_index] = shared_buffer(*planes.cls[level_index]);
          if (pose) {
            valid = validate_tensor(*planes.extra[level_index], level) && valid;
            raw.extra[level_index] = shared_buffer(*planes.extra[level_index]);
          }
        }
        if (!valid) {
          request_fatal_stop(cfg, "raw tensor validation failed");
          break;
        }

        sp500_yolo_result result{};
        const sp500_status status = sp500_yolo_decode(head, task, &raw, &params, &output, &result);
        packet.raw.clear();
        if (status != SP500_OK) {
          std::cerr << "[decode:" << cfg.branch << "] sp500_yolo_decode() failed: " << sp500_status_string(status)
                    << " (" << status << ")\n";
          if (status == SP500_ERR_TIMEOUT)
            std::cerr << "[decode:" << cfg.branch
                      << "] timeout is fatal; reload DSP firmware before "
                         "another run\n";
          cfg.run->stop(status == SP500_ERR_TIMEOUT ? StopReason::DspTimeout : StopReason::Decode);
          break;
        }

        packet.dets = Detections{};
        if (pose) {
          const auto* records = static_cast<const sp500_yolo_pose*>(output.records.data);
          for (uint32_t i = 0; i < std::min<uint32_t>(result.count, kMaxDets); ++i) {
            const auto& record = records[i];
            packet.dets.boxes.push_back({record.det.x1, record.det.y1, record.det.x2, record.det.y2});
            packet.dets.scores.push_back(record.det.score);
            packet.dets.labels.push_back(static_cast<int>(record.det.class_id));
            auto& keypoints = packet.dets.keypoints.emplace_back();
            keypoints.reserve(kPoseChannels);
            for (const auto& keypoint : record.keypoints) {
              keypoints.push_back(keypoint.x);
              keypoints.push_back(keypoint.y);
              keypoints.push_back(keypoint.score);
            }
          }
        } else {
          const auto* records = static_cast<const sp500_yolo_detection*>(output.records.data);
          for (uint32_t i = 0; i < std::min<uint32_t>(result.count, kMaxDets); ++i) {
            const auto& record = records[i];
            packet.dets.boxes.push_back({record.x1, record.y1, record.x2, record.y2});
            packet.dets.scores.push_back(record.score);
            packet.dets.labels.push_back(static_cast<int>(record.class_id));
            packet.dets.keypoints.emplace_back();
          }
        }

        if (cfg.bc) cfg.bc->end();
        if (cfg.activity) cfg.activity->fetch_add(1, std::memory_order_relaxed);
        if (cfg.tl) cfg.tl->det_decode_out.addValue(age_ms(packet.time_point));
        const SteadyTime detection_time = packet.time_point;
        if (out.push(std::move(packet)) == Queue::CLOSED) break;
        if (cfg.tl) cfg.tl->det_decode_pushed.addValue(age_ms(detection_time));
      }
    } catch (const std::exception& error) {
      std::cerr << "[decode:" << cfg.branch << "] " << error.what() << "\n";
      cfg.run->stop(StopReason::Decode);
    } catch (...) {
      std::cerr << "[decode:" << cfg.branch << "] unknown exception\n";
      cfg.run->stop(StopReason::Decode);
    }
    if (cfg.run->timed_out())
      cfg.run->keep_timeout_buffer(output.records);
    else
      sp500_buffer_release(&output.records);
  });
}
}  // namespace app
