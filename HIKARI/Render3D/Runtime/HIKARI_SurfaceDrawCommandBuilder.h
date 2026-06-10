#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

namespace HIKARI::RENDER3D::RUNTIME {

    struct SurfaceDrawCommandBuildStats {
        uint32_t commandCount = 0;
        uint32_t singlePacketCommandCount = 0;
        uint32_t mergedCommandCount = 0;
        uint32_t savedCommandCount = 0;
        uint32_t indirectReadyCommandCount = 0;
        uint32_t missingDrawArgsCommandCount = 0;
        uint32_t maxCommandPacketCount = 0;
    };

    class SurfaceDrawCommandBuilder final {
    public:
        SurfaceDrawCommandBuilder(
            SurfaceDrawCommandPass pass,
            const std::vector<SurfaceDrawPacket>& packets,
            std::vector<uint32_t>& executablePacketIndices,
            std::vector<SurfaceDrawCommand>& commands);

        void ClearOutput();
        bool AppendPacket(uint32_t packetIndex);
        void Flush();

        const SurfaceDrawCommandBuildStats& GetStats() const;

    private:
        bool CanContinueCommand(const SurfaceDrawBatchKey& key) const;
        SurfaceDrawCommand BuildCommand(uint32_t firstExecutableIndex, uint32_t packetCount) const;

        SurfaceDrawCommandPass pass_ = SurfaceDrawCommandPass::Forward;
        const std::vector<SurfaceDrawPacket>& packets_;
        std::vector<uint32_t>& executablePacketIndices_;
        std::vector<SurfaceDrawCommand>& commands_;

        SurfaceDrawBatchKey currentKey_{};
        bool hasCurrentKey_ = false;
        uint32_t currentStart_ = 0;
        uint32_t currentLength_ = 0;
        SurfaceDrawCommandBuildStats stats_{};
    };

} // namespace HIKARI::RENDER3D::RUNTIME
