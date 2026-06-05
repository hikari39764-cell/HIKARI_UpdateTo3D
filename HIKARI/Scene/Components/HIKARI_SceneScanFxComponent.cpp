#include "HIKARI_SceneScanFxComponent.h"

#include <algorithm>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "HIKARI_Input.h"

namespace HIKARI {

    namespace {
        void ClampSettings(SceneScanFxComponent& component) {
            (void)component;
        }

        MATH::Vec4 ReadVec4(const nlohmann::json& node, const MATH::Vec4& fallback) {
            MATH::Vec4 value = fallback;
            if (node.is_array() && node.size() >= 4) {
                value.x = node[0].is_number() ? node[0].get<float>() : value.x;
                value.y = node[1].is_number() ? node[1].get<float>() : value.y;
                value.z = node[2].is_number() ? node[2].get<float>() : value.z;
                value.w = node[3].is_number() ? node[3].get<float>() : value.w;
            } else if (node.is_object()) {
                value.x = node.value("x", value.x);
                value.y = node.value("y", value.y);
                value.z = node.value("z", value.z);
                value.w = node.value("w", value.w);
            }
            return value;
        }
    }

    void SceneScanFxComponent::Update(float dt) {
        (void)dt;

        if (!enabled_) {
            RequestStop();
            autoPlayConsumed_ = false;
            return;
        }

        if (!triggerActionName_.empty() && HINPUT::IsPressed(triggerActionName_)) {
            RequestPlay();
        }

        if (autoPlay_) {
            if (!autoPlayConsumed_) {
                RequestPlay();
                autoPlayConsumed_ = true;
            }
        } else {
            autoPlayConsumed_ = false;
        }
    }

    void SceneScanFxComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["triggerActionName"] = triggerActionName_;
        out["autoPlay"] = autoPlay_;
        out["skipSourceObject"] = skipSourceObject_;
        out["overrideExistingFx"] = overrideExistingFx_;
        out["restoreOnStop"] = restoreOnStop_;
        out["sourceObjectId"] = sourceObjectId_.value;
        out["radius"] = radius_;
        out["speed"] = speed_;
        out["bandWidth"] = bandWidth_;
        out["triangleCellSize"] = triangleCellSize_;
        out["triangleLineWidth"] = triangleLineWidth_;
        out["noiseScale"] = noiseScale_;
        out["flickerStrength"] = flickerStrength_;
        out["intensity"] = intensity_;
        out["color"] = nlohmann::json::array({ color_.x, color_.y, color_.z, color_.w });
        out["afterglowStrength"] = afterglowStrength_;
        out["frontLineStrength"] = frontLineStrength_;
        out["geometryEdgeStrength"] = geometryEdgeStrength_;
        out["distortionStrength"] = distortionStrength_;
    }

    void SceneScanFxComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        triggerActionName_ = in.value("triggerActionName", triggerActionName_);
        autoPlay_ = in.value("autoPlay", autoPlay_);
        skipSourceObject_ = in.value("skipSourceObject", skipSourceObject_);
        overrideExistingFx_ = in.value("overrideExistingFx", overrideExistingFx_);
        restoreOnStop_ = in.value("restoreOnStop", restoreOnStop_);
        sourceObjectId_.value = in.value("sourceObjectId", sourceObjectId_.value);
        radius_ = in.value("radius", radius_);
        speed_ = in.value("speed", speed_);
        bandWidth_ = in.value("bandWidth", bandWidth_);
        triangleCellSize_ = in.value("triangleCellSize", triangleCellSize_);
        triangleLineWidth_ = in.value("triangleLineWidth", triangleLineWidth_);
        noiseScale_ = in.value("noiseScale", noiseScale_);
        flickerStrength_ = in.value("flickerStrength", flickerStrength_);
        intensity_ = in.value("intensity", intensity_);
        if (in.contains("color")) {
            color_ = ReadVec4(in["color"], color_);
        }
        afterglowStrength_ = in.value("afterglowStrength", afterglowStrength_);
        frontLineStrength_ = in.value("frontLineStrength", frontLineStrength_);
        geometryEdgeStrength_ = in.value("geometryEdgeStrength", geometryEdgeStrength_);
        distortionStrength_ = in.value("distortionStrength", distortionStrength_);
        ClampSettings(*this);
    }

    void SceneScanFxComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.String("Trigger Action", triggerActionName_);
        builder.Bool("Auto Play", autoPlay_);
        builder.Bool("Skip Source Object", skipSourceObject_);
        builder.Bool("Override Existing FX", overrideExistingFx_);
        builder.Bool("Restore On Stop", restoreOnStop_);

        int sourceId = static_cast<int>(sourceObjectId_.value);
        if (builder.Int("Source ObjectId", sourceId)) {
            sourceObjectId_.value = static_cast<uint64_t>((std::max)(0, sourceId));
        }

        builder.Float("Radius", radius_);
        builder.Float("Speed", speed_);
        builder.Float("Band Width", bandWidth_);
        builder.Float("Triangle Cell Size", triangleCellSize_);
        builder.Float("Triangle Line Width", triangleLineWidth_);
        builder.Float("Noise Scale", noiseScale_);
        builder.Float("Flicker Strength", flickerStrength_);
        builder.Float("Intensity", intensity_);
        builder.Float("Color R", color_.x);
        builder.Float("Color G", color_.y);
        builder.Float("Color B", color_.z);
        builder.Float("Color A", color_.w);
        builder.Float("Afterglow Strength", afterglowStrength_);
        builder.Float("Front Line Strength", frontLineStrength_);
        builder.Float("Geometry Edge Strength", geometryEdgeStrength_);
        builder.Float("Distortion Strength", distortionStrength_);

        bool play = false;
        bool stop = false;
        if (builder.Bool("Play Scene Scan", play) && play) {
            RequestPlay();
        }
        if (builder.Bool("Stop Scene Scan", stop) && stop) {
            RequestStop();
        }
    }

    bool SceneScanFxComponent::IsEnabled() const { return enabled_; }
    const std::string& SceneScanFxComponent::GetTriggerActionName() const { return triggerActionName_; }
    SceneObjectId SceneScanFxComponent::GetSourceObjectId() const { return sourceObjectId_; }
    bool SceneScanFxComponent::GetAutoPlay() const { return autoPlay_; }
    bool SceneScanFxComponent::GetSkipSourceObject() const { return skipSourceObject_; }
    bool SceneScanFxComponent::GetOverrideExistingFx() const { return overrideExistingFx_; }
    bool SceneScanFxComponent::GetRestoreOnStop() const { return restoreOnStop_; }
    float SceneScanFxComponent::GetRadius() const { return (std::max)(radius_, 0.01f); }
    float SceneScanFxComponent::GetSpeed() const { return (std::max)(speed_, 0.01f); }
    float SceneScanFxComponent::GetBandWidth() const { return (std::max)(bandWidth_, 0.01f); }
    float SceneScanFxComponent::GetTriangleCellSize() const { return (std::max)(triangleCellSize_, 0.01f); }
    float SceneScanFxComponent::GetTriangleLineWidth() const { return (std::clamp)(triangleLineWidth_, 0.001f, 0.45f); }
    float SceneScanFxComponent::GetNoiseScale() const { return (std::max)(noiseScale_, 0.001f); }
    float SceneScanFxComponent::GetFlickerStrength() const { return (std::clamp)(flickerStrength_, 0.0f, 1.0f); }
    float SceneScanFxComponent::GetIntensity() const { return (std::max)(intensity_, 0.0f); }
    const MATH::Vec4& SceneScanFxComponent::GetColor() const { return color_; }
    float SceneScanFxComponent::GetAfterglowStrength() const { return (std::max)(afterglowStrength_, 0.0f); }
    float SceneScanFxComponent::GetFrontLineStrength() const { return (std::max)(frontLineStrength_, 0.0f); }
    float SceneScanFxComponent::GetGeometryEdgeStrength() const { return (std::max)(geometryEdgeStrength_, 0.0f); }
    float SceneScanFxComponent::GetDistortionStrength() const { return (std::max)(distortionStrength_, 0.0f); }

    void SceneScanFxComponent::RequestPlay() {
        playRequested_ = true;
        stopRequested_ = false;
    }

    void SceneScanFxComponent::RequestStop() {
        stopRequested_ = true;
        playRequested_ = false;
    }

    bool SceneScanFxComponent::ConsumePlayRequest() {
        const bool requested = playRequested_;
        playRequested_ = false;
        return requested;
    }

    bool SceneScanFxComponent::ConsumeStopRequest() {
        const bool requested = stopRequested_;
        stopRequested_ = false;
        return requested;
    }

    void SceneScanFxComponent::SetRuntimeState(bool active, float time, const SceneScanFxRuntimeStats& stats) {
        runtimeActive_ = active;
        runtimeTime_ = time;
        runtimeStats_ = stats;
    }

} // namespace HIKARI
