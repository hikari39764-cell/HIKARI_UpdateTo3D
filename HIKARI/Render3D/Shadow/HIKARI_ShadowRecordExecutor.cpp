#include "Render3D/Shadow/HIKARI_ShadowRecordExecutor.h"

#include <algorithm>
#include <limits>

#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::SHADOW::RECORD {

    namespace {
        struct PreparedShadowRecord {
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* record = nullptr;
            const Mesh* mesh = nullptr;
            RENDER3D::RUNTIME::SurfaceDrawBatchKey batchKey{};
        };

        bool IsPreparedBatchCompatible(
            const PreparedShadowRecord& first,
            const PreparedShadowRecord& candidate) {

            return RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(first.batchKey, candidate.batchKey);
        }

        bool PrepareShadowRecord(
            const ShadowRecordExecutorContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            PreparedShadowRecord& out) {

            if (ctx.resolveStaticMesh == nullptr ||
                record.model == nullptr ||
                !record.hasDrawWorldMatrix ||
                record.meshIndex >= record.model->meshes.size()) {
                return false;
            }

            const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[record.primitiveIndex];
            Mesh* mesh = ctx.resolveStaticMesh(primitive);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            out = {};
            out.record = &record;
            out.mesh = mesh;
            out.batchKey = RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(
                RENDER3D::RUNTIME::SurfaceDrawCommandPass::Shadow,
                record.key);
            return true;
        }

        void BindMesh(ID3D12GraphicsCommandList* cmd, const Mesh& mesh) {
            D3D12_VERTEX_BUFFER_VIEW vb = mesh.GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh.GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
        }

        bool HasPreparedGpuSceneMaterial(
            const ShadowRecordExecutorContext& ctx,
            size_t gpuSceneInstanceIndex) {

            return
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                ctx.surfaceGpuSceneFrameBuffer->HasMaterialDataIndex(gpuSceneInstanceIndex);
        }

        bool HasRecordFrameResources(const ShadowRecordExecutorContext& ctx) {
            return
                ctx.fallbackBaseColorSrv.ptr != 0 &&
                ctx.materialDataSrv.ptr != 0 &&
                ctx.surfaceGpuSceneSrv.ptr != 0 &&
                ctx.texturePoolSrv.ptr != 0;
        }

        void BindRecordFrameResources(
            const ShadowRecordExecutorContext& ctx,
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
            const ShadowRecordExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            return
                ctx.indirectDrawBuffer != nullptr &&
                command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::Shadow &&
                command.backend == RENDER3D::RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                command.drawArgsValid &&
                command.recordCount > 0 &&
                command.firstGpuSceneInstanceIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                command.gpuSceneInstanceCount == command.recordCount &&
                command.drawArgs.instanceCount == command.gpuSceneInstanceCount;
        }

        bool CanUseShadowIndirectCommand(
            const ShadowRecordExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            return
                CanStartShadowIndirectCommandRange(ctx, command) &&
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                HasRecordFrameResources(ctx);
        }

        bool TryResolveShadowCommandRecordRange(
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            size_t executableRecordIndexCount,
            size_t& outBegin,
            size_t& outEnd) {

            const size_t begin = command.firstExecutableIndex;
            const size_t count = static_cast<size_t>(command.recordCount);
            if (count == 0 || begin >= executableRecordIndexCount) {
                return false;
            }

            const size_t end = begin + count;
            if (end < begin || end > executableRecordIndexCount) {
                return false;
            }

            outBegin = begin;
            outEnd = end;
            return true;
        }

        void RecordShadowCommandStats(
            ShadowRecordDrawResult& result,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            ++result.commandCount;
            if (command.singleRecord) {
                ++result.singleRecordCommandCount;
            }
            result.maxCommandRecordCount =
                (std::max)(result.maxCommandRecordCount, static_cast<size_t>(command.recordCount));
        }

        struct PreparedShadowIndirectCommand {
            PreparedShadowRecord first{};
            UINT64 argumentOffset = 0;
            size_t recordCount = 0;
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
            const ShadowRecordExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            PreparedShadowIndirectCommand& outPrepared) {

            outPrepared = {};
            if (!CanUseShadowIndirectCommand(ctx, command) ||
                records == nullptr ||
                executableRecordIndices == nullptr ||
                ctx.indirectDrawBuffer == nullptr ||
                !ctx.indirectDrawBuffer->TryGetArgumentBufferOffset(command, outPrepared.argumentOffset) ||
                !ctx.indirectDrawBuffer->HasDrawBinding(command)) {
                return false;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveShadowCommandRecordRange(
                command,
                executableRecordIndexCount,
                commandBegin,
                commandEnd)) {
                return false;
            }

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount ||
                !PrepareShadowRecord(ctx, records[firstRecordIndex], outPrepared.first) ||
                !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(outPrepared.first.batchKey, command.batchKey)) {
                return false;
            }

            size_t localIndex = 0;
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t recordIndex = executableRecordIndices[executableIndex];
                if (recordIndex >= recordCount) {
                    return false;
                }

                PreparedShadowRecord candidate{};
                if (!PrepareShadowRecord(ctx, records[recordIndex], candidate) ||
                    !IsPreparedBatchCompatible(outPrepared.first, candidate) ||
                    !HasPreparedGpuSceneMaterial(
                        ctx,
                        static_cast<size_t>(command.firstGpuSceneInstanceIndex) + localIndex)) {
                    return false;
                }
                ++localIndex;
            }

            outPrepared.recordCount = localIndex;
            return
                localIndex > 0 &&
                localIndex == static_cast<size_t>(command.drawArgs.instanceCount);
        }

        bool TryExecuteShadowIndirectCommandRange(
            const ShadowRecordExecutorContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
            size_t commandCount,
            size_t commandIndex,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            size_t& objectIndex,
            size_t& outNextCommandIndex,
            ShadowRecordDrawResult& result) {

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
            size_t preparedRecordCount = 0;
            size_t maxInstanceCount = 0;
            size_t instancedDrawCount = 0;
            size_t instancedRecordCount = 0;

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
                    records,
                    recordCount,
                    executableRecordIndices,
                    executableRecordIndexCount,
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
                preparedRecordCount += prepared.recordCount;
                maxInstanceCount = (std::max)(maxInstanceCount, prepared.recordCount);
                if (prepared.recordCount > 1) {
                    ++instancedDrawCount;
                    instancedRecordCount += prepared.recordCount;
                }
            }

            if (preparedCommandCount == 0) {
                return false;
            }

            BindRecordFrameResources(ctx, 0u, true);
            ctx.cmd->ExecuteIndirect(
                commandSignature,
                static_cast<UINT>(preparedCommandCount),
                argumentBuffer,
                firstArgumentOffset,
                nullptr,
                0);

            objectIndex += preparedRecordCount;
            result.submittedRecordCount += preparedRecordCount;
            result.drawCallCount += preparedCommandCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, maxInstanceCount);
            result.instancedDrawCount += instancedDrawCount;
            result.instancedRecordCount += instancedRecordCount;
            result.indirectDrawCount += preparedCommandCount;
            result.indirectRecordCount += preparedRecordCount;
            ++result.indirectBatchCount;
            result.indirectSavedSubmitCount += preparedCommandCount - 1;
            result.indirectMaxBatchCommandCount =
                (std::max)(result.indirectMaxBatchCommandCount, preparedCommandCount);

            outNextCommandIndex = commandIndex + preparedCommandCount;
            return true;
        }
    }

    bool InitializeShadowRecordExecutor(ID3D12Device* device) {
        return device != nullptr;
    }

    void ResetShadowRecordExecutor() {
    }

    bool PrepareShadowRecordIndirectDrawBindings(
        const ShadowRecordExecutorContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands) {

        if (ctx.indirectDrawBuffer == nullptr ||
            records == nullptr ||
            executableRecordIndices == nullptr ||
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
            if (!TryResolveShadowCommandRecordRange(
                command,
                executableRecordIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }
            (void)commandEnd;

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount) {
                continue;
            }

            PreparedShadowRecord prepared{};
            if (!PrepareShadowRecord(ctx, records[firstRecordIndex], prepared) ||
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

    ShadowRecordDrawResult DrawShadowRecordCommands(
        const ShadowRecordExecutorContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands,
        size_t& objectIndex) {

        ShadowRecordDrawResult result{};
        if (ctx.cmd == nullptr ||
            ctx.staticRootSig == nullptr ||
            ctx.staticPso == nullptr ||
            ctx.cameraAddress == 0 ||
            ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            !HasRecordFrameResources(ctx) ||
            records == nullptr ||
            executableRecordIndices == nullptr ||
            executableRecordIndexCount == 0 ||
            commands.empty()) {
            return result;
        }

        size_t commandIndex = 0;
        while (commandIndex < commands.size()) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (command.recordCount == 0 || command.firstExecutableIndex >= executableRecordIndexCount) {
                ++commandIndex;
                continue;
            }

            size_t nextCommandIndex = commandIndex + 1;
            if (TryExecuteShadowIndirectCommandRange(
                ctx,
                commands.data(),
                commands.size(),
                commandIndex,
                records,
                recordCount,
                executableRecordIndices,
                executableRecordIndexCount,
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
            if (!TryResolveShadowCommandRecordRange(
                command,
                executableRecordIndexCount,
                commandBegin,
                commandEnd)) {
                commandIndex = nextCommandIndex;
                continue;
            }

            size_t executableIndex = commandBegin;
            while (executableIndex < commandEnd) {
                const uint32_t firstRecordIndex = executableRecordIndices[executableIndex];
                if (firstRecordIndex >= recordCount) {
                    ++result.skippedRecordCount;
                    ++executableIndex;
                    continue;
                }

                PreparedShadowRecord first{};
                if (!PrepareShadowRecord(ctx, records[firstRecordIndex], first) ||
                    !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(first.batchKey, command.batchKey)) {
                    ++result.skippedRecordCount;
                    ++executableIndex;
                    continue;
                }

                const size_t localBase = executableIndex - commandBegin;
                const size_t gpuSceneBase =
                    static_cast<size_t>(command.firstGpuSceneInstanceIndex) + localBase;
                size_t instanceCount = 0;
                size_t cursor = executableIndex;
                for (; cursor < commandEnd; ++cursor) {
                    const uint32_t recordIndex = executableRecordIndices[cursor];
                    if (recordIndex >= recordCount) {
                        break;
                    }

                    PreparedShadowRecord candidate{};
                    if (!PrepareShadowRecord(ctx, records[recordIndex], candidate) ||
                        !IsPreparedBatchCompatible(first, candidate) ||
                        !HasPreparedGpuSceneMaterial(ctx, gpuSceneBase + instanceCount)) {
                        break;
                    }

                    ++instanceCount;
                }

                if (instanceCount == 0) {
                    ++result.skippedRecordCount;
                    ++executableIndex;
                    continue;
                }

                BindRecordFrameResources(ctx, static_cast<uint32_t>(gpuSceneBase), true);
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
                result.submittedRecordCount += instanceCount;
                ++result.drawCallCount;
                result.maxInstanceCount = (std::max)(result.maxInstanceCount, instanceCount);
                if (instanceCount > 1) {
                    ++result.instancedDrawCount;
                    result.instancedRecordCount += instanceCount;
                }
                executableIndex = cursor;
            }

            commandIndex = nextCommandIndex;
        }

        return result;
    }

} // namespace HIKARI::SHADOW::RECORD
