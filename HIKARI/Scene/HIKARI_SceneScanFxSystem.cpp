#include "Scene/HIKARI_SceneScanFxSystem.h"

#include <algorithm>

#include "Core/HIKARI_FrameContext.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_SceneScanFxComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        constexpr const char* kSceneScanFxProfileId = "scene_scan_fx";

        uint64_t MakeStateKey(GameObject& owner) {
            const SceneObjectId id = owner.GetDocumentId();
            if (id.value != 0) {
                return id.value;
            }
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&owner));
        }

        GameObject* FindObjectById(World& world, SceneObjectId id) {
            if (id.value == 0) {
                return nullptr;
            }
            for (const auto& object : world.GetObjects()) {
                if (object && object->GetDocumentId() == id) {
                    return object.get();
                }
            }
            return nullptr;
        }

        GameObject* ResolveSourceObject(World& world, GameObject& owner, const SceneScanFxComponent& component) {
            if (GameObject* source = FindObjectById(world, component.GetSourceObjectId())) {
                return source;
            }
            return &owner;
        }

        bool IsSourceObject(const GameObject& candidate, const GameObject* source) {
            if (!source) {
                return false;
            }
            const SceneObjectId lhs = candidate.GetDocumentId();
            const SceneObjectId rhs = source->GetDocumentId();
            if (lhs.value != 0 && rhs.value != 0) {
                return lhs == rhs;
            }
            return &candidate == source;
        }

        bool ShouldApplyToModel(const GameObject& object, const ModelComponent& model, const GameObject* source, const SceneScanFxComponent& component) {
            if (!model.IsVisible()) {
                return false;
            }
            if (component.GetSkipSourceObject() && IsSourceObject(object, source)) {
                return false;
            }
            const std::string& profileId = model.GetMaterialFxProfileId();
            if (!component.GetOverrideExistingFx() && !profileId.empty() && profileId != kSceneScanFxProfileId) {
                return false;
            }
            return true;
        }

        void ApplyScanParams(ModelComponent& model, const SceneScanFxComponent& component, const MATH::Vec3& origin, float time) {
            if (model.GetMaterialFxProfileId() != kSceneScanFxProfileId) {
                model.SetMaterialFxProfileId(kSceneScanFxProfileId);
            }

            const MATH::Vec4& color = component.GetColor();
            model.SetMaterialFxFloat4("scanOriginTime", DirectX::XMFLOAT4(origin.x, origin.y, origin.z, time));
            model.SetMaterialFxFloat4("scanParams", DirectX::XMFLOAT4(
                component.GetRadius(),
                component.GetBandWidth(),
                component.GetSpeed(),
                component.GetIntensity()));
            model.SetMaterialFxFloat4("scanColor", DirectX::XMFLOAT4(color.x, color.y, color.z, color.w));
            model.SetMaterialFxFloat4("triangleParams", DirectX::XMFLOAT4(
                component.GetTriangleCellSize(),
                component.GetTriangleLineWidth(),
                component.GetNoiseScale(),
                component.GetFlickerStrength()));
            model.SetMaterialFxFloat4("scanEnhanceParams", DirectX::XMFLOAT4(
                component.GetAfterglowStrength(),
                component.GetFrontLineStrength(),
                component.GetGeometryEdgeStrength(),
                component.GetDistortionStrength()));
        }
    }

    void SceneScanFxSystem::OnWorldDetached(World& world) {
        for (auto& [_, state] : states_) {
            SceneScanFxComponent dummy{};
            StopScan(world, dummy, state);
        }
        states_.clear();
    }

    void SceneScanFxSystem::Update(World& world, const FrameContext& frame) {
        world.ForEachObjectWith<SceneScanFxComponent>(
            [&](GameObject& owner, SceneScanFxComponent& component) {
                RuntimeState& state = ResolveState(MakeStateKey(owner));

                if (!component.IsEnabled()) {
                    if (state.active) {
                        StopScan(world, component, state);
                    }
                    component.ConsumePlayRequest();
                    component.ConsumeStopRequest();
                    component.SetRuntimeState(false, 0.0f, state.stats);
                    return;
                }

                if (component.ConsumeStopRequest()) {
                    StopScan(world, component, state);
                }

                if (component.ConsumePlayRequest()) {
                    StopScan(world, component, state);
                    StartScan(world, owner, component, state);
                }

                if (state.active) {
                    UpdateActiveScan(world, component, state, frame.gameDt);
                }

                component.SetRuntimeState(state.active, state.time, state.stats);
            });
    }

    SceneScanFxSystem::RuntimeState& SceneScanFxSystem::ResolveState(uint64_t key) {
        return states_[key];
    }

    ModelComponent* SceneScanFxSystem::ResolveTargetModel(World& world, const TargetSnapshot& snapshot) const {
        if (snapshot.objectId.value != 0) {
            GameObject* object = FindObjectById(world, snapshot.objectId);
            return object ? object->GetComponent<ModelComponent>() : nullptr;
        }
        return snapshot.model;
    }

    void SceneScanFxSystem::StartScan(World& world, GameObject& owner, SceneScanFxComponent& component, RuntimeState& state) {
        GameObject* source = ResolveSourceObject(world, owner, component);
        state = RuntimeState{};
        state.active = true;
        state.time = 0.0f;
        state.origin = source ? source->Transform().position : owner.Transform().position;

        world.ForEachObjectWith<ModelComponent>(
            [&](GameObject& object, ModelComponent& model) {
                if (!ShouldApplyToModel(object, model, source, component)) {
                    ++state.stats.skippedModelCount;
                    return;
                }

                TargetSnapshot snapshot{};
                snapshot.objectId = object.GetDocumentId();
                snapshot.model = &model;
                snapshot.profileId = model.GetMaterialFxProfileId();
                snapshot.initialized = model.AreMaterialFxValuesInitialized();
                const auto& values = model.GetMaterialFxParamValues();
                for (size_t i = 0; i < snapshot.values.size(); ++i) {
                    snapshot.values[i] = values[i];
                }
                state.targets.push_back(std::move(snapshot));
                ApplyScanParams(model, component, state.origin, state.time);
                ++state.stats.affectedModelCount;
            });
    }

    void SceneScanFxSystem::StopScan(World& world, SceneScanFxComponent& component, RuntimeState& state) {
        if (!state.active && state.targets.empty()) {
            state = RuntimeState{};
            return;
        }

        if (component.GetRestoreOnStop()) {
            for (const TargetSnapshot& snapshot : state.targets) {
                ModelComponent* model = ResolveTargetModel(world, snapshot);
                if (!model) {
                    continue;
                }
                model->SetMaterialFxProfileId(snapshot.profileId);
                if (snapshot.initialized) {
                    DirectX::XMFLOAT4 values[VFX::kMaterialFxUserCount]{};
                    for (size_t i = 0; i < snapshot.values.size(); ++i) {
                        values[i] = snapshot.values[i];
                    }
                    model->SetMaterialFxParamValues(values, true);
                }
            }
        }

        state = RuntimeState{};
    }

    void SceneScanFxSystem::UpdateActiveScan(World& world, SceneScanFxComponent& component, RuntimeState& state, float dt) {
        state.time += (std::max)(0.0f, dt);

        for (const TargetSnapshot& snapshot : state.targets) {
            ModelComponent* model = ResolveTargetModel(world, snapshot);
            if (!model) {
                continue;
            }
            ApplyScanParams(*model, component, state.origin, state.time);
        }

        const float finishTime = (component.GetRadius() + component.GetBandWidth() * (2.0f + component.GetAfterglowStrength() * 3.0f)) / component.GetSpeed();
        if (state.time >= finishTime) {
            StopScan(world, component, state);
        }
    }

} // namespace HIKARI
