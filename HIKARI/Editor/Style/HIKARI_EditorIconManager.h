#pragma once

#include "Assets/HIKARI_AssetTypes.h"

#if defined(HIKARI_WITH_EDITOR)
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
        Sequence,
        File,
        Music,
        Component,
        System,
        GameObject,
        Camera,
        Light,
        Transform,
        Settings,
        Play,
        Stop,
        Translate,
        Rotate,
        Scale,
        Count,
    };

    class EditorIconManager {
    public:
        static bool Initialize();
        static void Finalize();

#if defined(HIKARI_WITH_EDITOR)
        static bool DrawIcon(EditorIconKind kind, const ImVec2& size);
        static bool DrawAssetIcon(AssetType type, const ImVec2& size);
        static bool IconButton(
            EditorIconKind kind,
            const char* id,
            const ImVec2& size,
            bool selected = false,
            const char* tooltip = nullptr);
#endif
        static const char* GetFallbackText(EditorIconKind kind);
        static const char* GetAssetFallbackText(AssetType type);

    private:
        static int GetAtlasIndex(EditorIconKind kind);
    };

} // namespace HIKARI::EDITOR
