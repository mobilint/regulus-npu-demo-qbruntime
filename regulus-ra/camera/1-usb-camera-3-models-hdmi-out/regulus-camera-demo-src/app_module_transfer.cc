#include "app_module_transfer.h"

namespace app {
static inline cv::Mat resize_frame(cv::Mat frame, int npu_w, int npu_h) {
    int w = frame.cols;
    int h = frame.rows;

    cv::Mat resized_frame;

    int w_resize;
    int h_resize;
    if ((float)npu_w / npu_h > (float)w / h) {
        float w_ratio = (float)npu_w / w;

        w_resize = npu_w;
        h_resize = std::ceil(h * w_ratio);

        cv::resize(frame, resized_frame, cv::Size(w_resize, h_resize));
    } else {
        float h_ratio = (float)npu_h / h;

        w_resize = std::ceil(w * h_ratio);
        h_resize = npu_h;

        cv::resize(frame, resized_frame, cv::Size(w_resize, h_resize));
    }

    cv::Mat frame_out = resized_frame({(int)((w_resize - npu_w) / 2),
                                       (int)((h_resize - npu_h) / 2), npu_w, npu_h})
                            .clone();

    return frame_out;
}
std::thread module_transfer(transfer::Module_config&& conf, transfer::Module_in&& in,
                            transfer::Module_out&& out) {
    return std::thread([conf = std::move(conf), in = std::move(in),
                        out = std::move(out)]() {
        int64_t index = 0;
        while (conf.push_on) {
            if (conf.bc) conf.bc->start();
            InterThreadData data_in;
            auto qsc = in.buffer.get(data_in, index);
            if (qsc == Buffer::StatusCode::CLOSED) {
                std::cout << "buffer closed.\n";
                exit(1);
            }
            cv::Mat resized_frame = resize_frame(data_in.frame, conf.npu_w, conf.npu_h);
            if (conf.bc) conf.bc->end();

            out.queue.push(resized_frame);
        }
    });
}
}  // namespace app
