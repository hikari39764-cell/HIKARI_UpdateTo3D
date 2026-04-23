#pragma once
#include <string>
#include <unordered_map>
#include "Gfx/HIKARI_GpuResources.h"
// Spine
#include <spine/Atlas.h>
#include <spine/TextureLoader.h>

namespace HIKARI {

    struct SpineTexture {
        uint32_t gpuResourceId{ 0 };
        int legacyHandle{ -1 };
        int width{ 0 };
        int height{ 0 };
    };


    class TextureLoader_Kamata : public spine::TextureLoader {
    public:
        void load(spine::AtlasPage& page, const spine::String& path) override;
        void unload(void* rendererObject) override;
    };

} // namespace HIKARI
