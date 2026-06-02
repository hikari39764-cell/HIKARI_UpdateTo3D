#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::RENDER3D::RUNTIME {

    constexpr uint32_t kInvalidRenderModelIndex = (std::numeric_limits<uint32_t>::max)();

    enum class RenderSubmeshSkinningMode {
        Static,
        Skinned,
    };

    struct RenderSubmeshRecord {
        uint32_t nodeIndex = kInvalidRenderModelIndex;
        uint32_t meshIndex = kInvalidRenderModelIndex;
        uint32_t primitiveIndex = kInvalidRenderModelIndex;
        uint32_t materialIndex = 0;

        Bounds localBounds{};

        RenderSubmeshSkinningMode skinningMode = RenderSubmeshSkinningMode::Static;
        int skinIndex = -1;

        bool castShadowDefault = true;
        bool receiveShadowDefault = true;
    };

    struct RenderModelNodeRecord {
        uint32_t nodeIndex = kInvalidRenderModelIndex;
        int parentIndex = -1;
        MATH::Mat4 localMatrix{};
        Bounds localBounds{};
        bool hasMesh = false;
        int meshIndex = -1;
        int skinIndex = -1;
    };

    struct RenderModelAsset {
        const ModelAsset* source = nullptr;
        std::string sourceName{};
        std::string sourcePath{};

        Bounds localBounds{};

        bool valid = false;
        bool hasSkinnedSubmeshes = false;
        bool hasStaticSubmeshes = false;

        std::vector<RenderModelNodeRecord> nodes{};
        std::vector<RenderSubmeshRecord> submeshes{};
    };

    bool BuildRenderModelAsset(
        const ModelAsset& source,
        RenderModelAsset& out,
        std::string* outMessage = nullptr);

} // namespace HIKARI::RENDER3D::RUNTIME
