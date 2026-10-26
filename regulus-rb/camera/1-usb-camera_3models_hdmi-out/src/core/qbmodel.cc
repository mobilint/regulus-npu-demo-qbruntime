#include "qbmodel.h"

#include <qbruntime/status_code.h>
#include <qbruntime/type.h>
#include <sp500/dsp.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "types.h"

namespace app {

QbMultiModel::QbMultiModel(const std::vector<std::string>& model_paths) {
  try {
    StatusCode status;
    acc = Accelerator::create(status);
    if (!status) {
      throw std::runtime_error("Accelerator::create() failed");
    }

    int input_height = 0;
    int input_width = 0;
    int input_channels = 0;
    DataType input_type = DataType::Error;
    DataType output_type = DataType::Error;
    for (const auto& model_path : model_paths) {
      // Let the loaded runtime construct its default config internally.
      std::unique_ptr<Model> model = Model::create(model_path, status);
      if (!status || model == nullptr) {
        // Missing or unreadable .mxq: say which one and stop, rather than
        // dereferencing null.
        throw std::runtime_error("Model::create() failed: " + model_path);
      }

      const auto& input_info = model->getInputBufferInfo();
      if (input_info.size() != 1) throw std::runtime_error("model must have one input");
      BufferInfo buffer_info = input_info[0];
      const DataType model_input_type = model->getModelInputDataType();
      const DataType model_output_type = model->getModelOutputDataType();
      if ((!_models.empty() && (input_height != static_cast<int>(buffer_info.original_height) ||
                                input_width != static_cast<int>(buffer_info.original_width) ||
                                input_channels != static_cast<int>(buffer_info.original_channel) ||
                                input_type != model_input_type || output_type != model_output_type)) ||
          buffer_info.original_channel != 3 || model_input_type != DataType::Uint8 ||
          model_output_type != DataType::Float32) {
        throw std::runtime_error("model input shapes or types are inconsistent");
      }
      input_height = buffer_info.original_height;
      input_width = buffer_info.original_width;
      input_channels = buffer_info.original_channel;
      input_type = model_input_type;
      output_type = model_output_type;

      std::cout << "launch model created from " << model->getModelPath() << ", hwc: " << input_height << " "
                << input_width << " " << input_channels << "\n";
      status = model->launch(*acc);
      if (!status) throw std::runtime_error("Model::launch() failed: " + model_path);

      // Output ring in SP500 shared memory so the NPU writes and the DSP reads
      // without a copy. inferPinnedMemory writes the NPU output here.
      const auto output_shapes = model->getModelOutputShape();
      _models.push_back({std::move(model), {}, 0});
      _Model& model_state = _models.back();
      model_state.out_ring.resize(kOutRing);
      for (int slot = 0; slot < kOutRing; ++slot) {
        for (const auto& output_shape : output_shapes) {
          uint64_t byte_count = sizeof(float);
          for (const int64_t dimension : output_shape) {
            if (dimension <= 0 || byte_count > UINT32_MAX / static_cast<uint64_t>(dimension)) {
              std::cerr << "Invalid model output size.\n";
              throw std::runtime_error("invalid model output size");
            }
            byte_count *= static_cast<uint64_t>(dimension);
          }
          const uint32_t bytes = static_cast<uint32_t>(byte_count);
          void* virtual_address = sp500_shmem_alloc(bytes, SP500_ALIGN_NPU);
          if (virtual_address == nullptr) {
            std::cout << "sp500_shmem_alloc() failed for an NPU output.\n";
            throw std::runtime_error("sp500_shmem_alloc() failed for an NPU output");
          }
          const uint64_t physical_address = sp500_shmem_phys_addr(virtual_address);
          if (sp500_shmem_contains(virtual_address) != 1 || physical_address == 0) {
            std::cerr << "SP500 output buffer is not valid shared memory.\n";
            sp500_shmem_free(virtual_address);
            throw std::runtime_error("SP500 output is not valid shared memory");
          }
          out_shmem.push_back(virtual_address);
          model_state.out_ring[slot].emplace_back(output_shape, physical_address, virtual_address);
        }
      }
    }

    // One shared NHWC uint8 input buffer for all models (same input geometry).
    input = std::make_unique<PinnedMemory<uint8_t>>(
        acc->allocatePinnedMemory<uint8_t>({1, input_height, input_width, input_channels}, status));
    if (!status) {
      std::cout << "allocatePinnedMemory failed.\n";
      throw std::runtime_error("allocatePinnedMemory() failed");
    }
  } catch (...) {
    cleanup();
    throw;
  }
}

StatusCode QbMultiModel::infer(int idx, std::vector<RawTensor>& out) {
  std::shared_lock<std::shared_mutex> input_lock(input_mutex_);
  auto& model_state = _models[idx];
  auto& outputs = model_state.out_ring[model_state.slot];
  model_state.slot = (model_state.slot + 1) % kOutRing;

  std::vector<PinnedMemory<uint8_t>> inputs{*input};
  StatusCode status;
  try {
    status = model_state.model->inferPinnedMemory(inputs, outputs, 0);
  } catch (...) {
    input_lock.unlock();
    input_read_complete();
    throw;
  }
  input_lock.unlock();
  input_read_complete();
  if (!status) return status;

  out.clear();
  out.reserve(outputs.size());
  // Pass every raw head tensor to decode. It identifies the box, class, and
  // keypoint planes by grid size and channel count.
  for (auto& output : outputs) out.push_back({output.data(), output.size(), output.shape()});
  return status;
}

void QbMultiModel::input_read_complete() noexcept {
  std::lock_guard<std::mutex> state_lock(input_state_mutex_);
  if (pending_input_readers_ > 0 && --pending_input_readers_ == 0) input_state_cv_.notify_one();
}

void QbMultiModel::cancel_input() noexcept {
  std::lock_guard<std::mutex> state_lock(input_state_mutex_);
  input_cancelled_ = true;
  pending_input_readers_ = 0;
  input_state_cv_.notify_all();
}

void QbMultiModel::cleanup() noexcept {
  for (auto& model_state : _models) {
    if (model_state.model) model_state.model->dispose();
  }
  for (auto& model_state : _models) {
    model_state.out_ring.clear();
  }
  for (void* virtual_address : out_shmem) sp500_shmem_free(virtual_address);
  out_shmem.clear();
  input.reset();
  for (auto& model_state : _models) model_state.model.reset();
  _models.clear();
  acc.reset();
}

QbMultiModel::~QbMultiModel() { cleanup(); }

}  // namespace app
