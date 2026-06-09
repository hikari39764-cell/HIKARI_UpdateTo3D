#include "Render3D/Shadow/HIKARI_ShadowPacketExecutor.h"

#include <algorithm>
#include <cstring>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

namespace HIKARI::SHADOW::PACKET {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr size_t kMaxShadowPacketObjects = 2048u;

        struct ExecutorState {
            bool initialized = false;
            ComPtr<ID3D12Resource> objectDataBuffer;
            ShadowPacketObjectData* objectDataMapped = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        };

        struct PreparedShadowPacket {
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packet = nullptr;
            const Mesh* mesh = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE textureSrv{};
            ShadowPacketObjectData objectData{};
            uint64_t psoKey = 0;
            uint64_t geometryKey = 0;
            uint64_t textureSetKey = 0;
        };

        ExecutorState g;

        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[static_cast<size_t>(materialIndex)];
        }

        bool IsPreparedBatchCompatible(
            const PreparedShadowPacket& first,
            const PreparedShadowPacket& candidate) {

            if (first.psoKey != candidate.psoKey ||
                first.geometryKey != candidate.geometryKey) {
                return false;
            }

            // Alpha Mask は同じ base texture を参照する。Opaque も同じ条件へ寄せて並びを安定させる。
            return
                first.textureSrv.ptr == candidate.textureSrv.ptr &&
                first.textureSetKey == candidate.textureSetKey;
        }

        bool PrepareShadowPacket(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            PreparedShadowPacket& out) {

            if (ctx.resolveStaticMesh == nullptr ||
                ctx.resolveBaseColorTexture == nullptr ||
                packet.model == nullptr ||
                !packet.hasDrawWorldMatrix ||
                packet.meshIndex >= packet.model->meshes.size()) {
                return false;
            }

            const MeshAsset& meshAsset = packet.model->meshes[packet.meshIndex];
            if (packet.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[packet.primitiveIndex];
            Mesh* mesh = ctx.resolveStaticMesh(primitive);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            const MaterialAsset* materialAsset = GetPrimitiveMaterial(*packet.model, primitive.materialIndex);
            RENDER3D::TextureResourceHandle textureResource =
                ctx.resolveBaseColorTexture(*packet.model, materialAsset);
            const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(textureResource);

            ShadowPacketObjectData objectData{};
            objectData.world = packet.drawWorldMatrix;
            objectData.alphaCutoff = materialAsset != nullptr ? materialAsset->alphaCutoff : 0.5f;
            if (materialAsset != nullptr && materialAsset->alphaMode == AlphaMode::Mask) {
                objectData.materialFlags |= MATERIAL_FEATURES::AlphaMask;
            }

            out = {};
            out.packet = &packet;
            out.mesh = mesh;
            out.textureSrv = textureSrv;
            out.objectData = objectData;
            out.psoKey = packet.key.psoKey;
            out.geometryKey = packet.key.geometryKey;
            out.textureSetKey = packet.key.textureSetKey;
            return true;
        }

        void CopyObjectData(const ShadowPacketObjectData& objectData, size_t objectIndex) {
            if (g.objectDataMapped == nullptr || objectIndex >= kMaxShadowPacketObjects) {
                return;
            }
            g.objectDataMapped[objectIndex] = objectData;
        }

        void BindBatchResources(
            const ShadowPacketExecutorContext& ctx,
            const PreparedShadowPacket& first,
            size_t objectIndex) {

            ctx.cmd->SetGraphicsRootSignature(ctx.staticRootSig);
            ctx.cmd->SetPipelineState(ctx.staticPso);
            ctx.cmd->SetGraphicsRootConstantBufferView(kShadowStaticRootParamCamera, ctx.cameraAddress);
            if (first.textureSrv.ptr != 0) {
                ctx.cmd->SetGraphicsRootDescriptorTable(kShadowStaticRootParamBaseColorTexture, first.textureSrv);
            }
            if (g.objectDataSrvGpu.ptr != 0) {
                ctx.cmd->SetGraphicsRootDescriptorTable(kShadowStaticRootParamObjectData, g.objectDataSrvGpu);
            }

            const uint32_t constants[2] = {
                static_cast<uint32_t>(objectIndex),
                1u
            };
            ctx.cmd->SetGraphicsRoot32BitConstants(
                kShadowStaticRootParamObjectDataControl,
                2,
                constants,
                0);
        }

        void BindMesh(ID3D12GraphicsCommandList* cmd, const Mesh& mesh) {
            D3D12_VERTEX_BUFFER_VIEW vb = mesh.GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh.GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
        }
    }

    bool InitializeShadowPacketExecutor(ID3D12Device* device) {
        if (g.initialized) {
            return true;
        }
        if (device == nullptr || SERVICES::gCtx.srvHeap == nullptr) {
            return false;
        }

        const UINT objectDataBytes = static_cast<UINT>(sizeof(ShadowPacketObjectData) * kMaxShadowPacketObjects);
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(objectDataBytes);
        if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(g.objectDataBuffer.GetAddressOf())))) {
            return false;
        }
        if (FAILED(g.objectDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&g.objectDataMapped)))) {
            return false;
        }
        GFX::SetD3D12Name(g.objectDataBuffer.Get(), L"Shadow Packet ObjectData Buffer");

        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT srvIndex =
            GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::ShadowObjectData);
        g.objectDataSrvCpu =
            GFX::DESCRIPTOR::CpuAt(SERVICES::gCtx.srvHeap, descriptorSize, srvIndex);
        g.objectDataSrvGpu =
            GFX::DESCRIPTOR::GpuAt(SERVICES::gCtx.srvHeap, descriptorSize, srvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = 0;
        srv.Buffer.NumElements = static_cast<UINT>(kMaxShadowPacketObjects);
        srv.Buffer.StructureByteStride = sizeof(ShadowPacketObjectData);
        srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(g.objectDataBuffer.Get(), &srv, g.objectDataSrvCpu);

        g.initialized = true;
        return true;
    }

    void ResetShadowPacketExecutor() {
        g.objectDataBuffer.Reset();
        g.objectDataMapped = nullptr;
        g.objectDataSrvCpu = {};
        g.objectDataSrvGpu = {};
        g.initialized = false;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetShadowPacketObjectDataSrv() {
        return g.objectDataSrvGpu;
    }

    void BindLegacyShadowObjectDataMode(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr) {
            return;
        }
        if (g.objectDataSrvGpu.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(kShadowStaticRootParamObjectData, g.objectDataSrvGpu);
        }
        const uint32_t constants[2] = { 0u, 0u };
        cmd->SetGraphicsRoot32BitConstants(
            kShadowStaticRootParamObjectDataControl,
            2,
            constants,
            0);
    }

    ShadowPacketDrawResult DrawShadowPacketCommands(
        const ShadowPacketExecutorContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands,
        size_t& objectIndex) {

        ShadowPacketDrawResult result{};
        if (!g.initialized ||
            ctx.cmd == nullptr ||
            ctx.staticRootSig == nullptr ||
            ctx.staticPso == nullptr ||
            ctx.cameraAddress == 0 ||
            g.objectDataMapped == nullptr ||
            g.objectDataSrvGpu.ptr == 0 ||
            packets == nullptr ||
            executablePacketIndices == nullptr ||
            executablePacketIndexCount == 0 ||
            commands.empty()) {
            return result;
        }

        for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command : commands) {
            if (command.packetCount == 0 || command.firstExecutableIndex >= executablePacketIndexCount) {
                continue;
            }

            ++result.commandCount;
            if (command.singlePacket) {
                ++result.singlePacketCommandCount;
            }
            result.maxCommandPacketCount =
                (std::max)(result.maxCommandPacketCount, static_cast<size_t>(command.packetCount));

            const size_t commandBegin = command.firstExecutableIndex;
            const size_t commandEnd = std::min(
                executablePacketIndexCount,
                commandBegin + static_cast<size_t>(command.packetCount));

            size_t executableIndex = commandBegin;
            while (executableIndex < commandEnd) {
                const uint32_t firstPacketIndex = executablePacketIndices[executableIndex];
                if (firstPacketIndex >= packetCount) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                PreparedShadowPacket first{};
                if (!PrepareShadowPacket(ctx, packets[firstPacketIndex], first)) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                const size_t batchObjectStart = objectIndex;
                size_t instanceCount = 0;
                size_t cursor = executableIndex;
                for (; cursor < commandEnd; ++cursor) {
                    const uint32_t packetIndex = executablePacketIndices[cursor];
                    if (packetIndex >= packetCount) {
                        break;
                    }

                    PreparedShadowPacket candidate{};
                    if (!PrepareShadowPacket(ctx, packets[packetIndex], candidate)) {
                        break;
                    }
                    if (!IsPreparedBatchCompatible(first, candidate) ||
                        objectIndex + instanceCount >= kMaxShadowPacketObjects) {
                        break;
                    }

                    CopyObjectData(candidate.objectData, objectIndex + instanceCount);
                    ++instanceCount;
                }

                if (instanceCount == 0) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                BindBatchResources(ctx, first, batchObjectStart);
                BindMesh(ctx.cmd, *first.mesh);
                ctx.cmd->DrawIndexedInstanced(
                    first.mesh->GetIndexCount(),
                    static_cast<UINT>(instanceCount),
                    0,
                    0,
                    0);

                objectIndex += instanceCount;
                result.submittedPacketCount += instanceCount;
                ++result.drawCallCount;
                result.maxInstanceCount = (std::max)(result.maxInstanceCount, instanceCount);
                if (instanceCount > 1) {
                    ++result.instancedDrawCount;
                    result.instancedPacketCount += instanceCount;
                }
                executableIndex = cursor;
            }
        }

        return result;
    }

} // namespace HIKARI::SHADOW::PACKET
