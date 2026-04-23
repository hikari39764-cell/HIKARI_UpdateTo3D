#include "HIKARI_SpineTextureLoader.h"

namespace HIKARI {


    void TextureLoader_Kamata::load(spine::AtlasPage& page, const spine::String& path) {
        const std::string p = path.buffer();

        const uint32_t gpuTextureId = HIKARI::GpuResources::LoadTexture(p, p);
        if (gpuTextureId == 0) {
            return;
        }

        // 查询纹理尺寸
        UINT w = 0, h = 0;
        uint32_t w32 = 0;
        uint32_t h32 = 0;
        HIKARI::GpuResources::GetTextureSize(gpuTextureId, w32, h32);
        w = static_cast<UINT>(w32);
        h = static_cast<UINT>(h32);

        // 创建扩展信息
        SpineTexture* tex = new SpineTexture{};
        tex->gpuResourceId = gpuTextureId;
        tex->legacyHandle = static_cast<int>(gpuTextureId) - 1;
        tex->width = static_cast<int>(w);
        tex->height = static_cast<int>(h);


        page.texture = tex;
    }

    void TextureLoader_Kamata::unload(void* textureObject) {

    }

} // namespace HIKARI
