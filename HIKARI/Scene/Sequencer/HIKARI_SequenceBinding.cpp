#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace HIKARI::SEQUENCER {

    void NormalizeSequenceBindings(SequenceBindingTable& bindings) {
        std::unordered_set<uint64_t> usedIds{};
        SequenceBindingId nextId = AllocateSequenceBindingId(bindings);
        for (SequenceBinding& binding : bindings) {
            if (!binding.id.IsValid() ||
                !usedIds.insert(binding.id.value).second) {
                while (!usedIds.insert(nextId.value).second) {
                    ++nextId.value;
                    if (nextId.value == 0) {
                        nextId.value = 1;
                    }
                }
                binding.id = nextId;
                ++nextId.value;
                if (nextId.value == 0) {
                    nextId.value = 1;
                }
            }
            if (binding.name.empty()) {
                binding.name = "Binding " +
                    std::to_string(binding.id.value);
            }
        }
    }

    SequenceBindingId AllocateSequenceBindingId(
        const SequenceBindingTable& bindings) {

        SequenceBindingId nextId{ 1 };
        for (const SequenceBinding& binding : bindings) {
            if (binding.id.value < nextId.value) {
                continue;
            }
            if (binding.id.value ==
                (std::numeric_limits<uint64_t>::max)()) {
                nextId.value = 1;
                break;
            }
            nextId.value = binding.id.value + 1;
        }
        while (FindSequenceBinding(bindings, nextId) != nullptr) {
            ++nextId.value;
            if (nextId.value == 0) {
                nextId.value = 1;
            }
        }
        return nextId;
    }

    SequenceBindingId FindOrCreateSceneObjectBinding(
        SequenceBindingTable& bindings,
        SceneObjectId sceneObjectId,
        const std::string& name) {

        if (sceneObjectId.value == 0) {
            return {};
        }
        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [sceneObjectId](const SequenceBinding& binding) {
                return binding.targetKind ==
                        SequenceBindingTargetKind::SceneObject &&
                    binding.sceneObjectId == sceneObjectId;
            });
        if (found != bindings.end()) {
            return found->id;
        }

        SequenceBinding binding{};
        binding.id = AllocateSequenceBindingId(bindings);
        binding.name = name;
        binding.sceneObjectId = sceneObjectId;
        bindings.push_back(binding);
        NormalizeSequenceBindings(bindings);
        return binding.id;
    }

    SequenceBinding* FindSequenceBinding(
        SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [bindingId](const SequenceBinding& binding) {
                return binding.id == bindingId;
            });
        return found != bindings.end() ? &*found : nullptr;
    }

    const SequenceBinding* FindSequenceBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [bindingId](const SequenceBinding& binding) {
                return binding.id == bindingId;
            });
        return found != bindings.end() ? &*found : nullptr;
    }

    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        SceneObjectId& outSceneObjectId) noexcept {

        const SequenceBinding* binding =
            FindSequenceBinding(bindings, bindingId);
        if (binding == nullptr ||
            binding->targetKind != SequenceBindingTargetKind::SceneObject ||
            binding->sceneObjectId.value == 0) {
            outSceneObjectId = {};
            return false;
        }
        outSceneObjectId = binding->sceneObjectId;
        return true;
    }

} // namespace HIKARI::SEQUENCER
