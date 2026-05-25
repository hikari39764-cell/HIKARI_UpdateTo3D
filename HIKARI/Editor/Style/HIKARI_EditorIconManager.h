#pragma once

#include "Assets/HIKARI_AssetTypes.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    enum class EditorIconKind {
        Unknown,
        Folder,
        Texture,
        Model,
        Material,
        Sky,
        Scene,
        Vfx,
        Component,
        System,
        GameObject,
        Camera,
        Light,
        Transform,
    };

    class EditorIconManager {
    public:
        static bool Initialize();
        static void Finalize();

#if defined(_DEBUG)
        static bool DrawIcon(EditorIconKind kind, const ImVec2& size);
        static bool DrawAssetIcon(AssetType type, const ImVec2& size);
#endif
        static const char* GetFallbackText(EditorIconKind kind);
        static const char* GetAssetFallbackText(AssetType type);

    private:
        static int GetAtlasIndex(EditorIconKind kind);
    };

} // namespace HIKARI::EDITOR
