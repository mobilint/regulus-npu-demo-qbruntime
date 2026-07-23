#ifndef APP_MACCEL_H_
#define APP_MACCEL_H_

#include <qbruntime/qbruntime.h>
#include <qbruntime/type.h>

#include <memory>
#include <vector>

using mobilint::Accelerator;
using mobilint::BufferInfo;
using mobilint::Cluster;
using mobilint::Core;
using mobilint::Model;
using mobilint::ModelConfig;
using mobilint::StatusCode;

namespace app {
class MaccelRepositionBuffer {
public:
    std::vector<mobilint::Buffer> buf;

    mobilint::StatusCode reposition(float* pre_data);

private:
    Model* model;

    friend class MaccelMultiModel;
};

class MaccelMultiModel {
    struct _Model {
        std::unique_ptr<Model> model;
        std::vector<mobilint::Buffer> reposition_buffer;
    };

public:
    MaccelMultiModel(const std::vector<std::string>& model_paths);
    ~MaccelMultiModel();

    std::unique_ptr<Accelerator> acc;
    ModelConfig mc;
    std::vector<_Model> _models;

    std::vector<Model*> get_models();
    MaccelRepositionBuffer get_reposition_buffer();
    std::vector<MaccelRepositionBuffer> get_reposition_buffers();
};
}  // namespace app

#endif
