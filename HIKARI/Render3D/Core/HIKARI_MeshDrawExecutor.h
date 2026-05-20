#pragma once

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"

namespace HIKARI {
    class Mesh;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::MESHRENDERER {

    class MeshMaterialResolver;
    class MeshPrimitiveCache;
    struct MeshPipelineStore;

    struct MeshDrawServices {
        ID3D12Device* device = nullptr;
        MeshPrimitiveCache* primitiveCache = nullptr;
        MeshMaterialResolver* materialResolver = nullptr;
        MeshPipelineStore* pipelines = nullptr;
        MeshRendererDebugStats* stats = nullptr;
    };

    struct MeshDrawContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        ID3D12RootSignature* staticRootSig = nullptr;
        ID3D12RootSignature* skinnedRootSig = nullptr;
        ID3D12Resource* objectCB = nullptr;
        ID3D12Resource* jointPaletteCB = nullptr;
        ObjectCB* objectMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
        MeshBindingContext binding{};
        MeshMaterialFillContext materialFill{};
        MeshDrawServices services{};
    };

    bool DrawMeshItem(
        const MeshDrawContext& ctx,
        const DrawItem& item,
        size_t& objectIndex);

} // namespace HIKARI::MESHRENDERER
