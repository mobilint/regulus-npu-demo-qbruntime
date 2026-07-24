#ifndef YOLOV8_SEG_POSTPROCESSOR_HDMI
#define YOLOV8_SEG_POSTPROCESSOR_HDMI

#include "post_yolov8.h"

namespace mobilint::post_hdmi {
class YOLOv8SegPostProcessor : public YOLOv8PostProcessor {
public:
    YOLOv8SegPostProcessor();
    YOLOv8SegPostProcessor(int nc, int imh, int imw, float conf_thres, float iou_thres,
                           bool verbose);

    void run_postprocess(const std::vector<std::vector<float>>& npu_outs);
    void decode_boxes(const std::vector<float>& npu_out, const std::vector<int>& grid,
                      int stride, int idx, std::array<float, 4>& pred_box);
    void decode_extra(const std::vector<float>& extra, const std::vector<int>& grid,
                      int stride, int idx, std::vector<float>& pred_extra);
    void decode_conf_thres(const std::vector<float>& npu_out,
                           const std::vector<int>& grid, int stride,
                           std::vector<std::array<float, 4>>& pred_boxes,
                           std::vector<float>& pred_conf, std::vector<int>& pred_label,
                           std::vector<std::pair<float, int>>& pred_scores,
                           const std::vector<float>& extra,
                           std::vector<std::vector<float>>& pred_extra);
    std::vector<std::array<float, 4>> downsample_boxes(
        std::vector<std::array<float, 4>> boxes);
    void process_mask(const std::vector<float>& proto,
                      const std::vector<std::vector<float>>& masks,
                      const std::vector<std::array<float, 4>>& boxes,
                      const std::vector<int>& labels);
    cv::Mat& get_label_mask();
    cv::Mat& get_final_mask();
    void plot_masks(cv::Mat& im, cv::Mat& masks, cv::Mat& label_masks,
                    const std::vector<std::array<float, 4>>& boxes);

    void worker();

protected:
    int m_proto_stride;
    int m_proto_h;
    int m_proto_w;
    cv::Mat label_masks;
    cv::Mat final_masks;
};
}  // namespace mobilint::post_hdmi

#endif
