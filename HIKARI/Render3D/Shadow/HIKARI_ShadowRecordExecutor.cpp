#include "Render3D/Shadow/HIKARI_ShadowRecordExecutor.h"

namespace HIKARI::SHADOW::RECORD {

    bool InitializeShadowRecordExecutor(ID3D12Device* device) {
        return device != nullptr;
    }

    void ResetShadowRecordExecutor() {
    }

} // namespace HIKARI::SHADOW::RECORD
