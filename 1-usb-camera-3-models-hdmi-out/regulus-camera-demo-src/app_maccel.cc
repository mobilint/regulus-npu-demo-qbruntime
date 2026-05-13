#include "app_maccel.h"

#include <qbruntime/status_code.h>
#include <qbruntime/type.h>

#include <iostream>
#include <memory>
#include <vector>

namespace app {
mobilint::StatusCode MaccelRepositionBuffer::reposition(float* pre_data) {
    return model->repositionInputs({pre_data}, buf);
}

MaccelMultiModel::MaccelMultiModel(const std::vector<std::string>& model_paths) {
    StatusCode sc;
    acc = Accelerator::create(sc);
    if (!sc) {
        std::cout << "Accelerator::create() failed.\n";
        exit(1);
    }

    for (const auto& model_path : model_paths) {
        std::unique_ptr<Model> model = Model::create(model_path, mc, sc);

        BufferInfo buffer_info = model->getInputBufferInfo()[0];
        const int h = buffer_info.original_height;
        const int w = buffer_info.original_width;
        const int c = buffer_info.original_channel;

        std::cout << "launch model created from " << model->getModelPath()
                  << ", hwc: " << h << " " << w << " " << c << "\n";
        model->launch(*acc);

        std::vector<mobilint::Buffer> reposition_buffer = model->acquireInputBuffer();

        _models.emplace_back(_Model{std::move(model), reposition_buffer});
    }
}

std::vector<Model*> MaccelMultiModel::get_models() {
    std::vector<Model*> result;
    for (const auto& _model : _models) {
        result.push_back(_model.model.get());
    }
    return result;
}

MaccelRepositionBuffer MaccelMultiModel::get_reposition_buffer() {
    MaccelRepositionBuffer shared_buf;
    if (_models.size() == 0) {
        std::cout << "No models available to get reposition buffer.\n";
        exit(1);
    }

    shared_buf.model = _models[0].model.get();
    shared_buf.buf = _models[0].reposition_buffer;

    return shared_buf;
}

std::vector<MaccelRepositionBuffer> MaccelMultiModel::get_reposition_buffers() {
    std::vector<MaccelRepositionBuffer> result;
    for (const auto& _model : _models) {
        MaccelRepositionBuffer buf;
        buf.model = _model.model.get();
        buf.buf = _model.reposition_buffer;

        result.push_back(buf);
    }

    return result;
}

MaccelMultiModel::~MaccelMultiModel() {
    for (const auto& _model : _models) {
        _model.model->dispose();
    }
}
}  // namespace app
