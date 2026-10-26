#ifndef BASE_PIPELINE_H
#define BASE_PIPELINE_H

#include <stdint.h>

#include <functional>

#include "gst/gstelement.h"

namespace gst_wrapper {
class BasePipeline {
   public:
    BasePipeline();
    ~BasePipeline();

    GstElement* pipeline = nullptr;

    void start();
    void stop();
};

void process_data_from_appsink(GstElement* appsink,
                               std::function<void(uint8_t* data, unsigned long size)> fn);

bool push_data_to_appsrc(GstElement* appsrc, uint8_t* data, unsigned long size,
                         uint64_t timestamp_ns, uint64_t sequence);

// Push a dma-heap(CMA) buffer by its dmabuf fd (zero-copy). The buffer is wrapped as
// dmabuf GstMemory so kmssink imports and scans it out with no copy. The fd is dup'd
// internally, so the caller keeps ownership of the original.
bool push_dmabuf_to_appsrc(GstElement* appsrc, int dmabuf_fd, unsigned long size,
                           uint64_t timestamp_ns, uint64_t sequence);
}  // namespace gst_wrapper

#endif
