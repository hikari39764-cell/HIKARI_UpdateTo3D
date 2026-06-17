#include "HIKARI_World.h"
#include <algorithm>
#include "HIKARI_GameObject.h"
namespace HIKARI {



    GameObject* World::CreateObject(const std::string& name) {
        auto object = std::make_unique<GameObject>(name);
        GameObject* ptr = object.get();
        ptr->SetOwnerWorld(this);
        objects_.push_back(std::move(object));
        MarkRenderObjectDirty(ptr);
        return ptr;
    }

    World::~World() = default;

    void World::DestroyObject(GameObject* object) {
        if (object != nullptr) {
            MarkRenderObjectRemoved(object->GetRenderStableId());
            renderDirtyObjects_.erase(
                std::remove(renderDirtyObjects_.begin(), renderDirtyObjects_.end(), object),
                renderDirtyObjects_.end());
            object->SetOwnerWorld(nullptr);
        }
        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(), [object](const std::unique_ptr<GameObject>& candidate) {
                return candidate.get() == object;
                }),
            objects_.end());
    }

    void World::Update(float dt) {
        for (const auto& object : objects_) {
            object->Update(dt);
        }
    }

    void World::Render() {
        for (const auto& object : objects_) {
            object->Render();
        }
    }

    void World::RenderImGui() {
        for (const auto& object : objects_) {
            object->RenderImGui();
        }
    }

    void World::Clear() {
        for (const auto& object : objects_) {
            if (object) {
                MarkRenderObjectRemoved(object->GetRenderStableId());
                object->SetOwnerWorld(nullptr);
            }
        }
        objects_.clear();
        renderDirtyObjects_.clear();
    }

    const std::vector<std::unique_ptr<GameObject>>& World::GetObjects() const {
        return objects_;
    }

    const std::vector<GameObject*>& World::GetRenderDirtyObjects() const {
        return renderDirtyObjects_;
    }

    const std::vector<uint64_t>& World::GetRemovedRenderObjectIds() const {
        return removedRenderObjectIds_;
    }

    void World::MarkRenderObjectDirty(GameObject* object) {
        if (object == nullptr) {
            return;
        }
        if (std::find(renderDirtyObjects_.begin(), renderDirtyObjects_.end(), object) !=
            renderDirtyObjects_.end()) {
            return;
        }
        renderDirtyObjects_.push_back(object);
    }

    void World::MarkRenderObjectRemoved(uint64_t renderObjectId) {
        if (renderObjectId == 0) {
            return;
        }
        if (std::find(removedRenderObjectIds_.begin(), removedRenderObjectIds_.end(), renderObjectId) !=
            removedRenderObjectIds_.end()) {
            return;
        }
        removedRenderObjectIds_.push_back(renderObjectId);
    }

    void World::AcknowledgeRenderDirtyObjects() {
        for (GameObject* object : renderDirtyObjects_) {
            if (object != nullptr) {
                object->ClearRenderStateDirty();
            }
        }
        renderDirtyObjects_.clear();
        removedRenderObjectIds_.clear();
    }

    void World::AcknowledgeAllRenderObjects() {
        for (const auto& object : objects_) {
            if (object) {
                object->ClearRenderStateDirty();
            }
        }
        renderDirtyObjects_.clear();
        removedRenderObjectIds_.clear();
    }

} // namespace HIKARI
