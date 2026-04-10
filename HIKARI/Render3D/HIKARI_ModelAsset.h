#pragma once
#include <memory>
#include <string>
#include "HIKARI_Material.h"
#include "HIKARI_Mesh.h"

namespace HIKARI {

    class ModelAsset {
    public:
        enum class State {
            Unloaded,
            Loaded,
            Failed
        };

        const std::string& GetName() const;
        const std::string& GetSourcePath() const;
        State GetState() const;

        void SetName(std::string name);
        void SetSourcePath(std::string path);
        void SetState(State state);

        Mesh* GetMesh();
        const Mesh* GetMesh() const;

        Material* GetMaterial();
        const Material* GetMaterial() const;

        void SetMesh(std::unique_ptr<Mesh> mesh);
        void SetMaterial(std::unique_ptr<Material> material);

    private:
        std::string name_;
        std::string sourcePath_;
        State state_ = State::Unloaded;

        std::unique_ptr<Mesh> mesh_;
        std::unique_ptr<Material> material_;
    };

} // namespace HIKARI
