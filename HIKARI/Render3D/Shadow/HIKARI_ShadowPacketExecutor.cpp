#include "Render3D/Shadow/HIKARI_ShadowPacketExecutor.h"

#include <algorithm>
#include <limits>

#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

namespace HIKARI::SHADOW::PACKET {

    namespace {
        struct PreparedShadowPacket {
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packet = nullptr;
            const Mesh* mesh = nullptr;
            RENDER3D::RUNTIME::SurfaceDrawBatchKey batchKey{};
        };

        bool IsPreparedBatchCompatible(
            const PreparedShadowPacket& first,
            const PreparedShadowPacket& candidate) {

            return RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(first.batchKey, candidate.batchKey);
        }

        bool PrepareShadowPacket(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            PreparedShadowPacket& out) {

            if (ctx.resolveStaticMesh == nullptr ||
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

            out = {};
            out.packet = &packet;
            out.mesh = mesh;
            out.batchKey = RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(
                RENDER3D::RUNTIME::SurfaceDrawCommandPass::Shadow,
                packet.key);
            return true;
        }

        void BindMesh(ID3D12GraphicsCommandList* cmd, const Mesh& mesh) {
            D3D12_VERTEX_BUFFER_VIEW vb = mesh.GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh.GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
        }

        bool HasPreparedGpuSceneMaterial(
            const ShadowPacketExecutorContext& ctx,
            size_t gpuSceneInstanceIndex) {

            return
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                ctx.surfaceGpuSceneFrameBuffer->HasMaterialDataIndex(gpuSceneInstanceIndex);
        }

        bool HasPacketFrameResources(const ShadowPacketExecutorContext& ctx) {
            return
                ctx.fallbackBaseColorSrv.ptr != 0 &&
                ctx.materialDataSrv.ptr != 0 &&
                ctx.surfaceGpuSceneSrv.ptr != 0 &&
                ctx.texturePoolSrv.ptr != 0;
        }

        void BindPacketFrameResources(
            const ShadowPacketExecutorContext& ctx,
            uint32_t surfaceGpuSceneBaseIndex,
            bool useSurfaceGpuScene) {

            ctx.cmd->SetGraphicsRootSignature(ctx.staticRootSig);
            ctx.cmd->SetPipelineState(ctx.staticPso);
            ctx.cmd->SetGraphicsRootConstantBufferView(
                kShadowStaticRootParamCamera,
                ctx.cameraAddress);
            ctx.cmd->SetGraphicsRootDescriptorTable(
                kShadowStaticRootParamBaseColorTexture,
                ctx.fallbackBaseColorSrv);
            ctx.cmd->SetGraphicsRootDescriptorTable(
                kShadowStaticRootParamMaterialData,
                ctx.materialDataSrv);
            ctx.cmd->SetGraphicsRootDescriptorTable(
                kShadowStaticRootParamSurfaceGpuScene,
                ctx.surfaceGpuSceneSrv);
            ctx.cmd->SetGraphicsRootDescriptorTable(
                kShadowStaticRootParamTexturePool,
                ctx.texturePoolSrv);

            const uint32_t constants[RENDER3D::GPUDRIVEN::kSurfaceIndirectRootConstantCount] = {
                surfaceGpuSceneBaseIndex,
                useSurfaceGpuScene ? 1u : 0u,
                0u,
                0u,
            };
            ctx.cmd->SetGraphicsRoot32BitConstants(
                kShadowStaticRootParamSurfaceGpuSceneControl,
                RENDER3D::GPUDRIVEN::kSurfaceIndirectRootConstantCount,
                constants,
                0);
            ctx.cmd->SetGraphicsRoot32BitConstant(
                kShadowStaticRootParamMaterialIndex,
                0u,
                0);
        }

        bool CanStartShadowIndirectCommandRange(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            return
                ctx.indirectDrawBuffer != nullptr &&
                command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::Shadow &&
                command.backend == RENDER3D::RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                command.drawArgsValid &&
                command.packetCount > 0 &&
                command.firstGpuSceneInstanceIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                command.gpuSceneInstanceCount == command.packetCount &&
                command.drawArgs.instanceCount == command.gpuSceneInstanceCount;
        }

        bool CanUseShadowIndirectCommand(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            return
                CanStartShadowIndirectCommandRange(ctx, command) &&
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                HasPacketFrameResources(ctx);
        }

        bool TryResolveShadowCommandPacketRange(
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            size_t executablePacketIndexCount,
            size_t& outBegin,
            size_t& outEnd) {

            const size_t begin = command.firstExecutableIndex;
            const size_t count = static_cast<size_t>(command.packetCount);
            if (count == 0 || begin >= executablePacketIndexCount) {
                return false;
            }

            const size_t end = begin + count;
            if (end < begin || end > executablePacketIndexCount) {
                return false;
            }

            outBegin = begin;
            outEnd = end;
            return true;
        }

        void RecordShadowCommandStats(
            ShadowPacketDrawResult& result,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            ++result.commandCount;
            if (command.singlePacket) {
                ++result.singlePacketCommandCount;
            }
            result.maxCommandPacketCount =
                (std::max)(result.maxCommandPacketCount, static_cast<size_t>(command.packetCount));
        }

        struct PreparedShadowIndirectCommand {
            PreparedShadowPacket first{};
            UINT64 argumentOffset = 0;
            size_t packetCount = 0;
        };

        bool IsSameShadowIndirectRootBatch(
            const PreparedShadowIndirectCommand& first,
            const PreparedShadowIndirectCommand& candidate) {

            return
                first.first.batchKey.pass == candidate.first.batchKey.pass &&
                first.first.batchKey.psoKey == candidate.first.batchKey.psoKey &&
                first.first.batchKey.transparent == candidate.first.batchKey.transparent;
        }

        bool TryPrepareShadowIndirectCommand(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            PreparedShadowIndirectCommand& outPrepared) {

            outPrepared = {};
            if (!CanUseShadowIndirectCommand(ctx, command) ||
                packets == nullptr ||
                executablePacketIndices == nullptr ||
                ctx.indirectDrawBuffer == nullptr ||
                !ctx.indirectDrawBuffer->TryGetArgumentBufferOffset(command, outPrepared.argumentOffset) ||
                !ctx.indirectDrawBuffer->HasDrawBinding(command)) {
                return false;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveShadowCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                return false;
            }

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount ||
                !PrepareShadowPacket(ctx, packets[firstPacketIndex], outPrepared.first) ||
                !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(outPrepared.first.batchKey, command.batchKey)) {
                return false;
            }

            size_t localIndex = 0;
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t packetIndex = executablePacketIndices[executableIndex];
                if (packetIndex >= packetCount) {
                    return false;
                }

                PreparedShadowPacket candidate{};
                if (!PrepareShadowPacket(ctx, packets[packetIndex], candidate) ||
                    !IsPreparedBatchCompatible(outPrepared.first, candidate) ||
                    !HasPreparedGpuSceneMaterial(
                        ctx,
                        static_cast<size_t>(command.firstGpuSceneInstanceIndex) + localIndex)) {
                    return false;
                }
                ++localIndex;
            }

            outPrepared.packetCount = localIndex;
            return
                localIndex > 0 &&
                localIndex == static_cast<size_t>(command.drawArgs.instanceCount);
        }

        bool TryExecuteShadowIndirectCommandRange(
            const ShadowPacketExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
            size_t commandCount,
            size_t commandIndex,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            size_t& objectIndex,
            size_t& outNextCommandIndex,
            ShadowPacketDrawResult& result) {

            if (ctx.cmd == nullptr ||
                ctx.staticRootSig == nullptr ||
                ctx.staticPso == nullptr ||
                ctx.cameraAddress == 0 ||
                ctx.indirectDrawBuffer == nullptr ||
                commands == nullptr ||
                commandIndex >= commandCount ||
                !CanUseShadowIndirectCommand(ctx, commands[commandIndex])) {
                return false;
            }

            ID3D12Resource* argumentBuffer = ctx.indirectDrawBuffer->GetArgumentBuffer();
            ID3D12CommandSignature* commandSignature = ctx.indirectDrawBuffer->GetCommandSignature();
            if (argumentBuffer == nullptr || commandSignature == nullptr) {
                return false;
            }

            const UINT64 argumentStride =
                static_cast<UINT64>(sizeof(RENDER3D::GPUDRIVEN::SurfaceIndirectDrawArgument));
            const size_t maxIndirectCommandCount =
                static_cast<size_t>((std::numeric_limits<UINT>::max)());

            PreparedShadowIndirectCommand firstPrepared{};
            UINT64 firstArgumentOffset = 0;
            size_t preparedCommandCount = 0;
            size_t preparedPacketCount = 0;
            size_t maxInstanceCount = 0;
            size_t instancedDrawCount = 0;
            size_t instancedPacketCount = 0;

            for (size_t scanIndex = commandIndex;
                scanIndex < commandCount && preparedCommandCount < maxIndirectCommandCount;
                ++scanIndex) {

                const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[scanIndex];
                if (!CanUseShadowIndirectCommand(ctx, command)) {
                    break;
                }

                PreparedShadowIndirectCommand prepared{};
                if (!TryPrepareShadowIndirectCommand(
                    ctx,
                    command,
                    packets,
                    packetCount,
                    executablePacketIndices,
                    executablePacketIndexCount,
                    prepared)) {
                    break;
                }

                if (preparedCommandCount == 0) {
                    firstPrepared = prepared;
                    firstArgumentOffset = prepared.argumentOffset;
                } else {
                    const UINT64 expectedOffset =
                        firstArgumentOffset + argumentStride * static_cast<UINT64>(preparedCommandCount);
                    if (prepared.argumentOffset != expectedOffset ||
                        !IsSameShadowIndirectRootBatch(firstPrepared, prepared)) {
                        break;
                    }
                }

                RecordShadowCommandStats(result, command);
                ++preparedCommandCount;
                preparedPacketCount += prepared.packetCount;
                maxInstanceCount = (std::max)(maxInstanceCount, prepared.packetCount);
                if (prepared.packetCount > 1) {
                    ++instancedDrawCount;
                    instancedPacketCount += prepared.packetCount;
                }
            }

            if (preparedCommandCount == 0) {
                return false;
            }

            BindPacketFrameResources(ctx, 0u, true);
            ctx.cmd->ExecuteIndirect(
                commandSignature,
                static_cast<UINT>(preparedCommandCount),
                argumentBuffer,
                firstArgumentOffset,
                nullptr,
                0);

            objectIndex += preparedPacketCount;
            result.submittedPacketCount += preparedPacketCount;
            result.drawCallCount += preparedCommandCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, maxInstanceCount);
            result.instancedDrawCount += instancedDrawCount;
            result.instancedPacketCount += instancedPacketCount;
            result.indirectDrawCount += preparedCommandCount;
            result.indirectPacketCount += preparedPacketCount;
            ++result.indirectBatchCount;
            result.indirectSavedSubmitCount += preparedCommandCount - 1;
            result.indirectMaxBatchCommandCount =
                (std::max)(result.indirectMaxBatchCommandCount, preparedCommandCount);

            outNextCommandIndex = commandIndex + preparedCommandCount;
            return true;
        }
    }

    bool InitializeShadowPacketExecutor(ID3D12Device* device) {
        return device != nullptr;
    }

    void ResetShadowPacketExecutor() {
    }

    bool PrepareShadowPacketIndirectDrawBindings(
        const ShadowPacketExecutorContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands) {

        if (ctx.indirectDrawBuffer == nullptr ||
            packets == nullptr ||
            executablePacketIndices == nullptr ||
            commands.empty()) {
            return false;
        }

        bool patchedAny = false;
        for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command : commands) {
            if (!CanStartShadowIndirectCommandRange(ctx, command)) {
                continue;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveShadowCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }
            (void)commandEnd;

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount) {
                continue;
            }

            PreparedShadowPacket prepared{};
            if (!PrepareShadowPacket(ctx, packets[firstPacketIndex], prepared) ||
                prepared.mesh == nullptr ||
                !prepared.mesh->IsValid()) {
                continue;
            }

            patchedAny =
                ctx.indirectDrawBuffer->PatchDrawBinding(
                    command,
                    prepared.mesh->GetVBView(),
                    prepared.mesh->GetIBView()) ||
                patchedAny;
        }

        return patchedAny;
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
        if (ctx.cmd == nullptr ||
            ctx.staticRootSig == nullptr ||
            ctx.staticPso == nullptr ||
            ctx.cameraAddress == 0 ||
            ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            !HasPacketFrameResources(ctx) ||
            packets == nullptr ||
            executablePacketIndices == nullptr ||
            executablePacketIndexCount == 0 ||
            commands.empty()) {
            return result;
        }

        size_t commandIndex = 0;
        while (commandIndex < commands.size()) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (command.packetCount == 0 || command.firstExecutableIndex >= executablePacketIndexCount) {
                ++commandIndex;
                continue;
            }

            size_t nextCommandIndex = commandIndex + 1;
            if (TryExecuteShadowIndirectCommandRange(
                ctx,
                commands.data(),
                commands.size(),
                commandIndex,
                packets,
                packetCount,
                executablePacketIndices,
                executablePacketIndexCount,
                objectIndex,
                nextCommandIndex,
                result)) {
                commandIndex = nextCommandIndex;
                continue;
            }
            if (CanUseShadowIndirectCommand(ctx, command)) {
                ++result.indirectFallbackCommandCount;
            }

            RecordShadowCommandStats(result, command);

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveShadowCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                commandIndex = nextCommandIndex;
                continue;
            }

            size_t executableIndex = commandBegin;
            while (executableIndex < commandEnd) {
                const uint32_t firstPacketIndex = executablePacketIndices[executableIndex];
                if (firstPacketIndex >= packetCount) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                PreparedShadowPacket first{};
                if (!PrepareShadowPacket(ctx, packets[firstPacketIndex], first) ||
                    !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(first.batchKey, command.batchKey)) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                const size_t localBase = executableIndex - commandBegin;
                const size_t gpuSceneBase =
                    static_cast<size_t>(command.firstGpuSceneInstanceIndex) + localBase;
                size_t instanceCount = 0;
                size_t cursor = executableIndex;
                for (; cursor < commandEnd; ++cursor) {
                    const uint32_t packetIndex = executablePacketIndices[cursor];
                    if (packetIndex >= packetCount) {
                        break;
                    }

                    PreparedShadowPacket candidate{};
                    if (!PrepareShadowPacket(ctx, packets[packetIndex], candidate) ||
                        !IsPreparedBatchCompatible(first, candidate) ||
                        !HasPreparedGpuSceneMaterial(ctx, gpuSceneBase + instanceCount)) {
                        break;
                    }

                    ++instanceCount;
                }

                if (instanceCount == 0) {
                    ++result.skippedPacketCount;
                    ++executableIndex;
                    continue;
                }

                BindPacketFrameResources(ctx, static_cast<uint32_t>(gpuSceneBase), true);
                BindMesh(ctx.cmd, *first.mesh);
                const uint32_t indexCount = command.drawArgsValid
                    ? command.drawArgs.indexCountPerInstance
                    : first.mesh->GetIndexCount();
                ctx.cmd->DrawIndexedInstanced(
                    indexCount,
                    static_cast<UINT>(instanceCount),
                    command.drawArgs.startIndexLocation,
                    command.drawArgs.baseVertexLocation,
                    command.drawArgs.startInstanceLocation);

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

            commandIndex = nextCommandIndex;
        }

        return result;
    }

} // namespace HIKARI::SHADOW::PACKET
