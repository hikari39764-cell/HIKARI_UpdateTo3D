#pragma once
#include <memory>
#include <string>
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

        const std::vector<std::unique_ptr<GameObject>>& GetObjects() const;

    private:
        std::vector<std::unique_ptr<GameObject>> objects_;
    };

} // namespace HIKARI
