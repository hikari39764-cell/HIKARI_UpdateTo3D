#pragma once

#include <string>

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct SceneScanFxRuntimeStats {
        uint32_t affectedModelCount = 0;
        uint32_t skippedModelCount = 0;
    };

    class SceneScanFxComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "SceneScanFxComponent"; }

        void Update(float dt) override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const;
        const std::string& GetTriggerActionName() const;
        SceneObjectId GetSourceObjectId() const;
        bool GetAutoPlay() const;
        bool GetSkipSourceObject() const;
        bool GetOverrideExistingFx() const;
        bool GetRestoreOnStop() const;
        float GetRadius() const;
        float GetSpeed() const;
        float GetBandWidth() const;
        float GetTriangleCellSize() const;
        float GetTriangleLineWidth() const;
        float GetNoiseScale() const;
        float GetFlickerStrength() const;
        float GetIntensity() const;
        const MATH::Vec4& GetColor() const;
        float GetAfterglowStrength() const;
        float GetFrontLineStrength() const;
        float GetGeometryEdgeStrength() const;
        float GetDistortionStrength() const;

        void RequestPlay();
        void RequestStop();
        bool ConsumePlayRequest();
        bool ConsumeStopRequest();
        void SetRuntimeState(bool active, float time, const SceneScanFxRuntimeStats& stats);

    private:
        bool enabled_ = true;
        std::string triggerActionName_ = "PlaySceneScan";
        bool autoPlay_ = false;
        bool skipSourceObject_ = true;
        bool overrideExistingFx_ = false;
        bool restoreOnStop_ = true;
        SceneObjectId sourceObjectId_{};

        float radius_ = 28.0f;
        float speed_ = 16.0f;
        float bandWidth_ = 4.4f;
        float triangleCellSize_ = 2.85f;
        float triangleLineWidth_ = 0.15f;
        float noiseScale_ = 1.35f;
        float flickerStrength_ = 0.68f;
        float intensity_ = 4.6f;
        MATH::Vec4 color_{ 0.1f, 1.0f, 0.95f, 0.94f };
        float afterglowStrength_ = 0.72f;
        float frontLineStrength_ = 1.35f;
        float geometryEdgeStrength_ = 0.85f;
        float distortionStrength_ = 0.004f;

        bool playRequested_ = false;
        bool stopRequested_ = false;
        bool autoPlayConsumed_ = false;
        bool runtimeActive_ = false;
        float runtimeTime_ = 0.0f;
        SceneScanFxRuntimeStats runtimeStats_{};
    };

} // namespace HIKARI
