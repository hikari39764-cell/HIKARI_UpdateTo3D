#pragma once

#include <functional>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {
    class Mesh;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::MESHRENDERER {

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
        MeshRendererDebugStats* stats = nullptr;

        std::function<Mesh*(const MeshPrimitive&)> getPrimitiveMesh;
        std::function<Mesh*(const MeshPrimitive&)> getSkinnedPrimitiveMesh;
        std::function<int(const ModelAsset&, const MaterialAsset*)> resolveBaseColorTexture;
        std::function<int(const ModelAsset&, const MaterialAsset*)> resolveNormalTexture;
        std::function<int(const ModelAsset&, const MaterialAsset*)> resolveEmissiveTexture;
        std::function<int(const ModelAsset&, const MaterialAsset*)> resolveMetallicRoughnessTexture;
        std::function<int(const ModelAsset&, const MaterialAsset*)> resolveOcclusionTexture;
        std::function<VFX::VariantKey(const DrawItem&, const MaterialAsset*)> resolvePrimitiveVariant;
        std::function<ID3D12PipelineState*(const VFX::VariantKey&, bool, bool)> getOrCreatePso;
    };

    bool DrawMeshItem(
        const MeshDrawContext& ctx,
        const DrawItem& item,
        size_t& objectIndex);

} // namespace HIKARI::MESHRENDERER
