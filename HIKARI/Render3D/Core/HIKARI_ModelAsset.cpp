#include "Render3D/HIKARI_ModelAsset.h"

#include <utility>

namespace HIKARI {

    const std::string& ModelAsset::GetName() const {
        return id.value;
    }

    const std::string& ModelAsset::GetSourcePath() const {
        return sourcePath;
    }

    ModelAsset::State ModelAsset::GetState() const {
        return state;
    }

    void ModelAsset::SetName(std::string name) {
        id.value = std::move(name);
    }

    void ModelAsset::SetSourcePath(std::string path) {
        sourcePath = std::move(path);
    }

    void ModelAsset::SetState(State stateValue) {
        state = stateValue;
    }

    bool ModelAsset::HasSkeleton() const {
        return !skins.empty();
    }

    bool ModelAsset::HasAnimations() const {
        return !animations.empty();
    }

    const AnimationClip* ModelAsset::FindAnimationClip(std::string_view name) const {
        for (const AnimationClip& clip : animations) {
            if (clip.name == name) {
                return &clip;
            }
        }
        return nullptr;
    }

    Mesh* ModelAsset::GetMesh() { return mesh_.get(); }
    const Mesh* ModelAsset::GetMesh() const { return mesh_.get(); }
    Material* ModelAsset::GetMaterial() { return material_.get(); }
    const Material* ModelAsset::GetMaterial() const { return material_.get(); }
    void ModelAsset::SetMesh(std::unique_ptr<Mesh> mesh) { mesh_ = std::move(mesh); }
    void ModelAsset::SetMaterial(std::unique_ptr<Material> material) { material_ = std::move(material); }

} // namespace HIKARI
