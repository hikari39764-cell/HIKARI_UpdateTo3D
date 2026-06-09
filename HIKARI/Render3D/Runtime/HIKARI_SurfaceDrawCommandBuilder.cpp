#include "Render3D/Runtime/HIKARI_SurfaceDrawCommandBuilder.h"

#include <algorithm>
#include <limits>

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
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
        if (!CanContinueCommand(packet.key)) {
            Flush();
            currentKey_ = packet.key;
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

        commands_.push_back(BuildCommand(currentStart_, currentLength_));

        ++stats_.commandCount;
        if (currentLength_ == 1) {
            ++stats_.singlePacketCommandCount;
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

    bool SurfaceDrawCommandBuilder::CanContinueCommand(const SurfaceDrawPacketKey& key) const {
        if (!hasCurrentKey_) {
            return false;
        }

        return
            currentKey_.passMask == key.passMask &&
            currentKey_.psoKey == key.psoKey &&
            currentKey_.geometryKey == key.geometryKey;
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
        command.psoKey = key.psoKey;
        command.geometryKey = key.geometryKey;
        command.materialKey = key.materialKey;
        command.textureSetKey = key.textureSetKey;
        command.modelKey = key.modelKey;
        command.transparent = key.transparent;
        return command;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
