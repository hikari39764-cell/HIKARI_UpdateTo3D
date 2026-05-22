#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"

#include <algorithm>
#include <cstring>

#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Core/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[materialIndex];
        }

        void CopyObjectCB(const MeshDrawContext& ctx, const ObjectCB& obj, size_t objectIndex) {
            constexpr UINT kObjectStride = AlignConstantBufferSize(sizeof(ObjectCB));
            uint8_t* dst = reinterpret_cast<uint8_t*>(ctx.objectMapped) + static_cast<size_t>(kObjectStride) * objectIndex;
            std::memcpy(dst, &obj, sizeof(ObjectCB));
        }

        D3D12_GPU_VIRTUAL_ADDRESS ObjectAddress(const MeshDrawContext& ctx, size_t objectIndex) {
            constexpr UINT kObjectStride = AlignConstantBufferSize(sizeof(ObjectCB));
            return ctx.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * objectIndex;
        }

        D3D12_GPU_VIRTUAL_ADDRESS JointPaletteAddress(const MeshDrawContext& ctx, size_t objectIndex) {
            constexpr UINT kJointPaletteStride = AlignConstantBufferSize(sizeof(JointPaletteCB));
            return ctx.jointPaletteCB->GetGPUVirtualAddress() + static_cast<UINT64>(kJointPaletteStride) * objectIndex;
        }

        void BindPerDrawCommon(
            const MeshDrawContext& ctx,
            ID3D12RootSignature* rootSig,
            D3D12_GPU_VIRTUAL_ADDRESS objectAddress) {
            BindFrameCommonResources(
                ctx.binding,
                rootSig,
                ctx.cameraAddress,
                ctx.lightAddress,
                ctx.shadowAddress,
                ctx.skyEnvironmentAddress);
            BindObjectConstantBuffer(ctx.binding, objectAddress);
        }

        void DrawPrimitiveWithDebugMode(
            const MeshDrawContext& ctx,
            const Mesh& mesh,
            const VFX::VariantKey& variant,
            bool drawingSkinned,
            MeshRenderDebugMode mode) {
            if (ctx.cmd == nullptr ||
                ctx.services.pipelines == nullptr ||
                ctx.services.device == nullptr ||
                ctx.services.stats == nullptr) {
                return;
            }

            const bool drawSolid = mode != MeshRenderDebugMode::WireOnly;
            const bool drawWire = mode == MeshRenderDebugMode::WireOnly ||
                mode == MeshRenderDebugMode::WireOverlay;

            auto drawPrimitive = [&](bool wireframe) {
                ID3D12PipelineState* pso = GetOrCreateVariantPso(
                    *ctx.services.pipelines,
                    ctx.services.device,
                    *ctx.services.stats,
                    variant,
                    drawingSkinned,
                    wireframe);
                if (pso == nullptr) {
                    return;
                }
                ctx.cmd->SetPipelineState(pso);
                ctx.cmd->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
                if (drawingSkinned) {
                    ++ctx.services.stats->skinnedGpuDrawCount;
                }
                if (wireframe) {
                    ++ctx.services.stats->wireGpuDrawCount;
                }
            };

            if (drawSolid) {
                drawPrimitive(false);
            }
            if (drawWire) {
                drawPrimitive(true);
            }
        }

        bool DrawStructuredMeshItem(
            const MeshDrawContext& ctx,
            const DrawItem& item,
            size_t& objectIndex) {
            const MATH::Mat4 world = item.transform.GetWorldMatrix();
            const MATH::Mat4 normalMatrix = BuildNormalMatrix(item.transform);

            for (const MeshAsset& meshAsset : item.asset->meshes) {
                for (const MeshPrimitive& primitive : meshAsset.primitives) {
                    if (objectIndex >= kMaxObjectCount) {
                        break;
                    }

                    const bool shouldDrawSkinned = !item.jointPalette.empty() && !primitive.skinnedVertices.empty();
                    bool drawingSkinned = false;
                    MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
                    Mesh* mesh = shouldDrawSkinned && primitiveCache != nullptr
                        ? primitiveCache->GetOrCreateSkinned(ctx.services.device, primitive, ctx.services.stats)
                        : (primitiveCache != nullptr ? primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats) : nullptr);
                    drawingSkinned = shouldDrawSkinned && mesh != nullptr && mesh->IsValid();
                    if (mesh == nullptr || !mesh->IsValid()) {
                        if (shouldDrawSkinned && ctx.services.stats != nullptr) {
                            ++ctx.services.stats->skinnedFallbackCount;
                        }
                        mesh = primitiveCache != nullptr
                            ? primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats)
                            : nullptr;
                        if (mesh == nullptr || !mesh->IsValid()) {
                            continue;
                        }
                    }

                    const MaterialAsset* materialAsset = GetPrimitiveMaterial(*item.asset, primitive.materialIndex);
                    ResolvedMaterialTextures textures{};
                    if (ctx.services.materialResolver != nullptr) {
                        textures = ctx.services.materialResolver->Resolve(*item.asset, materialAsset, ctx.services.stats);
                    } else {
                        textures.baseColor = ctx.binding.fallbackTextureHandle;
                        textures.normal = ctx.binding.fallbackNormalTextureHandle;
                        textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
                        textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
                        textures.occlusion = ctx.binding.fallbackTextureHandle;
                    }

                    ObjectCB obj{};
                    obj.world = world;
                    obj.normalMatrix = normalMatrix;
                    FillMaterialValues(
                        obj,
                        materialAsset,
                        textures.normal,
                        textures.emissive,
                        textures.metallicRoughness,
                        textures.occlusion,
                        ctx.materialFill);
                    obj.hasBaseColorTexture = (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
                    obj.receiveShadow = item.receiveShadow ? 1u : 0u;
                    FillFxValues(obj, item);
                    CopyObjectCB(ctx, obj, objectIndex);

                    const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
                    BindPerDrawCommon(ctx, drawingSkinned ? ctx.skinnedRootSig : ctx.staticRootSig, objectAddress);

                    const VFX::VariantKey primitiveVariant = ResolvePrimitiveVariant(item, materialAsset);

                    if (drawingSkinned) {
                        const size_t uploadedJointCount = UploadJointPalette(ctx.jointPaletteMapped, objectIndex, item.jointPalette);
                        if (ctx.cmd != nullptr && ctx.jointPaletteCB != nullptr) {
                            ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::JointPalette, JointPaletteAddress(ctx, objectIndex));
                        }
                        if (ctx.services.stats != nullptr) {
                            ctx.services.stats->uploadedJointCount += uploadedJointCount;
                            ctx.services.stats->maxJointCount = std::max(ctx.services.stats->maxJointCount, item.jointPalette.size());
                            ctx.services.stats->lastSkinnedVertexCount = primitive.skinnedVertices.size();
                        }
                    }

                    BindMaterialTextureSet(ctx.binding, {
                        textures.baseColor,
                        textures.normal,
                        textures.emissive,
                        textures.metallicRoughness,
                        textures.occlusion
                    });
                    BindSkyCube(ctx.binding);
                    BindSceneDepth(ctx.binding);
                    BindSceneColor(ctx.binding);
                    BindIblResources(ctx.binding);

                    D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
                    D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
                    ctx.cmd->IASetVertexBuffers(0, 1, &vb);
                    ctx.cmd->IASetIndexBuffer(&ib);

                    DrawPrimitiveWithDebugMode(ctx, *mesh, primitiveVariant, drawingSkinned, item.renderDebugMode);

                    ++objectIndex;
                }
            }

            return true;
        }

        bool DrawLegacyMeshItem(
            const MeshDrawContext& ctx,
            const DrawItem& item,
            size_t& objectIndex) {
            if (!item.asset->GetMesh() || !item.asset->GetMesh()->IsValid()) {
                return true;
            }

            ObjectCB obj{};
            obj.world = item.transform.GetWorldMatrix();
            obj.normalMatrix = BuildNormalMatrix(item.transform);
            FillMaterialValues(
                obj,
                nullptr,
                ctx.binding.fallbackNormalTextureHandle,
                ctx.materialFill.fallbackBlackTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.materialFill);
            obj.receiveShadow = item.receiveShadow ? 1u : 0u;
            if (const Material* material = item.asset->GetMaterial()) {
                obj.baseColor = material->GetBaseColor();
                obj.hasBaseColorTexture = material->HasBaseColorTexture() ? 1u : 0u;
            } else {
                obj.baseColor = { 1, 1, 1, 1 };
                obj.hasBaseColorTexture = 0u;
            }
            FillFxValues(obj, item);
            CopyObjectCB(ctx, obj, objectIndex);

            const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
            BindPerDrawCommon(ctx, ctx.staticRootSig, objectAddress);

            int textureHandle = ctx.binding.fallbackTextureHandle;
            if (const Material* material = item.asset->GetMaterial()) {
                if (material->HasBaseColorTexture()) {
                    textureHandle = material->GetBaseColorTextureHandle();
                }
            }

            BindMaterialTextureSet(ctx.binding, {
                textureHandle,
                ctx.binding.fallbackNormalTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.binding.fallbackTextureHandle
            });
            BindSkyCube(ctx.binding);
            BindSceneDepth(ctx.binding);
            BindSceneColor(ctx.binding);
            BindIblResources(ctx.binding);

            const Mesh* mesh = item.asset->GetMesh();
            D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
            ctx.cmd->IASetVertexBuffers(0, 1, &vb);
            ctx.cmd->IASetIndexBuffer(&ib);

            DrawPrimitiveWithDebugMode(ctx, *mesh, item.variant, false, item.renderDebugMode);

            ++objectIndex;
            return true;
        }
    }

    bool DrawMeshItem(
        const MeshDrawContext& ctx,
        const DrawItem& item,
        size_t& objectIndex) {
        if (objectIndex >= kMaxObjectCount || item.asset == nullptr) {
            return false;
        }
        if (ctx.cmd == nullptr || ctx.objectMapped == nullptr || ctx.objectCB == nullptr) {
            return false;
        }

        const bool hasStructuredGltfMeshes = !item.asset->meshes.empty();
        if (hasStructuredGltfMeshes) {
            return DrawStructuredMeshItem(ctx, item, objectIndex);
        }

        return DrawLegacyMeshItem(ctx, item, objectIndex);
    }

} // namespace HIKARI::MESHRENDERER
