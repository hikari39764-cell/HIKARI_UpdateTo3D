#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        uint32_t ToFlag(SurfaceGpuSceneInstanceFlags flag) {
            return static_cast<uint32_t>(flag);
        }

        uint32_t ToResourceFlag(SurfaceGpuSceneResourceFlags flag) {
            return static_cast<uint32_t>(flag);
        }

        MATH::Vec4 BuildBoundsCenterRadius(const Bounds& bounds) {
            const MATH::Vec3 center{
                (bounds.min.x + bounds.max.x) * 0.5f,
                (bounds.min.y + bounds.max.y) * 0.5f,
                (bounds.min.z + bounds.max.z) * 0.5f,
            };
            const MATH::Vec3 extent{
                bounds.max.x - center.x,
                bounds.max.y - center.y,
                bounds.max.z - center.z,
            };
            const float radius = std::sqrt(
                extent.x * extent.x +
                extent.y * extent.y +
                extent.z * extent.z);
            return { center.x, center.y, center.z, radius };
        }

        MATH::Mat4 BuildNormalMatrixFromWorld(const MATH::Mat4& world) {
            const float a00 = world.m[0][0];
            const float a01 = world.m[1][0];
            const float a02 = world.m[2][0];
            const float a10 = world.m[0][1];
            const float a11 = world.m[1][1];
            const float a12 = world.m[2][1];
            const float a20 = world.m[0][2];
            const float a21 = world.m[1][2];
            const float a22 = world.m[2][2];

            const float det =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-6f) {
                return MATH::Mat4::Identity();
            }

            const float invDet = 1.0f / det;
            MATH::Mat4 normalMatrix = MATH::Mat4::Identity();
            normalMatrix.m[0][0] = (a11 * a22 - a12 * a21) * invDet;
            normalMatrix.m[0][1] = (a02 * a21 - a01 * a22) * invDet;
            normalMatrix.m[0][2] = (a01 * a12 - a02 * a11) * invDet;
            normalMatrix.m[1][0] = (a12 * a20 - a10 * a22) * invDet;
            normalMatrix.m[1][1] = (a00 * a22 - a02 * a20) * invDet;
            normalMatrix.m[1][2] = (a02 * a10 - a00 * a12) * invDet;
            normalMatrix.m[2][0] = (a10 * a21 - a11 * a20) * invDet;
            normalMatrix.m[2][1] = (a01 * a20 - a00 * a21) * invDet;
            normalMatrix.m[2][2] = (a00 * a11 - a01 * a10) * invDet;
            return normalMatrix;
        }

        uint32_t BuildInstanceFlags(const SurfaceDrawPacket& packet) {
            uint32_t flags = 0;
            if (packet.isStatic && !packet.skinned) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::StaticGeometry);
            }
            if (packet.castShadow) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::CastShadow);
            }
            if (packet.receiveShadow) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::ReceiveShadow);
            }
            if (packet.key.alphaMasked) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::AlphaMasked);
            }
            if (packet.key.transparent) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::Transparent);
            }
            if (packet.materialOverride != nullptr) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::MaterialOverride);
            }
            if (packet.key.doubleSided) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::DoubleSided);
            }
            if (!packet.materialFxProfileId.empty()) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::MaterialFx);
            }
            if (packet.key.waterMaterialFx) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::WaterMaterialFx);
            }
            if (packet.key.clusterMainlineEligible) {
                flags |= ToFlag(SurfaceGpuSceneInstanceFlags::ClusterMainline);
            }
            return flags;
        }

        void FillMaterialFxData(const SurfaceDrawPacket& packet, SurfaceGpuSceneInstance& instance) {
            if (!packet.materialFxProfileId.empty()) {
                MaterialFxProfile profile{};
                if (MaterialFxProfile::LoadById(packet.materialFxProfileId, profile)) {
                    instance.fxFlags = profile.featureBits;
                    for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                        const DirectX::XMFLOAT4& value = profile.values[i];
                        instance.fxUser[i] = { value.x, value.y, value.z, value.w };
                    }
                }
            }

            if (!packet.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = packet.materialFxParamValues[i];
                instance.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillClusterGeometryData(
            const SurfaceDrawPacket& packet,
            SurfaceGpuSceneInstance& instance,
            SurfaceGpuSceneBuildStats* stats) {

            const SurfaceResourceIds& resources = packet.key.resources;
            if (!resources.clusterGeometry) {
                return;
            }

            if (stats != nullptr) {
                ++stats->clusterResourceInstanceCount;
            }

            const ClusterGeometryResourceRecord* record =
                GetClusterGeometryResource(resources.clusterGeometry);
            if (record != nullptr && record->ready && record->srv.IsValid()) {
                instance.clusterGeometrySrvDescriptorIndex = record->srv.descriptorIndex;
                instance.resourceFlags |= ToResourceFlag(
                    SurfaceGpuSceneResourceFlags::ClusterGeometryShaderVisible);
                if (stats != nullptr) {
                    ++stats->clusterShaderVisibleInstanceCount;
                }
            }

            const CLUSTER::ClusterGeometrySurfaceRange* range =
                FindClusterGeometrySurfaceRange(
                    resources.clusterGeometry,
                    packet.nodeIndex,
                    packet.meshIndex,
                    packet.primitiveIndex);
            if (range == nullptr) {
                if (stats != nullptr) {
                    ++stats->clusterMissingSurfaceRangeInstanceCount;
                }
                return;
            }

            instance.clusterSurfaceIndex = range->surfaceIndex;
            instance.clusterLodRangeIndex = range->firstLodRange;
            instance.clusterLodRangeCount = range->lodRangeCount;
            instance.clusterSelectedLodIndex = range->selectedLodIndex;
            const CLUSTER::ClusterGeometrySurfaceLodRange* lod0Range =
                FindClusterGeometrySurfaceLodRange(
                    resources.clusterGeometry,
                    range->firstLodRange,
                    range->lodRangeCount,
                    0u);
            if (lod0Range != nullptr) {
                instance.clusterRangeIndex = lod0Range->firstCluster;
                instance.clusterRangeCount = lod0Range->clusterCount;
                instance.clusterIndexCount = lod0Range->indexCount;
                instance.clusterSelectedLodIndex = lod0Range->lodIndex;
                instance.clusterLodFlags = lod0Range->flags;
            } else {
                instance.clusterRangeIndex = range->firstCluster;
                instance.clusterRangeCount = range->clusterCount;
                instance.clusterIndexCount = range->indexCount;
            }
            instance.resourceFlags |= ToResourceFlag(
                SurfaceGpuSceneResourceFlags::ClusterGeometrySurfaceRange);
            if (range->lodRangeCount > 0u) {
                instance.resourceFlags |= ToResourceFlag(
                    SurfaceGpuSceneResourceFlags::ClusterGeometryLodRanges);
            }
            if (stats != nullptr) {
                ++stats->clusterSurfaceRangeInstanceCount;
            }
        }
    }

    SurfaceGpuSceneBuildStats SurfaceGpuSceneWriter::BuildPacketList(
        const std::vector<SurfaceDrawPacket>& packets,
        const std::vector<uint32_t>& packetIndices,
        std::vector<SurfaceGpuSceneInstance>& outInstances) {

        outInstances.clear();
        outInstances.reserve(packetIndices.size());
        return AppendPacketList(packets, packetIndices, outInstances);
    }

    SurfaceGpuSceneBuildStats SurfaceGpuSceneWriter::AppendPacketList(
        const std::vector<SurfaceDrawPacket>& packets,
        const std::vector<uint32_t>& packetIndices,
        std::vector<SurfaceGpuSceneInstance>& outInstances) {

        SurfaceGpuSceneBuildStats stats{};
        outInstances.reserve(outInstances.size() + packetIndices.size());

        uint32_t localIndex = 0;
        for (const uint32_t packetIndex : packetIndices) {
            if (packetIndex >= packets.size()) {
                ++stats.skippedInvalidPacketCount;
                ++localIndex;
                continue;
            }

            SurfaceGpuSceneInstance instance = BuildInstance(
                packets[packetIndex],
                packetIndex,
                localIndex);
            FillClusterGeometryData(packets[packetIndex], instance, &stats);
            if (packets[packetIndex].key.resources.HasPoolHandles()) {
                ++stats.resourceBackedInstanceCount;
            } else {
                ++stats.missingResourceHandleInstanceCount;
            }
            outInstances.push_back(instance);
            ++stats.instanceCount;
            stats.maxCommandInstanceCount =
                (std::max)(stats.maxCommandInstanceCount, 1u);
            ++localIndex;
        }

        return stats;
    }

    SurfaceGpuSceneBuildStats SurfaceGpuSceneWriter::BuildCommandRanges(
        const std::vector<SurfaceDrawPacket>& packets,
        const std::vector<uint32_t>& executablePacketIndices,
        std::vector<SurfaceDrawCommand>& commands,
        std::vector<SurfaceGpuSceneInstance>& outInstances) {

        outInstances.clear();
        outInstances.reserve(executablePacketIndices.size());
        return AppendCommandRanges(
            packets,
            executablePacketIndices,
            commands,
            outInstances);
    }

    SurfaceGpuSceneBuildStats SurfaceGpuSceneWriter::AppendCommandRanges(
        const std::vector<SurfaceDrawPacket>& packets,
        const std::vector<uint32_t>& executablePacketIndices,
        std::vector<SurfaceDrawCommand>& commands,
        std::vector<SurfaceGpuSceneInstance>& outInstances) {

        SurfaceGpuSceneBuildStats stats{};
        outInstances.reserve(outInstances.size() + executablePacketIndices.size());

        for (SurfaceDrawCommand& command : commands) {
            command.backend = SurfaceDrawCommandBackend::CpuDirect;
            command.firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
            command.gpuSceneInstanceCount = 0;
            command.drawArgs.instanceCount = 0;
            ++stats.commandCount;

            if (command.packetCount == 0 ||
                command.firstExecutableIndex >= executablePacketIndices.size()) {
                ++stats.skippedInvalidCommandCount;
                continue;
            }

            const uint32_t firstInstanceIndex = ClampToUint32(outInstances.size());
            const size_t commandBegin = command.firstExecutableIndex;
            const size_t commandEnd = (std::min)(
                executablePacketIndices.size(),
                commandBegin + static_cast<size_t>(command.packetCount));

            uint32_t localIndex = 0;
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t packetIndex = executablePacketIndices[executableIndex];
                if (packetIndex >= packets.size()) {
                    ++stats.skippedInvalidPacketCount;
                    ++localIndex;
                    continue;
                }

                SurfaceGpuSceneInstance instance = BuildInstance(
                    packets[packetIndex],
                    packetIndex,
                    localIndex);
                FillClusterGeometryData(packets[packetIndex], instance, &stats);
                if (packets[packetIndex].key.resources.HasPoolHandles()) {
                    ++stats.resourceBackedInstanceCount;
                } else {
                    ++stats.missingResourceHandleInstanceCount;
                }
                outInstances.push_back(instance);
                ++command.gpuSceneInstanceCount;
                ++localIndex;
            }

            if (command.gpuSceneInstanceCount == 0) {
                command.firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
                continue;
            }

            command.firstGpuSceneInstanceIndex = firstInstanceIndex;
            if (command.drawArgsValid) {
                command.drawArgs.instanceCount = command.gpuSceneInstanceCount;
                // Forward / Shadow はどちらも SurfaceDrawCommand から indirect 実行へ進める。
                if (command.pass == SurfaceDrawCommandPass::Forward ||
                    command.pass == SurfaceDrawCommandPass::DepthAware ||
                    command.pass == SurfaceDrawCommandPass::Shadow) {
                    command.backend = SurfaceDrawCommandBackend::GpuDriven;
                }
            }
            stats.instanceCount += command.gpuSceneInstanceCount;
            stats.maxCommandInstanceCount =
                (std::max)(stats.maxCommandInstanceCount, command.gpuSceneInstanceCount);
        }

        return stats;
    }

    SurfaceGpuSceneInstance SurfaceGpuSceneWriter::BuildInstance(
        const SurfaceDrawPacket& packet,
        uint32_t sourcePacketIndex,
        uint32_t sourceCommandLocalIndex) {

        SurfaceGpuSceneInstance instance{};
        instance.world = packet.drawWorldMatrix;
        instance.normalMatrix = BuildNormalMatrixFromWorld(packet.drawWorldMatrix);
        // HCMESH は node global を頂点へ bake 済みなので、cluster draw では object world だけを使う。
        instance.clusterWorld = packet.objectWorldTransform.GetWorldMatrix();
        instance.clusterNormalMatrix = BuildNormalMatrixFromWorld(instance.clusterWorld);
        instance.boundsCenterRadius = BuildBoundsCenterRadius(packet.worldBounds);
        // LOD 判定だけは object 全体の見た目サイズを参照できるようにし、分割済み surface の過剰降段を防ぐ。
        instance.sourcePacketIndex = sourcePacketIndex;
        instance.sourceSurfaceInstanceIndex = packet.sourceSurfaceInstanceIndex;
        instance.objectIdLow = static_cast<uint32_t>(packet.objectId.value & 0xffffffffull);
        instance.objectIdHigh = static_cast<uint32_t>((packet.objectId.value >> 32) & 0xffffffffull);
        instance.meshIndex = packet.meshIndex;
        instance.primitiveIndex = packet.primitiveIndex;
        instance.sourceMaterialIndex = packet.materialIndex;
        instance.nodeIndex = packet.nodeIndex;
        instance.flags = BuildInstanceFlags(packet);
        instance.geometryBackend = static_cast<uint32_t>(packet.key.geometryBackend);
        const SurfaceResourceIds& resources = packet.key.resources;
        if (resources.mesh) {
            instance.meshResourceIndex = resources.mesh.index;
            instance.meshResourceGeneration = resources.mesh.generation;
            instance.resourceFlags |= ToResourceFlag(SurfaceGpuSceneResourceFlags::Mesh);
        }
        if (resources.material) {
            instance.materialResourceIndex = resources.material.index;
            instance.materialResourceGeneration = resources.material.generation;
            instance.resourceFlags |= ToResourceFlag(SurfaceGpuSceneResourceFlags::Material);
        }
        if (resources.clusterGeometry) {
            instance.clusterGeometryResourceIndex = resources.clusterGeometry.index;
            instance.clusterGeometryResourceGeneration = resources.clusterGeometry.generation;
            instance.resourceFlags |= ToResourceFlag(SurfaceGpuSceneResourceFlags::ClusterGeometry);
        }
        FillMaterialFxData(packet, instance);
        (void)sourceCommandLocalIndex;
        return instance;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
