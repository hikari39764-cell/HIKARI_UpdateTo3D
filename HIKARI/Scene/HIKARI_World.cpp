#include "HIKARI_World.h"
#include <algorithm>
#include "HIKARI_GameObject.h"

namespace HIKARI {

    GameObject* World::CreateObject(const std::string& name) {
        auto object = std::make_unique<GameObject>(name);
        GameObject* ptr = object.get();
        objects_.push_back(std::move(object));
        return ptr;
    }

    void World::DestroyObject(GameObject* object) {
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

    const std::vector<std::unique_ptr<GameObject>>& World::GetObjects() const {
        return objects_;
    }

} // namespace HIKARI
