#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetRecord.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI::EDITOR {

    struct ModelCollisionPreviewNode {
        int32_t nodeIndex = -1;
        int32_t meshIndex = -1;
        std::string name{};
        Bounds bounds{};
    };

    class ModelCollisionPreviewScene {
    public:
        bool Load(
            const AssetRecord& record,
            const std::filesystem::path& projectRoot,
            std::string& outMessage);
        void Clear();

        bool IsReady() const noexcept;
        const ModelAsset* GetModel() const noexcept;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource*
            GetSceneSource() const noexcept;
        const std::vector<ModelCollisionPreviewNode>&
            GetSourceNodes() const noexcept;
        const ModelCollisionPreviewNode* FindSourceNode(
            int32_t nodeIndex) const noexcept;
        uint64_t GetRevision() const noexcept;

    private:
        void RebuildSceneSource();

        ModelAsset model_{};
        RENDER3D::RUNTIME::RenderModelAsset renderModel_{};
        RENDER3D::RUNTIME::SceneRenderCache sceneCache_{};
        RENDER3D::GPUDRIVEN::GpuSceneRegistry gpuSceneRegistry_{};
        std::vector<ModelCollisionPreviewNode> sourceNodes_{};
        std::string clusteredGeometryPath_{};
        uint64_t revision_ = 0u;
        bool ready_ = false;
    };

} // namespace HIKARI::EDITOR
