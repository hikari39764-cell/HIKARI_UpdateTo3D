#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_SceneScanFxComponent.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

    class GameObject;
    class ModelComponent;

    class SceneScanFxSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "SceneScanFxSystem"; }

        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        struct TargetSnapshot {
            SceneObjectId objectId{};
            ModelComponent* model = nullptr;
            std::string profileId{};
            std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> values{};
            bool initialized = false;
        };

        struct RuntimeState {
            bool active = false;
            float time = 0.0f;
            MATH::Vec3 origin{};
            SceneScanFxRuntimeStats stats{};
            std::vector<TargetSnapshot> targets{};
        };

        RuntimeState& ResolveState(uint64_t key);
        ModelComponent* ResolveTargetModel(World& world, const TargetSnapshot& snapshot) const;
        void StartScan(World& world, GameObject& owner, SceneScanFxComponent& component, RuntimeState& state);
        void StopScan(World& world, SceneScanFxComponent& component, RuntimeState& state);
        void UpdateActiveScan(World& world, SceneScanFxComponent& component, RuntimeState& state, float dt);

        std::unordered_map<uint64_t, RuntimeState> states_;
    };

} // namespace HIKARI
