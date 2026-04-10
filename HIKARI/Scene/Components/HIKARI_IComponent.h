#pragma once

namespace HIKARI {

    class GameObject;

    class IComponent {
    public:
        virtual ~IComponent() = default;

        void SetOwner(GameObject* owner) { owner_ = owner; }
        GameObject* GetOwner() { return owner_; }
        const GameObject* GetOwner() const { return owner_; }

        virtual const char* GetTypeName() const = 0;
        virtual void OnAttach() {}
        virtual void Update(float dt) { (void)dt; }
        virtual void Render() {}
        virtual void RenderImGui() {}

    private:
        GameObject* owner_ = nullptr;
    };

} // namespace HIKARI
