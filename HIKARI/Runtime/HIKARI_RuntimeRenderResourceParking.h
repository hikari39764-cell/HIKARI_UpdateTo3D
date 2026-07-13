#pragma once

namespace HIKARI::GFX {
    class Dx12Core;
    struct Context;
}

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::RUNTIME {

    bool ParkEditorForStandalone(
        DocumentSceneBase& scene,
        GFX::Dx12Core& core);
    bool RestoreEditorAfterStandalone(
        DocumentSceneBase& scene,
        const GFX::Context& context);
    bool IsEditorParkedForStandalone();

} // namespace HIKARI::RUNTIME
