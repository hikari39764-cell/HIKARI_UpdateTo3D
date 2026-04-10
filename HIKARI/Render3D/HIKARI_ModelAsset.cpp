#include "HIKARI_ModelAsset.h"
#include <utility>

namespace HIKARI {

    const std::string& ModelAsset::GetName() const { return name_; }
    const std::string& ModelAsset::GetSourcePath() const { return sourcePath_; }
    ModelAsset::State ModelAsset::GetState() const { return state_; }

    void ModelAsset::SetName(std::string name) { name_ = std::move(name); }
    void ModelAsset::SetSourcePath(std::string path) { sourcePath_ = std::move(path); }
    void ModelAsset::SetState(State state) { state_ = state; }

    Mesh* ModelAsset::GetMesh() { return mesh_.get(); }
    const Mesh* ModelAsset::GetMesh() const { return mesh_.get(); }

    Material* ModelAsset::GetMaterial() { return material_.get(); }
    const Material* ModelAsset::GetMaterial() const { return material_.get(); }

    void ModelAsset::SetMesh(std::unique_ptr<Mesh> mesh) { mesh_ = std::move(mesh); }
    void ModelAsset::SetMaterial(std::unique_ptr<Material> material) { material_ = std::move(material); }

} // namespace HIKARI
