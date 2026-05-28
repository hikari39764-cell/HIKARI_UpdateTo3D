#include "HIKARI_SpineTextureLoader.h"

namespace HIKARI {


    void SpineDxTextureLoader::load(spine::AtlasPage& page, const spine::String& path) {
        const std::string p = path.buffer();

        int handle = HIKARI::DXTEX::DxTextureManager::LoadTexture(p, p);
        if (handle < 0) {
            return;
        }

        // Spine が必要とする実サイズを DxTextureManager から取得する。
        UINT w = 0, h = 0;
        HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, w, h);

        // AtlasPage に rendererObject として保持させる。
        SpineTexture* tex = new SpineTexture{};
        tex->handle = handle;
        tex->width = static_cast<int>(w);
        tex->height = static_cast<int>(h);


        page.texture = tex;
    }

    void SpineDxTextureLoader::unload(void* textureObject) {
        (void)textureObject;
    }

} // namespace HIKARI
