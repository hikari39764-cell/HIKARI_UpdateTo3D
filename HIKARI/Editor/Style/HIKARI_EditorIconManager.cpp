#include "HIKARI_EditorIconManager.h"

#if defined(_DEBUG)
#include <algorithm>
#include <cstdint>

#include "Render2D/HIKARI_DxTexture.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(_DEBUG)
        constexpr const char* kIconAtlasTextureId = "editor/asset_icon_atlas";
        constexpr const char* kIconAtlasPath = "HIKARI/Icon/hikari_asset_icons.png";
        constexpr int kIconAtlasColumns = 4;
        constexpr int kIconAtlasRows = 3;

        int gIconAtlasHandle = -2;

        bool EnsureIconAtlasLoaded() {
            if (gIconAtlasHandle == -2) {
                // エディタ専用アイコンは AssetDatabase に登録しない。
                gIconAtlasHandle = DXTEX::DxTextureManager::LoadTextureSrgb(kIconAtlasTextureId, kIconAtlasPath);
            }
            return gIconAtlasHandle >= 0;
        }

        bool DrawAtlasCell(int iconIndex, const ImVec2& size) {
            if (!EnsureIconAtlasLoaded()) {
                return false;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE srv =
                DXTEX::DxTextureManager::GetSrvGpuHandle(gIconAtlasHandle);
            if (srv.ptr == 0) {
                return false;
            }

            const int clampedIndex = (std::max)(0, (std::min)(iconIndex, kIconAtlasColumns * kIconAtlasRows - 1));
            const int column = clampedIndex % kIconAtlasColumns;
            const int row = clampedIndex / kIconAtlasColumns;
            const ImVec2 uv0{
                static_cast<float>(column) / static_cast<float>(kIconAtlasColumns),
                static_cast<float>(row) / static_cast<float>(kIconAtlasRows)
            };
            const ImVec2 uv1{
                static_cast<float>(column + 1) / static_cast<float>(kIconAtlasColumns),
                static_cast<float>(row + 1) / static_cast<float>(kIconAtlasRows)
            };

            ImGui::Image(
                reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)),
                size,
                uv0,
                uv1);
            return true;
        }
#endif

        EditorIconKind ToIconKind(AssetType type) {
            switch (type) {
            case AssetType::Texture: return EditorIconKind::Texture;
            case AssetType::Model: return EditorIconKind::Model;
            case AssetType::Material: return EditorIconKind::Material;
            case AssetType::Sky: return EditorIconKind::Sky;
            case AssetType::Scene: return EditorIconKind::Scene;
            case AssetType::VfxEffect: return EditorIconKind::Vfx;
            case AssetType::Unknown:
            default: return EditorIconKind::Unknown;
            }
        }
    }

    bool EditorIconManager::Initialize() {
#if defined(_DEBUG)
        return EnsureIconAtlasLoaded();
#else
        return false;
#endif
    }

    void EditorIconManager::Finalize() {
#if defined(_DEBUG)
        gIconAtlasHandle = -2;
#endif
    }

#if defined(_DEBUG)
    bool EditorIconManager::DrawIcon(EditorIconKind kind, const ImVec2& size) {
        if (DrawAtlasCell(GetAtlasIndex(kind), size)) {
            return true;
        }

        ImGui::TextDisabled("%s", GetFallbackText(kind));
        return true;
    }

    bool EditorIconManager::DrawAssetIcon(AssetType type, const ImVec2& size) {
        return DrawIcon(ToIconKind(type), size);
    }
#endif

    const char* EditorIconManager::GetFallbackText(EditorIconKind kind) {
        switch (kind) {
        case EditorIconKind::Folder: return "[Dir]";
        case EditorIconKind::Texture: return "[Tex]";
        case EditorIconKind::Model: return "[Mdl]";
        case EditorIconKind::Material: return "[Mat]";
        case EditorIconKind::Sky: return "[Sky]";
        case EditorIconKind::Scene: return "[Scn]";
        case EditorIconKind::Vfx: return "[Vfx]";
        case EditorIconKind::Component: return "[Cmp]";
        case EditorIconKind::System: return "[Sys]";
        case EditorIconKind::GameObject: return "[Obj]";
        case EditorIconKind::Camera: return "[Cam]";
        case EditorIconKind::Light: return "[Lit]";
        case EditorIconKind::Transform: return "[Tr]";
        case EditorIconKind::Unknown:
        default: return "[?]";
        }
    }

    const char* EditorIconManager::GetAssetFallbackText(AssetType type) {
        return GetFallbackText(ToIconKind(type));
    }

    int EditorIconManager::GetAtlasIndex(EditorIconKind kind) {
        switch (kind) {
        case EditorIconKind::Folder: return 0;
        case EditorIconKind::Texture: return 1;
        case EditorIconKind::Model: return 2;
        case EditorIconKind::Material: return 3;
        case EditorIconKind::Sky: return 4;
        case EditorIconKind::Scene: return 5;
        case EditorIconKind::Vfx: return 6;
        case EditorIconKind::Component: return 10;
        case EditorIconKind::System: return 10;
        case EditorIconKind::GameObject: return 10;
        case EditorIconKind::Camera: return 10;
        case EditorIconKind::Light: return 10;
        case EditorIconKind::Transform: return 10;
        case EditorIconKind::Unknown:
        default: return 10;
        }
    }

} // namespace HIKARI::EDITOR
