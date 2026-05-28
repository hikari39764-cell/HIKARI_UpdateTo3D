#include "HIKARI_EditorIconManager.h"

#if defined(_DEBUG)
#include <algorithm>
#include <array>
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
        constexpr std::size_t kIconKindCount = static_cast<std::size_t>(EditorIconKind::Count);

        int gIconAtlasHandle = -2;
        std::array<int, kIconKindCount> gNamedIconHandles = [] {
            std::array<int, kIconKindCount> handles{};
            handles.fill(-2);
            return handles;
        }();

        const char* NamedIconPath(EditorIconKind kind) {
            switch (kind) {
            case EditorIconKind::Folder: return "HIKARI/Icon/DirectoryIcon.png";
            case EditorIconKind::File: return "HIKARI/Icon/FileIcon.png";
            case EditorIconKind::Music: return "HIKARI/Icon/music.png";
            case EditorIconKind::GameObject: return "HIKARI/Icon/Entity.png";
            case EditorIconKind::Component: return "HIKARI/Icon/Entity2.png";
            case EditorIconKind::Settings: return "HIKARI/Icon/Setting.png";
            case EditorIconKind::Play: return "HIKARI/Icon/PlayButton.png";
            case EditorIconKind::Stop: return "HIKARI/Icon/StopButton.png";
            case EditorIconKind::Translate: return "HIKARI/Icon/Trans.png";
            case EditorIconKind::Rotate: return "HIKARI/Icon/Rotate.png";
            case EditorIconKind::Scale: return "HIKARI/Icon/Scale.png";
            default: return nullptr;
            }
        }

        const char* NamedIconId(EditorIconKind kind) {
            switch (kind) {
            case EditorIconKind::Folder: return "editor/icon/folder";
            case EditorIconKind::File: return "editor/icon/file";
            case EditorIconKind::Music: return "editor/icon/music";
            case EditorIconKind::GameObject: return "editor/icon/game_object";
            case EditorIconKind::Component: return "editor/icon/component";
            case EditorIconKind::Settings: return "editor/icon/settings";
            case EditorIconKind::Play: return "editor/icon/play";
            case EditorIconKind::Stop: return "editor/icon/stop";
            case EditorIconKind::Translate: return "editor/icon/translate";
            case EditorIconKind::Rotate: return "editor/icon/rotate";
            case EditorIconKind::Scale: return "editor/icon/scale";
            default: return nullptr;
            }
        }

        bool EnsureIconAtlasLoaded() {
            if (gIconAtlasHandle == -2) {
                // エディタ専用アイコンは AssetDatabase に登録しない。
                gIconAtlasHandle = DXTEX::DxTextureManager::LoadTextureSrgb(kIconAtlasTextureId, kIconAtlasPath);
            }
            return gIconAtlasHandle >= 0;
        }

        bool TryGetNamedIconImage(EditorIconKind kind, ImTextureID& outTexture, ImVec2& outUv0, ImVec2& outUv1) {
            const char* path = NamedIconPath(kind);
            const char* id = NamedIconId(kind);
            if (!path || !id) {
                return false;
            }

            const std::size_t index = static_cast<std::size_t>(kind);
            if (index >= gNamedIconHandles.size()) {
                return false;
            }

            int& handle = gNamedIconHandles[index];
            if (handle == -2) {
                // 単体 PNG はツールバーや階層用に遅延ロードする。
                handle = DXTEX::DxTextureManager::LoadTextureSrgb(id, path);
            }
            if (handle < 0) {
                return false;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE srv = DXTEX::DxTextureManager::GetSrvGpuHandle(handle);
            if (srv.ptr == 0) {
                return false;
            }

            outTexture = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
            outUv0 = ImVec2(0.0f, 0.0f);
            outUv1 = ImVec2(1.0f, 1.0f);
            return true;
        }

        bool TryGetAtlasImage(int iconIndex, ImTextureID& outTexture, ImVec2& outUv0, ImVec2& outUv1) {
            if (!EnsureIconAtlasLoaded()) {
                return false;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE srv = DXTEX::DxTextureManager::GetSrvGpuHandle(gIconAtlasHandle);
            if (srv.ptr == 0) {
                return false;
            }

            const int clampedIndex = (std::max)(0, (std::min)(iconIndex, kIconAtlasColumns * kIconAtlasRows - 1));
            const int column = clampedIndex % kIconAtlasColumns;
            const int row = clampedIndex / kIconAtlasColumns;
            outUv0 = ImVec2{
                static_cast<float>(column) / static_cast<float>(kIconAtlasColumns),
                static_cast<float>(row) / static_cast<float>(kIconAtlasRows)
            };
            outUv1 = ImVec2{
                static_cast<float>(column + 1) / static_cast<float>(kIconAtlasColumns),
                static_cast<float>(row + 1) / static_cast<float>(kIconAtlasRows)
            };
            outTexture = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
            return true;
        }

        int AtlasIndexForKind(EditorIconKind kind) {
            switch (kind) {
            case EditorIconKind::Folder: return 0;
            case EditorIconKind::Texture: return 1;
            case EditorIconKind::Model: return 2;
            case EditorIconKind::Material: return 3;
            case EditorIconKind::Sky: return 4;
            case EditorIconKind::Scene: return 5;
            case EditorIconKind::Vfx: return 6;
            default: return 10;
            }
        }

        bool TryGetIconImage(EditorIconKind kind, ImTextureID& outTexture, ImVec2& outUv0, ImVec2& outUv1) {
            if (TryGetNamedIconImage(kind, outTexture, outUv0, outUv1)) {
                return true;
            }
            return TryGetAtlasImage(AtlasIndexForKind(kind), outTexture, outUv0, outUv1);
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
        gNamedIconHandles.fill(-2);
#endif
    }

#if defined(_DEBUG)
    bool EditorIconManager::DrawIcon(EditorIconKind kind, const ImVec2& size) {
        ImTextureID texture{};
        ImVec2 uv0{};
        ImVec2 uv1{};
        if (TryGetIconImage(kind, texture, uv0, uv1)) {
            ImGui::Image(texture, size, uv0, uv1);
            return true;
        }

        ImGui::TextDisabled("%s", GetFallbackText(kind));
        return true;
    }

    bool EditorIconManager::DrawAssetIcon(AssetType type, const ImVec2& size) {
        return DrawIcon(ToIconKind(type), size);
    }

    bool EditorIconManager::IconButton(
        EditorIconKind kind,
        const char* id,
        const ImVec2& size,
        bool selected,
        const char* tooltip) {

        ImGui::PushID(id ? id : GetFallbackText(kind));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##icon_button", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 max{ pos.x + size.x, pos.y + size.y };
        const ImU32 bg = selected
            ? ImGui::GetColorU32(ImVec4(0.16f, 0.42f, 0.48f, 0.92f))
            : active
                ? ImGui::GetColorU32(ImVec4(0.18f, 0.25f, 0.30f, 0.92f))
                : hovered
                    ? ImGui::GetColorU32(ImVec4(0.20f, 0.27f, 0.32f, 0.86f))
                    : ImGui::GetColorU32(ImVec4(0.10f, 0.13f, 0.16f, 0.64f));
        drawList->AddRectFilled(pos, max, bg, 4.0f);

        ImTextureID texture{};
        ImVec2 uv0{};
        ImVec2 uv1{};
        if (TryGetIconImage(kind, texture, uv0, uv1)) {
            const float padding = 4.0f;
            drawList->AddImage(
                texture,
                ImVec2(pos.x + padding, pos.y + padding),
                ImVec2(max.x - padding, max.y - padding),
                uv0,
                uv1,
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.88f)));
        } else {
            drawList->AddText(
                ImVec2(pos.x + 4.0f, pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.80f, 1.0f)),
                GetFallbackText(kind));
        }

        if (tooltip && hovered) {
            ImGui::SetTooltip("%s", tooltip);
        }

        ImGui::PopID();
        return pressed;
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
        case EditorIconKind::File: return "[File]";
        case EditorIconKind::Music: return "[Mus]";
        case EditorIconKind::Component: return "[Cmp]";
        case EditorIconKind::System: return "[Sys]";
        case EditorIconKind::GameObject: return "[Obj]";
        case EditorIconKind::Camera: return "[Cam]";
        case EditorIconKind::Light: return "[Lit]";
        case EditorIconKind::Transform: return "[Tr]";
        case EditorIconKind::Settings: return "[Set]";
        case EditorIconKind::Play: return "[Play]";
        case EditorIconKind::Stop: return "[Stop]";
        case EditorIconKind::Translate: return "[Move]";
        case EditorIconKind::Rotate: return "[Rot]";
        case EditorIconKind::Scale: return "[Scl]";
        case EditorIconKind::Unknown:
        case EditorIconKind::Count:
        default: return "[?]";
        }
    }

    const char* EditorIconManager::GetAssetFallbackText(AssetType type) {
        return GetFallbackText(ToIconKind(type));
    }

    int EditorIconManager::GetAtlasIndex(EditorIconKind kind) {
#if defined(_DEBUG)
        return AtlasIndexForKind(kind);
#else
        (void)kind;
        return 10;
#endif
    }

} // namespace HIKARI::EDITOR
