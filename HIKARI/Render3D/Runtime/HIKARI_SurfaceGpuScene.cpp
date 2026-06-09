#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        uint32_t ToFlag(SurfaceGpuSceneInstanceFlags flag) {
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
            return flags;
        }
    }

    SurfaceGpuSceneBuildStats SurfaceGpuSceneWriter::BuildCommandRanges(
        const std::vector<SurfaceDrawPacket>& packets,
        const std::vector<uint32_t>& executablePacketIndices,
        std::vector<SurfaceDrawCommand>& commands,
        std::vector<SurfaceGpuSceneInstance>& outInstances) {

        SurfaceGpuSceneBuildStats stats{};
        outInstances.clear();
        outInstances.reserve(executablePacketIndices.size());

        for (SurfaceDrawCommand& command : commands) {
            command.firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
            command.gpuSceneInstanceCount = 0;
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

                outInstances.push_back(BuildInstance(
                    packets[packetIndex],
                    packetIndex,
                    localIndex));
                ++command.gpuSceneInstanceCount;
                ++localIndex;
            }

            if (command.gpuSceneInstanceCount == 0) {
                command.firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
                continue;
            }

            command.firstGpuSceneInstanceIndex = firstInstanceIndex;
            stats.instanceCount += command.gpuSceneInstanceCount;
            stats.maxCommandInstanceCount =
                (std::max)(stats.maxCommandInstanceCount, command.gpuSceneInstanceCount);
        }

        return stats;
    }

    SurfaceGpuSceneInstance SurfaceGpuSceneWriter::BuildInstance(
        const SurfaceDrawPacket& packet,
        uint32_t sourcePacketIndex,
        uint32_t commandLocalIndex) {

        SurfaceGpuSceneInstance instance{};
        instance.world = packet.drawWorldMatrix;
        instance.boundsCenterRadius = BuildBoundsCenterRadius(packet.worldBounds);
        instance.sourcePacketIndex = sourcePacketIndex;
        instance.sourceSurfaceInstanceIndex = packet.sourceSurfaceInstanceIndex;
        instance.objectIdLow = static_cast<uint32_t>(packet.objectId.value & 0xffffffffull);
        instance.objectIdHigh = static_cast<uint32_t>((packet.objectId.value >> 32) & 0xffffffffull);
        instance.meshIndex = packet.meshIndex;
        instance.primitiveIndex = packet.primitiveIndex;
        instance.materialIndex = packet.materialIndex;
        instance.nodeIndex = packet.nodeIndex;
        instance.flags = BuildInstanceFlags(packet);
        instance.commandLocalIndex = commandLocalIndex;
        return instance;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
