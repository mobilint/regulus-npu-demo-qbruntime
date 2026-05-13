#include "gst_base_pipeline.h"

#include <stdint.h>

#include <functional>
#include <iostream>

#include "glib.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstutils.h"

namespace gst_wrapper {
BasePipeline::BasePipeline() {
    num_pipelines++;
    if (num_pipelines == 1) {
        gst_init(NULL, NULL);
        std::cout << "BasePipeline: init gstreamer.\n";
    }
}

BasePipeline::~BasePipeline() {
    /* Free resources */
    gst_object_unref(GST_OBJECT(pipeline));
    std::cout << "BasePipeline: unref pipeline.\n";

    num_pipelines--;
    if (num_pipelines == 0) {
        gst_deinit();
        std::cout << "BasePipeline: deinit gstreamer.\n";
    }
}

void BasePipeline::start() {
    /* Start pipeline */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

void BasePipeline::stop() {
    /* Stop pipeline */
    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void process_data_from_appsink(
    GstElement* appsink, std::function<void(uint8_t* data, unsigned long size)> fn) {
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK_CAST(appsink));
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo info;
        gst_buffer_map(buffer, &info, GST_MAP_READ);

        /* Process data from appsink here */
        fn(info.data, info.size);

        /* Free resources */
        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
    } else {
        std::cout << "pull sample from appsink failed.\n";
    }
}

void push_data_to_appsrc(GstElement* appsrc, uint8_t* data, unsigned long size) {
    /* Allocate a buffer */
    GstBuffer* buffer;
    buffer = gst_buffer_new_allocate(NULL, size, NULL);

    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);

    // Copy data to the buffer
    memcpy(map.data, data, size);
    gst_buffer_unmap(buffer, &map);

    gst_app_src_push_buffer(GST_APP_SRC_CAST(appsrc), buffer);
}
}  // namespace gst_wrapper
