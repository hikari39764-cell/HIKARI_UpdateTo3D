#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GeometryBackendPolicy.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrameContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenProducer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenSceneSource;
    class IGpuDrivenProducer;

    struct GpuDrivenSceneResidency {
        bool resident = false;
        uint64_t layoutVersion = 0;
        uint64_t sourceVersion = 0;
        size_t instanceCount = 0;

        void Reset();
    };

    struct GpuDrivenSceneUploadStats {
        size_t sourceInstanceCount = 0;
        bool sceneResident = false;
        bool reusedResidentFrame = false;
        bool patchedDirtyRanges = false;
        bool uploadedFullScene = false;
        std::array<uint32_t, kGpuDrivenPassCount> passInstanceCounts{};
        SurfaceGpuSceneFrameBufferStats bufferStats{};
    };

    struct GpuDrivenSceneUploadDesc {
        GpuDrivenSceneResidency* residency = nullptr;
        bool allowDirtyRangePatching = true;
    };

    struct GpuDrivenCommandFrameDesc {
        ID3D12GraphicsCommandList* commandList = nullptr;
        bool resetTraditionalIndirectBuffer = true;
        bool publishCommandBuffers = true;
    };

    struct GpuDrivenCommandFrameStats {
        bool commandFramePublished = false;
        SurfaceIndirectDrawBufferStats surfaceIndirectStats{};
    };

    class GpuDrivenLayer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* staticRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);

        void Attach(
            SurfaceGpuSceneFrameBuffer* sceneBuffer,
            SurfaceIndirectDrawBuffer* indirectDrawBuffer,
            IGpuDrivenProducer* producer);

        void ResetFrame();
        bool BeginFrame(const GpuDrivenSceneSource* source);
        const GpuDrivenSceneUploadStats& UploadSceneFrame(
            const GpuDrivenSceneUploadDesc& desc);
        void UploadSurfaceGpuSceneFrame(
            uint32_t instanceCount,
            uint32_t opaqueBaseIndex,
            uint32_t depthAwareBaseIndex,
            uint32_t transparentBaseIndex,
            uint32_t shadowBaseIndex,
            bool sceneResident);
        void PrepareSurfaceGpuSceneMaterialFrame();
        void ImportProducerOutput(
            const GpuDrivenProducerFrameOutput& output);
        void SetBackendAvailability(
            const GpuDrivenBackendAvailability& availability);
        const GpuDrivenCommandFrameStats& BuildCommandFrame(
            const GpuDrivenCommandFrameDesc& desc);
        void BuildCommandBuffers();

        bool IsBackendConsumable(
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;
        bool IsPassGpuReady(GpuDrivenPassKind pass) const;
        GeometryBackendExecutionPlan GetPassExecutionPlan(
            GpuDrivenPassKind pass) const;
        GeometryBackendContext BuildGeometryBackendContext(
            ID3D12GraphicsCommandList* commandList,
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;

        const GpuDrivenPassExecutionState& GetPassExecutionState(
            GpuDrivenPassKind pass) const;
        SurfaceGpuSceneFrameBuffer* GetSceneBuffer() const;
        SurfaceIndirectDrawBuffer* GetIndirectDrawBuffer() const;
        IGpuDrivenProducer* GetProducer() const;
        const GpuDrivenFrameContext& GetFrameContext() const;
        const GpuDrivenDrawCommandStream& GetDrawCommandStream() const;
        const GpuDrivenSceneUploadStats& GetSceneUploadStats() const;
        const GpuDrivenCommandFrameStats& GetCommandFrameStats() const;

    private:
        SurfaceGpuSceneFrameBuffer* sceneBuffer_ = nullptr;
        SurfaceIndirectDrawBuffer* indirectDrawBuffer_ = nullptr;
        IGpuDrivenProducer* producer_ = nullptr;
        const GpuDrivenSceneSource* frameSource_ = nullptr;
        GpuDrivenFrameContext frameContext_{};
        GpuDrivenSceneUploadStats sceneUploadStats_{};
        GpuDrivenCommandFrameStats commandFrameStats_{};

        void InitializePassExecutionStates(
            const GpuDrivenSceneSource* source);
        void RefreshPassExecutionStates();
        void RebuildDrawCommandStream();
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
