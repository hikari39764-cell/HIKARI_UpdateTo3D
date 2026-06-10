#include "Render3D/Runtime/HIKARI_SurfaceDrawCommandBuilder.h"

#include <algorithm>
#include <limits>

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        SurfaceDrawIndexedArgs BuildDrawIndexedArgs(
            const SurfaceDrawPacket& packet,
            uint32_t instanceCount) {

            SurfaceDrawIndexedArgs args{};
            if (packet.model == nullptr ||
                packet.meshIndex >= packet.model->meshes.size()) {
                return args;
            }

            const MeshAsset& mesh = packet.model->meshes[packet.meshIndex];
            if (packet.primitiveIndex >= mesh.primitives.size()) {
                return args;
            }

            const MeshPrimitive& primitive = mesh.primitives[packet.primitiveIndex];
            args.indexCountPerInstance = ClampToUint32(primitive.indices.size());
            args.instanceCount = instanceCount;
            args.startIndexLocation = 0;
            args.baseVertexLocation = 0;
            args.startInstanceLocation = 0;
            return args;
        }

        bool IsValidDrawIndexedArgs(const SurfaceDrawIndexedArgs& args) {
            return args.indexCountPerInstance > 0 && args.instanceCount > 0;
        }
    }

    SurfaceDrawCommandBuilder::SurfaceDrawCommandBuilder(
        SurfaceDrawCommandPass pass,
        const std::vector<SurfaceDrawPacket>& packets,
        std::vector<uint32_t>& executablePacketIndices,
        std::vector<SurfaceDrawCommand>& commands)
        : pass_(pass)
        , packets_(packets)
        , executablePacketIndices_(executablePacketIndices)
        , commands_(commands) {
    }

    void SurfaceDrawCommandBuilder::ClearOutput() {
        executablePacketIndices_.clear();
        commands_.clear();
        currentKey_ = {};
        hasCurrentKey_ = false;
        currentStart_ = 0;
        currentLength_ = 0;
        stats_ = {};
    }

    bool SurfaceDrawCommandBuilder::AppendPacket(uint32_t packetIndex) {
        if (packetIndex >= packets_.size()) {
            Flush();
            return false;
        }

        const SurfaceDrawPacket& packet = packets_[packetIndex];
        const SurfaceDrawBatchKey batchKey = BuildSurfaceDrawBatchKey(pass_, packet.key);
        if (!CanContinueCommand(batchKey)) {
            Flush();
            currentKey_ = batchKey;
            hasCurrentKey_ = true;
            currentStart_ = ClampToUint32(executablePacketIndices_.size());
        }

        executablePacketIndices_.push_back(packetIndex);
        ++currentLength_;
        return true;
    }

    void SurfaceDrawCommandBuilder::Flush() {
        if (currentLength_ == 0) {
            return;
        }

        SurfaceDrawCommand command = BuildCommand(currentStart_, currentLength_);
        commands_.push_back(command);

        ++stats_.commandCount;
        if (currentLength_ == 1) {
            ++stats_.singlePacketCommandCount;
        } else {
            ++stats_.mergedCommandCount;
            stats_.savedCommandCount += currentLength_ - 1;
        }
        if (command.drawArgsValid) {
            ++stats_.indirectReadyCommandCount;
        } else {
            ++stats_.missingDrawArgsCommandCount;
        }
        stats_.maxCommandPacketCount = (std::max)(stats_.maxCommandPacketCount, currentLength_);

        currentKey_ = {};
        hasCurrentKey_ = false;
        currentStart_ = 0;
        currentLength_ = 0;
    }

    const SurfaceDrawCommandBuildStats& SurfaceDrawCommandBuilder::GetStats() const {
        return stats_;
    }

    bool SurfaceDrawCommandBuilder::CanContinueCommand(const SurfaceDrawBatchKey& key) const {
        if (!hasCurrentKey_) {
            return false;
        }

        return IsSameSurfaceDrawBatchKey(currentKey_, key);
    }

    SurfaceDrawCommand SurfaceDrawCommandBuilder::BuildCommand(
        uint32_t firstExecutableIndex,
        uint32_t packetCount) const {

        SurfaceDrawCommand command{};
        command.pass = pass_;
        command.firstExecutableIndex = firstExecutableIndex;
        command.packetCount = packetCount;
        command.singlePacket = packetCount == 1;

        if (packetCount == 0 || firstExecutableIndex >= executablePacketIndices_.size()) {
            return command;
        }

        const uint32_t firstPacketIndex = executablePacketIndices_[firstExecutableIndex];
        command.firstPacketIndex = firstPacketIndex;
        if (firstPacketIndex >= packets_.size()) {
            return command;
        }

        const SurfaceDrawPacketKey& key = packets_[firstPacketIndex].key;
        command.batchKey = BuildSurfaceDrawBatchKey(pass_, key);
        command.resources = key.resources;
        command.psoKey = key.psoKey;
        command.geometryKey = key.geometryKey;
        command.materialKey = key.materialKey;
        command.textureSetKey = key.textureSetKey;
        command.modelKey = key.modelKey;
        command.transparent = key.transparent;
        command.drawArgs = BuildDrawIndexedArgs(
            packets_[firstPacketIndex],
            packetCount);
        command.drawArgsValid = IsValidDrawIndexedArgs(command.drawArgs);
        return command;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
