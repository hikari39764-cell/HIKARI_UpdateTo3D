#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI::SEQUENCER {

    struct SequenceBindingId {
        uint64_t value = 0;

        bool IsValid() const noexcept {
            return value != 0;
        }

        bool operator==(const SequenceBindingId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

    enum class SequenceBindingTargetKind : uint8_t {
        SceneObject,
    };

    struct SequenceBinding {
        SequenceBindingId id{};
        std::string name{};
        SequenceBindingTargetKind targetKind =
            SequenceBindingTargetKind::SceneObject;
        SceneObjectId sceneObjectId{};
    };

    using SequenceBindingTable = std::vector<SequenceBinding>;

    void NormalizeSequenceBindings(SequenceBindingTable& bindings);
    SequenceBindingId AllocateSequenceBindingId(
        const SequenceBindingTable& bindings);
    SequenceBindingId FindOrCreateSceneObjectBinding(
        SequenceBindingTable& bindings,
        SceneObjectId sceneObjectId,
        const std::string& name = {});
    SequenceBinding* FindSequenceBinding(
        SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept;
    const SequenceBinding* FindSequenceBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept;
    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        SceneObjectId& outSceneObjectId) noexcept;

} // namespace HIKARI::SEQUENCER
