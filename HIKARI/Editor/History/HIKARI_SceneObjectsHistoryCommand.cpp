#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"

#include <utility>

namespace HIKARI::EDITOR {
    namespace {
        class SceneObjectsHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            SceneObjectsHistoryCommand(
                std::string label,
                std::vector<SceneObjectData> beforeObjects,
                std::vector<SceneObjectData> afterObjects,
                SceneCameraSettings beforeCamera,
                SceneCameraSettings afterCamera)
                : label_(std::move(label)),
                  beforeObjects_(std::move(beforeObjects)),
                  afterObjects_(std::move(afterObjects)),
                  beforeCamera_(std::move(beforeCamera)),
                  afterCamera_(std::move(afterCamera)) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return EditorDocumentImpact::RuntimeWorld;
            }

            void Undo(SceneDocument& document) const override {
                document.objects = beforeObjects_;
                document.camera = beforeCamera_;
            }

            void Redo(SceneDocument& document) const override {
                document.objects = afterObjects_;
                document.camera = afterCamera_;
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand&) override {
                return false;
            }

        private:
            std::string label_{};
            std::vector<SceneObjectData> beforeObjects_{};
            std::vector<SceneObjectData> afterObjects_{};
            SceneCameraSettings beforeCamera_{};
            SceneCameraSettings afterCamera_{};
        };
    }

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneObjectsHistoryCommand(
            std::string label,
            std::vector<SceneObjectData> beforeObjects,
            std::vector<SceneObjectData> afterObjects,
            SceneCameraSettings beforeCamera,
            SceneCameraSettings afterCamera) {

        return std::make_unique<SceneObjectsHistoryCommand>(
            std::move(label),
            std::move(beforeObjects),
            std::move(afterObjects),
            std::move(beforeCamera),
            std::move(afterCamera));
    }

} // namespace HIKARI::EDITOR
