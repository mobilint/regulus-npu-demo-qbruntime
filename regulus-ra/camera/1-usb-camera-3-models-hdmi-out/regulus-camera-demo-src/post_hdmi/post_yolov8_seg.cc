#include "post_yolov8_seg.h"

namespace mobilint::post_hdmi {

/*
        Apply softmax function to the input vector in-place
*/
template <typename FloatContainer>
void softmax_inplace(FloatContainer& con) {
    float sum = 0;
    for (auto v : con) {
        sum += exp(v);
    }
    for (auto& v : con) {
        v = exp(v) / sum;
    }
}

/*
        Unpad given crop by cropping according to the pads
*/
cv::Mat unpad_yolov8_seg(const cv::Mat& image, int xpad, int ypad) {
    // Get the image shape
    int rows = image.rows;
    int cols = image.cols;

    // Calculate the size of the cropped region
    int width = cols - 2 * xpad;
    int height = rows - 2 * ypad;

    // Create a cv::Rect object specifying the region to be cropped
    cv::Rect rect(xpad, ypad, width, height);

    // Select the desired rows and columns of the image
    cv::Mat roi = image(rect);

    // Copy the selected region to a new image
    cv::Mat cropped;
    roi.copyTo(cropped);

    return cropped;
}

/*
        Resize the image. For size parameters you can pass just
        image.size() or create new cv::Size(width, height).
*/
template <typename T>
cv::Mat interpolate(const cv::Mat& input, const cv::Size& size, int mode) {
    // Resize the input tensor using the specified interpolation mode
    cv::Mat output;
    cv::resize(input, output, size, 0, 0, mode);

    return output;
}

}  // namespace mobilint::post_hdmi

mobilint::post_hdmi::YOLOv8SegPostProcessor::YOLOv8SegPostProcessor()
    : YOLOv8PostProcessor() {
    m_nextra = 32;  // number of extra outputs (masks)
    mType = PostType::SEG;

    // Segmentation has extra detection head called proto
    m_proto_stride = 4;
    m_proto_h = m_imh / m_proto_stride;
    m_proto_w = m_imw / m_proto_stride;
}

mobilint::post_hdmi::YOLOv8SegPostProcessor::YOLOv8SegPostProcessor(
    int nc, int imh, int imw, float conf_thres, float iou_thres, bool verbose)
    : YOLOv8PostProcessor(nc, imh, imw, conf_thres, iou_thres, verbose) {
    m_nextra = 32;  // number of extra outputs (masks)
    mType = PostType::SEG;

    // Segmentation has extra detection head called proto
    m_proto_stride = 4;
    m_proto_h = m_imh / m_proto_stride;
    m_proto_w = m_imw / m_proto_stride;
}

void mobilint::post_hdmi::YOLOv8SegPostProcessor::decode_boxes(
    const std::vector<float>& npu_out, const std::vector<int>& grid, int stride, int idx,
    std::array<float, 4>& pred_box) {
    std::array<float, 4> box;
    std::array<float, 16> tmp;
    for (int j = 0; j < 4; j++) {
        for (int k = 0; k < 16; k++) tmp[k] = npu_out[idx * (m_nc + 64) + j * 16 + k];
        softmax_inplace(tmp);

        float box_value = 0;
        for (int k = 0; k < 16; k++) box_value += tmp[k] * k;
        box[j] = box_value;
    }

    float xmin = grid[idx * 2 + 0] - box[0] + 0.5;
    float ymin = grid[idx * 2 + 1] - box[1] + 0.5;
    float xmax = grid[idx * 2 + 0] + box[2] + 0.5;
    float ymax = grid[idx * 2 + 1] + box[3] + 0.5;

    float x = (xmin + xmax) / 2 * stride;
    float y = (ymin + ymax) / 2 * stride;
    float w = (xmax - xmin) * stride;
    float h = (ymax - ymin) * stride;

    pred_box = {x, y, w, h};
}

/*
        Access elements in output related to masks and decode them
*/
void mobilint::post_hdmi::YOLOv8SegPostProcessor::decode_extra(
    const std::vector<float>& extra, const std::vector<int>& grid, int stride, int idx,
    std::vector<float>& pred_extra) {
    pred_extra.clear();
    for (int i = 0; i < 32; i++) {
        pred_extra.push_back(extra[idx * 32 + i]);
    }
}

void mobilint::post_hdmi::YOLOv8SegPostProcessor::decode_conf_thres(
    const std::vector<float>& npu_out, const std::vector<int>& grid, int stride,
    std::vector<std::array<float, 4>>& pred_boxes, std::vector<float>& pred_conf,
    std::vector<int>& pred_label, std::vector<std::pair<float, int>>& pred_scores,
    const std::vector<float>& extra, std::vector<std::vector<float>>& pred_extra) {
    int grid_h = m_imh / stride;
    int grid_w = m_imw / stride;
    int tmp_no = npu_out.size() / (grid_h * grid_w);

    if (tmp_no != m_nc + 64)  // 64 = 16 * 4
        throw std::invalid_argument(
            "Number of outputs per grid should be 114, "
            "however post-processor received " +
            std::to_string(tmp_no));

#pragma omp parallel for num_threads(mOpenmpThreadCount) \
    shared(pred_boxes, pred_conf, pred_label, pred_scores, pred_extra)
    for (int i = 0; i < grid_h * grid_w; i++) {
        std::array<float, 4> pred_box = {-999, -999, -999, -999};
        std::vector<float> pred_extra_values;
        for (int j = 0; j < m_nc; j++) {
            float inverse_conf = npu_out[i * tmp_no + 64 + j];
            if (inverse_conf > m_inverse_conf_thres) {
                float conf = sigmoid(inverse_conf);
                if (pred_box[0] == -999) {  // decode box only once
                    decode_boxes(npu_out, grid, stride, i, pred_box);
                    decode_extra(extra, grid, stride, i, pred_extra_values);
                }

#pragma omp critical
                {
                    pred_conf.push_back(conf);
                    pred_label.push_back(j);
                    pred_boxes.push_back(pred_box);
                    pred_scores.push_back(std::make_pair(conf, pred_scores.size()));
                    pred_extra.push_back(pred_extra_values);
                }
            }
        }
    }
}

cv::Mat& mobilint::post_hdmi::YOLOv8SegPostProcessor::get_label_mask() {
    return label_masks;
}

cv::Mat& mobilint::post_hdmi::YOLOv8SegPostProcessor::get_final_mask() {
    return final_masks;
}

void mobilint::post_hdmi::YOLOv8SegPostProcessor::run_postprocess(
    const std::vector<std::vector<float>>& npu_outs) {
    double start = set_timer();

    if ((npu_outs.size() - 1) / 2 != m_nl) {  // 1 proto + 3 masks + 3 dets
        throw std::invalid_argument(
            "YOLOv8 Segmentation post-processing is "
            "expected to receive 7 NPU outputs, however received " +
            std::to_string(npu_outs.size()));
    }

    final_boxes.clear();
    final_scores.clear();
    final_labels.clear();
    final_extra.clear();

    std::vector<std::array<float, 4>> pred_boxes;
    std::vector<float> pred_conf;
    std::vector<int> pred_label;
    std::vector<std::pair<float, int>> pred_scores;
    std::vector<std::vector<float>> pred_extra;
    std::vector<float> proto = npu_outs[2];

    for (int i = 0; i < m_nl; i++) {
        int base = (i == 0) ? 0 : 2 * i + 1;
        decode_conf_thres(npu_outs[base], m_grids[i], m_strides[i], pred_boxes, pred_conf,
                          pred_label, pred_scores, npu_outs[base + 1], pred_extra);
    }

    xywh2xyxy(pred_boxes);

    nms(pred_boxes, pred_conf, pred_label, pred_scores, pred_extra, final_boxes,
        final_scores, final_labels, final_extra);

    process_mask(proto, final_extra, final_boxes, final_labels);
    double endd = set_timer();
    if (m_verbose) std::cout << "Real C++ Time        : " << endd - start << std::endl;
}

/*
        Downsample bounding box coordinates to proto resolution
*/
std::vector<std::array<float, 4>>
mobilint::post_hdmi::YOLOv8SegPostProcessor::downsample_boxes(
    std::vector<std::array<float, 4>> boxes) {
    for (int i = 0; i < boxes.size(); i++) {
        for (int j = 0; j < 4; j++) {
            boxes[i][j] /= m_proto_stride;
        }
    }

    return boxes;
}

/*
        Compute masks
*/
void mobilint::post_hdmi::YOLOv8SegPostProcessor::process_mask(
    const std::vector<float>& proto, const std::vector<std::vector<float>>& masks,
    const std::vector<std::array<float, 4>>& boxes, const std::vector<int>& labels) {
    auto boxes_down = downsample_boxes(boxes);
    int num_boxes = boxes.size();
    int matmul_col = 32;

    // Windows에서 build시 omp shared 영역에 멤버 변수가 있어 에러가 난다.
    // 이를 회피하고자 temp_ 지역 변수를 추가한다.
    // 로직을 보아, 지역 변수가 추가되어도 성능에 영향은 없을 듯 하다.
    cv::Mat temp_label_masks = cv::Mat::zeros(m_proto_h, m_proto_w, CV_32F);
    cv::Mat temp_final_masks = cv::Mat::zeros(m_proto_h, m_proto_w, CV_32F);

    for (int i = 0; i < num_boxes; i++) {
        // person만 탐지
        if (labels[i] != 0) {
            continue;
        }
        int x_min = std::max(int(boxes_down[i][0]), 0);
        int y_min = std::max(int(boxes_down[i][1]), 0);
        int x_max = std::min(int(boxes_down[i][2]), m_proto_w - 1);
        int y_max = std::min(int(boxes_down[i][3]), m_proto_h - 1);

#pragma omp parallel for num_threads(mOpenmpThreadCount)                 \
    shared(proto, masks, labels, x_min, y_min, x_max, y_max, matmul_col, \
               temp_label_masks, temp_final_masks)
        for (int h = int(y_min); h <= y_max; h++) {
            for (int w = int(x_min); w <= x_max; w++) {
                float temp = 0;
                int idx_proto = h * m_proto_w * matmul_col + w * matmul_col;
                for (int j = 0; j < matmul_col; j++) {
                    temp += masks[i][j] * proto[idx_proto + j];
                }
                auto temp_sig = sigmoid(temp);

                if (temp_final_masks.at<float>(h, w) < temp_sig) {
                    temp_label_masks.at<float>(h, w) = labels[i] + 1;  // 0 background
                    temp_final_masks.at<float>(h, w) = temp_sig;
                }
            }
        }
    }

    label_masks = interpolate<float>(temp_label_masks, cv::Size(m_imw, m_imh), 0);
    final_masks = interpolate<float>(temp_final_masks, cv::Size(m_imw, m_imh), 1);
}

/*
        Plot masks on image
*/
void mobilint::post_hdmi::YOLOv8SegPostProcessor::plot_masks(
    cv::Mat& im, cv::Mat& masks, cv::Mat& lbl_masks,
    const std::vector<std::array<float, 4>>& boxes) {
    float ratio, xpad, ypad;
    compute_ratio_pads(im, ratio, xpad, ypad);

    cv::Mat colored_masks(m_imh, m_imw, CV_8UC3);
    colored_masks.setTo(cv::Scalar(0, 0, 0));  // refresh image
    cv::Rect rect;
    for (int i = 0; i < boxes.size(); i++) {
        int x_min = std::max(int(boxes[i][0]), 0);
        int y_min = std::max(int(boxes[i][1]), 0);
        int x_max = std::min(int(boxes[i][2]), (int)m_imw - 1);
        int y_max = std::min(int(boxes[i][3]), (int)m_imh - 1);

        // box 그리는 부분
        rect.x = x_min;
        rect.y = y_min;
        rect.width = x_max - x_min;
        rect.height = y_max - y_min;

        std::array<int, 3> bgr_box = {56, 56, 255};
        cv::Scalar clr(bgr_box[0], bgr_box[1], bgr_box[2]);
        cv::rectangle(im, rect, clr, 2);
        //

        for (int h = int(y_min); h <= y_max; h++) {
            for (int w = int(x_min); w <= x_max; w++) {
                if (masks.at<float>(h, w) > 0.5) {
                    int idx = h * m_imw + w;
                    int cls = lbl_masks.at<float>(h, w) - 1;  // 0 background
                    std::array<int, 3> bgr = COLORS[cls % 20];
                    colored_masks.data[3 * idx + 0] = (uint8_t)bgr[0];
                    colored_masks.data[3 * idx + 1] = (uint8_t)bgr[1];
                    colored_masks.data[3 * idx + 2] = (uint8_t)bgr[2];
                }
            }
        }
    }

    colored_masks = unpad_yolov8_seg(colored_masks, xpad, ypad);
    colored_masks = interpolate<float>(colored_masks, im.size(), 1);
    cv::addWeighted(im, 0.9, colored_masks, 0.7, 0.0, im);
}

void mobilint::post_hdmi::YOLOv8SegPostProcessor::worker() {
    auto thres_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto title = std::to_string(thres_id) + " | Postprocessor Worker | ";

    print(title + "Start");
    while (!destroyed) {
        std::unique_lock<std::mutex> lk(mMutexIn);
        if (mQueueIn.empty()) {
            mCondIn.wait(lk, [this] { return !mQueueIn.empty() || destroyed; });
        }

        if (destroyed) {
            break;
        }

        auto k = mQueueIn.front();
        mQueueIn.pop();
        lk.unlock();

        auto start = set_timer();

        run_postprocess(k.npu_outs);
        k.boxes = get_result_box();
        k.scores = get_result_score();
        k.labels = get_result_label();
        k.extras = get_result_extra();

        auto lbl_masks = get_label_mask();
        auto masks = get_final_mask();

        // plot_boxes(k.im, k.boxes, k.scores, k.labels);
        plot_masks(k.im, masks, lbl_masks, k.boxes);

        auto end = set_timer();
        auto elapsed = std::to_string(end - start);

        print(title + "Postprocessing time: " + elapsed);
        print(title + "Number of detections " + std::to_string(k.boxes.size()));

        std::unique_lock<std::mutex> lk2(mMutexOut);
        mOut.push_back(k.id);
        lk2.unlock();

        std::unique_lock<std::mutex> lk_(mMutexOut);  // JUST IN CASE
        mCondOut.notify_all();
        lk_.unlock();  // JUST IN CASE
    }
    print(title + "Finish");
}
