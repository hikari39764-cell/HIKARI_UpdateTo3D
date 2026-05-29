#include "HIKARI_DefaultPbrResources.h"

#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Render2D/HIKARI_DxTexture.h"

namespace HIKARI {

    namespace {
        bool gInitialized = false;
        RuntimeTextureSlot gWhiteSlot{};
        RuntimeTextureSlot gBlackSlot{};
        RuntimeTextureSlot gFlatNormalSlot{};
        RuntimeTextureSlot gMetallicRoughnessSlot{};
        RuntimeTextureSlot gMissingSlot{};

        RuntimeTextureSlot MakeDefaultSlot(std::string resolvedPath, int handle)
        {
            RuntimeTextureSlot slot{};
            slot.sourcePath = resolvedPath;
            slot.resolvedPath = std::move(resolvedPath);
            slot.handle = handle;
            // デフォルト貼り付けは SRV を埋めるだけで、ユーザー texture としては扱わない。
            slot.enabled = false;
            return slot;
        }

        void ReleaseSlot(RuntimeTextureSlot& slot)
        {
            if (slot.handle >= 0) {
                DXTEX::DxTextureManager::ReleaseTexture(slot.handle);
            }
            slot = {};
        }
    }

    void DefaultPbrResources::Initialize()
    {
        if (gInitialized) {
            return;
        }

        gWhiteSlot = MakeDefaultSlot(
            "generated://pbr/default_white",
            DXTEX::DxTextureManager::CreateSolidColorTexture(
                "default_pbr/white",
                0xffffffffu,
                DXTEX::TextureColorSpace::Linear));
        gBlackSlot = MakeDefaultSlot(
            "generated://pbr/default_black",
            DXTEX::DxTextureManager::CreateSolidColorTexture(
                "default_pbr/black",
                0x000000ffu,
                DXTEX::TextureColorSpace::Linear));
        gFlatNormalSlot = MakeDefaultSlot(
            "generated://pbr/default_flat_normal",
            DXTEX::DxTextureManager::CreateSolidColorTexture(
                "default_pbr/flat_normal",
                0x8080ffffu,
                DXTEX::TextureColorSpace::Linear));
        gMetallicRoughnessSlot = MakeDefaultSlot(
            "generated://pbr/default_metallic_roughness",
            DXTEX::DxTextureManager::CreateSolidColorTexture(
                "default_pbr/metallic_roughness",
                0xffff00ffu,
                DXTEX::TextureColorSpace::Linear));
        gMissingSlot = MakeDefaultSlot(
            "generated://pbr/missing_checker",
            DXTEX::DxTextureManager::CreateCheckerTexture(
                "default_pbr/missing",
                0xff00ffffu,
                0x000000ffu,
                DXTEX::TextureColorSpace::Linear));

        gInitialized = true;
        HIKARI_LOG_INFO("[DefaultPbrResources] initialized.");
    }

    void DefaultPbrResources::Shutdown()
    {
        if (!gInitialized) {
            return;
        }

        ReleaseSlot(gWhiteSlot);
        ReleaseSlot(gBlackSlot);
        ReleaseSlot(gFlatNormalSlot);
        ReleaseSlot(gMetallicRoughnessSlot);
        ReleaseSlot(gMissingSlot);
        gInitialized = false;
        HIKARI_LOG_INFO("[DefaultPbrResources] shutdown.");
    }

    RuntimeTextureSlot DefaultPbrResources::WhiteSlot()
    {
        Initialize();
        return gWhiteSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::BlackSlot()
    {
        Initialize();
        return gBlackSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::FlatNormalSlot()
    {
        Initialize();
        return gFlatNormalSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::MetallicRoughnessSlot()
    {
        Initialize();
        return gMetallicRoughnessSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::MissingSlot()
    {
        Initialize();
        return gMissingSlot;
    }

} // namespace HIKARI
