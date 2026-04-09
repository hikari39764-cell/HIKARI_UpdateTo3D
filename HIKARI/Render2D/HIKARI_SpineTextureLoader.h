#pragma once
#include <string>
#include <unordered_map>
#include <Novice.h>
#include "HIKARI_DxTexture.h"
// Spine
#include <spine/Atlas.h>
#include <spine/TextureLoader.h>

namespace HIKARI {

    struct SpineTexture {
        int handle{ -1 };
        int width{ 0 };
        int height{ 0 };
    };


    class TextureLoader_Novice : public spine::TextureLoader {
    public:
        void load(spine::AtlasPage& page, const spine::String& path) override;
        void unload(void* rendererObject) override;
    };

} // namespace HIKARI
