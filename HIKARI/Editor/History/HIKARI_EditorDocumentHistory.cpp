#include "Editor/History/HIKARI_EditorDocumentHistory.h"

#include <utility>

namespace HIKARI::EDITOR {

    namespace {
        constexpr size_t kMaximumHistoryEntries = 128;
    }

    void EditorDocumentHistory::Reset(
        uint64_t documentRevision,
        bool initiallyDirty) {

        entries_.clear();
        cursor_ = 0;
        savedCursor_ = initiallyDirty
            ? std::optional<size_t>{}
            : std::optional<size_t>{ 0 };
        documentRevision_ = documentRevision;
        openMergeGroup_ = 0;
        initialDirty_ = initiallyDirty;
        bound_ = true;
    }

    bool EditorDocumentHistory::SyncDocumentRevision(
        uint64_t documentRevision,
        bool initiallyDirty) {

        if (bound_ && documentRevision_ == documentRevision) {
            return false;
        }
        Reset(documentRevision, initiallyDirty);
        return true;
    }

    void EditorDocumentHistory::RecordApplied(
        std::unique_ptr<IEditorDocumentCommand> command,
        uint64_t mergeGroup) {

        if (!command) {
            return;
        }
        if (cursor_ < entries_.size()) {
            if (savedCursor_ && *savedCursor_ > cursor_) {
                savedCursor_.reset();
            }
            entries_.erase(entries_.begin() + cursor_, entries_.end());
        }

        const bool mayMerge = mergeGroup != 0 &&
            openMergeGroup_ == mergeGroup &&
            cursor_ > 0 && cursor_ == entries_.size() &&
            entries_.back().mergeGroup == mergeGroup;
        if (mayMerge &&
            entries_.back().command->TryMergeApplied(*command)) {
            return;
        }

        entries_.push_back({ std::move(command), mergeGroup });
        cursor_ = entries_.size();
        openMergeGroup_ = mergeGroup;
        if (entries_.size() > kMaximumHistoryEntries) {
            const size_t overflow =
                entries_.size() - kMaximumHistoryEntries;
            entries_.erase(entries_.begin(), entries_.begin() + overflow);
            cursor_ -= overflow;
            if (savedCursor_) {
                if (*savedCursor_ < overflow) {
                    savedCursor_.reset();
                } else {
                    *savedCursor_ -= overflow;
                }
            }
        }
    }

    void EditorDocumentHistory::SealMerge() noexcept {
        openMergeGroup_ = 0;
    }

    EditorHistoryResult EditorDocumentHistory::Undo(
        SceneDocument& document) {

        SealMerge();
        if (!CanUndo()) {
            return {};
        }
        Entry& entry = entries_[cursor_ - 1];
        entry.command->Undo(document);
        --cursor_;
        return {
            entry.command->GetImpact(),
            entry.command->GetLabel(),
            true
        };
    }

    EditorHistoryResult EditorDocumentHistory::Redo(
        SceneDocument& document) {

        SealMerge();
        if (!CanRedo()) {
            return {};
        }
        Entry& entry = entries_[cursor_];
        entry.command->Redo(document);
        ++cursor_;
        return {
            entry.command->GetImpact(),
            entry.command->GetLabel(),
            true
        };
    }

    void EditorDocumentHistory::MarkSaved() noexcept {
        SealMerge();
        savedCursor_ = cursor_;
        initialDirty_ = false;
    }

    bool EditorDocumentHistory::IsDirty() const noexcept {
        return initialDirty_ || !savedCursor_ || cursor_ != *savedCursor_;
    }

    bool EditorDocumentHistory::CanUndo() const noexcept {
        return cursor_ > 0;
    }

    bool EditorDocumentHistory::CanRedo() const noexcept {
        return cursor_ < entries_.size();
    }

    const std::string* EditorDocumentHistory::GetUndoLabel() const noexcept {
        return CanUndo()
            ? &entries_[cursor_ - 1].command->GetLabel()
            : nullptr;
    }

    const std::string* EditorDocumentHistory::GetRedoLabel() const noexcept {
        return CanRedo()
            ? &entries_[cursor_].command->GetLabel()
            : nullptr;
    }

    uint64_t EditorDocumentHistory::GetDocumentRevision() const noexcept {
        return documentRevision_;
    }

} // namespace HIKARI::EDITOR
