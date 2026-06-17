#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "Components/HIKARI_IComponent.h"
#include "HIKARI_SceneDocument.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    class World;

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
            MarkRenderStateDirty();
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

        void MarkRenderStateDirty();
        void ClearRenderStateDirty();
        bool IsRenderStateDirty() const;
        uint64_t GetRenderStateRevision() const;
        uint64_t GetRenderStableId() const;

    private:
        friend class World;

        void SetOwnerWorld(World* world);

        std::string name_;
        SceneObjectId documentId_{};
        Transform3D transform_{};
        std::vector<std::unique_ptr<IComponent>> components_;
        World* ownerWorld_ = nullptr;
        uint64_t renderStateRevision_ = 1;
        bool renderStateDirty_ = true;
        bool active_ = true;
    };

} // namespace HIKARI
