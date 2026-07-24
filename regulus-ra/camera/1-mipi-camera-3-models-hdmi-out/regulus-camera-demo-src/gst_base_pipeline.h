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

    GstElement* pipeline;

    void start();
    void stop();

private:
    int num_pipelines = 0;
};

void process_data_from_appsink(GstElement* appsink,
                               std::function<void(uint8_t* data, unsigned long size)> fn);

void push_data_to_appsrc(GstElement* appsrc, uint8_t* data, unsigned long size);
}  // namespace gst_wrapper

#endif
