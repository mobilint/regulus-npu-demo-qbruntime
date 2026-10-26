#include "gst_base_pipeline.h"

#include <stdint.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>

#include "glib.h"
#include "gst/allocators/gstdmabuf.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstutils.h"

namespace gst_wrapper {
BasePipeline::BasePipeline() {
    static std::once_flag init_flag;
    std::call_once(init_flag, [] {
        gst_init(NULL, NULL);
        std::cout << "BasePipeline: init gstreamer.\n";
    });
}

BasePipeline::~BasePipeline() {
    /* Free resources */
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
        std::cout << "BasePipeline: unref pipeline.\n";
    }
}

void BasePipeline::start() {
    /* Start pipeline */
    if (!pipeline) throw std::runtime_error("GStreamer pipeline is null");
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        throw std::runtime_error("GStreamer pipeline did not start");
}

void BasePipeline::stop() {
    /* Stop pipeline */
    if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
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

bool push_data_to_appsrc(GstElement* appsrc, uint8_t* data, unsigned long size,
                         uint64_t timestamp_ns, uint64_t sequence) {
    // Zero-copy: wrap the caller's existing buffer instead of allocating + memcpy. The
    // data is owned by the caller (plot's persistent display ring) and outlives the
    // buffer, so notify is NULL (gst never frees it). Removes the 6MB/frame memcpy that
    // dominated the display stage.
    GstBuffer* buffer = gst_buffer_new_wrapped_full(static_cast<GstMemoryFlags>(0), data,
                                                    size, 0, size, nullptr, nullptr);
    if (!buffer) return false;
    GST_BUFFER_PTS(buffer) = timestamp_ns;
    GST_BUFFER_OFFSET(buffer) = sequence;
    return gst_app_src_push_buffer(GST_APP_SRC_CAST(appsrc), buffer) == GST_FLOW_OK;
}

bool push_dmabuf_to_appsrc(GstElement* appsrc, int dmabuf_fd, unsigned long size,
                           uint64_t timestamp_ns, uint64_t sequence) {
    // Wrap the dmabuf fd as GstMemory (no data copy). The dmabuf allocator takes
    // ownership of the fd it is given and closes it when the memory is freed, so pass a
    // dup of the caller's fd.
    static GstAllocator* alloc = gst_dmabuf_allocator_new();
    if (!alloc) return false;
    int dfd = dup(dmabuf_fd);
    if (dfd < 0) return false;
    GstMemory* mem = gst_dmabuf_allocator_alloc(alloc, dfd, size);
    if (!mem) {
        close(dfd);
        return false;
    }
    GstBuffer* buffer = gst_buffer_new();
    if (!buffer) {
        gst_memory_unref(mem);
        return false;
    }
    gst_buffer_append_memory(buffer, mem);
    GST_BUFFER_PTS(buffer) = timestamp_ns;
    GST_BUFFER_OFFSET(buffer) = sequence;
    return gst_app_src_push_buffer(GST_APP_SRC_CAST(appsrc), buffer) == GST_FLOW_OK;
}
}  // namespace gst_wrapper
