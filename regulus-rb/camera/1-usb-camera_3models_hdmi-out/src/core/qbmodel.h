#ifndef CORE_QBMODEL_H_
#define CORE_QBMODEL_H_

#include <qbruntime/qbruntime.h>
#include <qbruntime/type.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "types_fwd.h"

using mobilint::Accelerator;
using mobilint::BufferInfo;
using mobilint::DataType;
using mobilint::Model;
using mobilint::ModelConfig;
using mobilint::PinnedMemory;
using mobilint::StatusCode;

namespace app {

// Loads N models that share ONE plain-RGB pinned input buffer (NHWC uint8). Inference
// uses `inferPinnedMemory` (zero-copy NPU I/O) so no repositionInputs step is needed.
// Outputs stay in pinned memory: `infer` returns pointers into the model's output pinned
// buffers and decode reads them in place (no host copy). Each model owns a small ring of
// output pinned sets so the infer thread can produce the next frame without clobbering a
// set that decode is still reading (the infer->decode queue is depth-1, so at most ~2
// sets are live). One infer thread per model (pinned inference on a given model must be
// serial; different models run concurrently).
class QbMultiModel {
    static constexpr int kOutRing = 4;

    struct _Model {
        std::unique_ptr<Model> model;
        std::vector<std::vector<PinnedMemory<float>>> out_ring;  // [slot][tensor], reused
        int slot = 0;
    };

    void cleanup() noexcept;
    void input_read_complete() noexcept;

    std::shared_mutex input_mutex_;
    std::mutex input_state_mutex_;
    std::condition_variable input_state_cv_;
    size_t pending_input_readers_ = 0;
    bool input_cancelled_ = false;

   public:
    QbMultiModel(const std::vector<std::string>& model_paths);
    ~QbMultiModel();

    std::unique_ptr<Accelerator> acc;
    ModelConfig mc;
    std::vector<_Model> _models;
    std::unique_ptr<PinnedMemory<uint8_t>> input;  // shared NHWC uint8 input (all models)
    std::vector<void*> out_shmem;  // SP500 shared buffers backing the output ring

    int model_count() const { return (int)_models.size(); }

    template <typename Writer>
    bool write_input(Writer&& writer) {
        {
            std::unique_lock<std::mutex> state_lock(input_state_mutex_);
            input_state_cv_.wait(state_lock, [this] {
                return pending_input_readers_ == 0 || input_cancelled_;
            });
            if (input_cancelled_) return false;
        }

        std::unique_lock<std::shared_mutex> input_lock(input_mutex_);
        writer(input->data());
        {
            std::lock_guard<std::mutex> state_lock(input_state_mutex_);
            if (input_cancelled_) return false;
            pending_input_readers_ = _models.size();
        }
        return true;
    }

    void cancel_input() noexcept;

    // Run model `idx` on the shared input. `out` is filled with pointers to the model's
    // output head tensors (float), which live in pinned memory and stay valid until this
    // model's ring wraps back to the same slot (kOutRing frames later). decode must read
    // them before then.
    StatusCode infer(int idx, std::vector<RawTensor>& out);
};

}  // namespace app

#endif
