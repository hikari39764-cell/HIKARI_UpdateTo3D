#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Runtime/Core/HIKARI_GameObject.h"
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
        std::vector<std::unique_ptr<GameObject>>& GetObjects();

        template<class T, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                if (!object) {
                    continue;
                }

                if (T* component = object->GetComponent<T>()) {
                    fn(*object, *component);
                }
            }
        }

        template<class T1, class T2, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                if (!object) {
                    continue;
                }

                T1* component1 = object->GetComponent<T1>();
                T2* component2 = object->GetComponent<T2>();
                if (component1 && component2) {
                    fn(*object, *component1, *component2);
                }
            }
        }

    private:
        std::vector<std::unique_ptr<GameObject>> objects_;
    };

} // namespace HIKARI
