#include "HIKARI_GameObject.h"

#include "HIKARI_World.h"

namespace HIKARI {

    GameObject::GameObject(std::string name)
        : name_(std::move(name)) {
    }

    const std::string& GameObject::GetName() const {
        return name_;
    }

    void GameObject::SetName(const std::string& name) {
        name_ = name;
        MarkRenderStateDirty();
    }

    void GameObject::SetDocumentId(SceneObjectId id) {
        const uint64_t oldStableId = GetRenderStableId();
        documentId_ = id;
        if (ownerWorld_ != nullptr && oldStableId != GetRenderStableId()) {
            ownerWorld_->MarkRenderObjectRemoved(oldStableId);
        }
        MarkRenderStateDirty();
    }

    SceneObjectId GameObject::GetDocumentId() const {
        return documentId_;
    }

    Transform3D& GameObject::Transform() {
        MarkRenderStateDirty();
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

    IComponent* GameObject::AddComponentInstance(std::unique_ptr<IComponent> component) {
        if (!component) {
            return nullptr;
        }

        IComponent* ptr = component.get();
        ptr->SetOwner(this);
        ptr->OnAttach();
        components_.push_back(std::move(component));
        MarkRenderStateDirty();
        return ptr;
    }

    const std::vector<std::unique_ptr<IComponent>>& GameObject::GetComponents() const {
        return components_;
    }

    void GameObject::MarkRenderStateDirty() {
        ++renderStateRevision_;
        if (!renderStateDirty_) {
            renderStateDirty_ = true;
        }
        if (ownerWorld_ != nullptr) {
            ownerWorld_->MarkRenderObjectDirty(this);
        }
    }

    void GameObject::ClearRenderStateDirty() {
        renderStateDirty_ = false;
    }

    bool GameObject::IsRenderStateDirty() const {
        return renderStateDirty_;
    }

    uint64_t GameObject::GetRenderStateRevision() const {
        return renderStateRevision_;
    }

    uint64_t GameObject::GetRenderStableId() const {
        if (documentId_.value != 0) {
            return documentId_.value;
        }
        return reinterpret_cast<uint64_t>(this);
    }

    void GameObject::SetOwnerWorld(World* world) {
        ownerWorld_ = world;
        if (ownerWorld_ != nullptr) {
            ownerWorld_->MarkRenderObjectDirty(this);
        }
    }

} // namespace HIKARI
