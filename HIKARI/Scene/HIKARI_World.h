#pragma once
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
    };

} // namespace HIKARI
