#include "HIKARI_ModelAsset.h"
#include <utility>

namespace HIKARI {

    const std::string& ModelAsset::GetName() const {
        return name_;
    }

    const std::string& ModelAsset::GetSourcePath() const {
        return sourcePath_;
    }

    ModelAsset::State ModelAsset::GetState() const {
        return state_;
    }

    void ModelAsset::SetName(std::string name) {
        name_ = std::move(name);
    }

    void ModelAsset::SetSourcePath(std::string path) {
        sourcePath_ = std::move(path);
    }

    void ModelAsset::SetState(State state) {
        state_ = state;
    }

} // namespace HIKARI
