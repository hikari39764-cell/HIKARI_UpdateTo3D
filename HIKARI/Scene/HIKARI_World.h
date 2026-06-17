#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "HIKARI_GameObject.h"
namespace HIKARI {


    class World {
    public:
        World() = default;
        ~World();

        GameObject* CreateObject(const std::string& name);
        void DestroyObject(GameObject* object);

        void Update(float dt);
        void Render();
        void RenderImGui();
        void Clear();

        const std::vector<std::unique_ptr<GameObject>>& GetObjects() const;
        const std::vector<GameObject*>& GetRenderDirtyObjects() const;
        const std::vector<uint64_t>& GetRemovedRenderObjectIds() const;
        void MarkRenderObjectDirty(GameObject* object);
        void MarkRenderObjectRemoved(uint64_t renderObjectId);
        void AcknowledgeRenderDirtyObjects();
        void AcknowledgeAllRenderObjects();

        template<class T, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                if (auto* component = object->GetComponent<T>()) {
                    std::forward<Fn>(fn)(*object, *component);
                }
            }
        }

        template<class T1, class T2, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                auto* c1 = object->GetComponent<T1>();
                auto* c2 = object->GetComponent<T2>();
                if (c1 && c2) {
                    std::forward<Fn>(fn)(*object, *c1, *c2);
                }
            }
        }

    private:
        std::vector<std::unique_ptr<GameObject>> objects_;
        std::vector<GameObject*> renderDirtyObjects_{};
        std::vector<uint64_t> removedRenderObjectIds_{};
    };

} // namespace HIKARI
