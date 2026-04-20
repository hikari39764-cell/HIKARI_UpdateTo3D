#pragma once
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "Runtime/Components/HIKARI_IComponent.h"
#include "Authoring/Scene/HIKARI_SceneDocument.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    class GameObject {
    public:
        explicit GameObject(std::string name);

        const std::string& GetName() const;
        void SetName(const std::string& name);
        void SetDocumentId(SceneObjectId id);
        SceneObjectId GetDocumentId() const;

        Transform3D& Transform();
        const Transform3D& Transform() const;

        template<class T, class... Args>
        T* AddComponent(Args&&... args) {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* ptr = component.get();
            ptr->SetOwner(this);
            ptr->OnAttach();
            components_.push_back(std::move(component));
            return ptr;
        }

        template<class T>
        T* GetComponent() {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted = dynamic_cast<T*>(component.get())) {
                    return casted;
                }
            }
            return nullptr;
        }

        template<class T>
        const T* GetComponent() const {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted = dynamic_cast<const T*>(component.get())) {
                    return casted;
                }
            }
            return nullptr;
        }

        void Update(float dt);
        void Render();
        void RenderImGui();
        IComponent* AddComponentInstance(std::unique_ptr<IComponent> component);

        const std::vector<std::unique_ptr<IComponent>>& GetComponents() const;

    private:
        std::string name_;
        SceneObjectId documentId_{};
        Transform3D transform_{};
        std::vector<std::unique_ptr<IComponent>> components_;
        bool active_ = true;
    };

} // namespace HIKARI
