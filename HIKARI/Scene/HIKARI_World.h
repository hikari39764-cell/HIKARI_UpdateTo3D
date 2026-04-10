#pragma once
#include <memory>
#include <string>
#include <vector>

namespace HIKARI {

    class GameObject;

    class World {
    public:
        GameObject* CreateObject(const std::string& name);
        void DestroyObject(GameObject* object);

        void Update(float dt);
        void Render();
        void RenderImGui();

        const std::vector<std::unique_ptr<GameObject>>& GetObjects() const;

    private:
        std::vector<std::unique_ptr<GameObject>> objects_;
    };

} // namespace HIKARI
