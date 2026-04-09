#include "HIKARI_SpineTextureLoader.h"

namespace HIKARI {


    void TextureLoader_Kamata::load(spine::AtlasPage& page, const spine::String& path) {
        const std::string p = path.buffer();

        int handle = HIKARI::DXTEX::DxTextureManager::LoadTexture(p, p);
        if (handle < 0) {
            return;
        }

        // 查询纹理尺寸
        UINT w = 0, h = 0;
        HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, w, h);

        // 创建扩展信息
        SpineTexture* tex = new SpineTexture{};
        tex->handle = handle;
        tex->width = static_cast<int>(w);
        tex->height = static_cast<int>(h);


        page.texture = tex;
    }

    void TextureLoader_Kamata::unload(void* textureObject) {

    }

} // namespace HIKARI
