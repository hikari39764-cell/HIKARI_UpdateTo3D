#include "HIKARI_GameObject.h"

namespace HIKARI {

    GameObject::GameObject(std::string name)
        : name_(std::move(name)) {
    }

    const std::string& GameObject::GetName() const {
        return name_;
    }

    void GameObject::SetName(const std::string& name) {
        name_ = name;
    }

    Transform3D& GameObject::Transform() {
        return transform_;
    }

    const Transform3D& GameObject::Transform() const {
        return transform_;
    }

    void GameObject::Update(float dt) {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->Update(dt);
        }
    }

    void GameObject::Render() {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->Render();
        }
    }

    void GameObject::RenderImGui() {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->RenderImGui();
        }
    }

    const std::vector<std::unique_ptr<IComponent>>& GameObject::GetComponents() const {
        return components_;
    }

} // namespace HIKARI
