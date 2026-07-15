#pragma once

#include <memory>
#include <string_view>

namespace HIKARI {

    class DocumentSceneBase;
    struct EditorContext;

    namespace EDITOR {

        struct EditorToolContext {
            DocumentSceneBase& scene;
            EditorContext& editorContext;
        };

        class IEditorTool {
        public:
            virtual ~IEditorTool() = default;
            virtual void Draw(EditorToolContext& context, bool& open) = 0;
        };

        using EditorToolFactoryFn = std::unique_ptr<IEditorTool>(*)();

        struct EditorToolDescriptor {
            std::string_view toolId{};
            std::string_view displayName{};
            std::string_view menuGroup{};
            bool defaultOpen = false;
            EditorToolFactoryFn factory = nullptr;
        };

    } // namespace EDITOR

} // namespace HIKARI
