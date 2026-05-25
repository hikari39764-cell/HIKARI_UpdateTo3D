#pragma once

namespace HIKARI {

    class AssetDatabase;
    class AssetRegistry;
    struct SceneEnvironment;

    namespace SKYRENDERER {
        struct SkyRendererDebugState;
    }

    class EnvironmentPanel {
    public:
        void Draw(
            SceneEnvironment& environment,
            const SKYRENDERER::SkyRendererDebugState* skyDebugState,
            const AssetRegistry* assetRegistry = nullptr,
            const AssetDatabase* assetDatabase = nullptr) const;
    };

} // namespace HIKARI
