#include "HIKARI_SceneSerializer.h"

#include <fstream>
#include <algorithm>
#include <array>
#include <utility>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_DefaultSceneSystems.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    using nlohmann::json;

    namespace {
        json ToVec3(const MATH::Vec3& v) {
            return json::array({ v.x, v.y, v.z });
        }

        json ToFloat4(const DirectX::XMFLOAT4& v) {
            return json::array({ v.x, v.y, v.z, v.w });
        }

        DirectX::XMFLOAT4 FromFloat4(const json& in, const DirectX::XMFLOAT4& fallback) {
            if (!in.is_array() || in.size() < 4) {
                return fallback;
            }
            return {
                in[0].is_number() ? in[0].get<float>() : fallback.x,
                in[1].is_number() ? in[1].get<float>() : fallback.y,
                in[2].is_number() ? in[2].get<float>() : fallback.z,
                in[3].is_number() ? in[3].get<float>() : fallback.w
            };
        }

        const char* ToString(SkyMode mode) {
            switch (mode) {
            case SkyMode::None: return "None";
            case SkyMode::Cubemap: return "Cubemap";
            case SkyMode::Texture2D: return "Texture2D";
            case SkyMode::Gradient:
            default: return "Gradient";
            }
        }

        SkyMode ParseSkyMode(const json& in, SkyMode fallback) {
            if (in.is_number_integer()) {
                const int value = in.get<int>();
                if (value >= static_cast<int>(SkyMode::None) && value <= static_cast<int>(SkyMode::Texture2D)) {
                    return static_cast<SkyMode>(value);
                }
                return fallback;
            }
            if (!in.is_string()) {
                return fallback;
            }
            const std::string value = in.get<std::string>();
            if (value == "None") return SkyMode::None;
            if (value == "Gradient") return SkyMode::Gradient;
            if (value == "Cubemap") return SkyMode::Cubemap;
            if (value == "Texture2D") return SkyMode::Texture2D;
            return fallback;
        }

        const char* ToString(ReflectionProbeInfluenceShape shape) {
            switch (shape) {
            case ReflectionProbeInfluenceShape::Box: return "Box";
            case ReflectionProbeInfluenceShape::Sphere:
            default: return "Sphere";
            }
        }

        const char* ToString(ReflectionProbeProjectionShape shape) {
            switch (shape) {
            case ReflectionProbeProjectionShape::Box: return "Box";
            case ReflectionProbeProjectionShape::Infinite:
            default: return "Infinite";
            }
        }

        ReflectionProbeInfluenceShape ParseReflectionProbeInfluenceShape(
            const json& in,
            ReflectionProbeInfluenceShape fallback) {

            if (in.is_number_integer()) {
                const int value = in.get<int>();
                return value == 1 ? ReflectionProbeInfluenceShape::Box : fallback;
            }
            if (!in.is_string()) {
                return fallback;
            }
            const std::string value = in.get<std::string>();
            if (value == "Box") return ReflectionProbeInfluenceShape::Box;
            if (value == "Sphere") return ReflectionProbeInfluenceShape::Sphere;
            return fallback;
        }

        ReflectionProbeProjectionShape ParseReflectionProbeProjectionShape(
            const json& in,
            ReflectionProbeProjectionShape fallback) {

            if (in.is_number_integer()) {
                const int value = in.get<int>();
                return value == 1 ? ReflectionProbeProjectionShape::Box : fallback;
            }
            if (!in.is_string()) {
                return fallback;
            }
            const std::string value = in.get<std::string>();
            if (value == "Box") return ReflectionProbeProjectionShape::Box;
            if (value == "Infinite") return ReflectionProbeProjectionShape::Infinite;
            return fallback;
        }

        const char* ToString(SsaoMode mode) {
            switch (mode) {
            case SsaoMode::Reference: return "Reference";
            case SsaoMode::OptimizedHigh: return "OptimizedHigh";
            case SsaoMode::Balanced: return "Balanced";
            case SsaoMode::Off:
            default: return "Off";
            }
        }

        SsaoMode ParseSsaoMode(const json& in, SsaoMode fallback) {
            if (in.is_number_integer()) {
                const int value = in.get<int>();
                if (value >= static_cast<int>(SsaoMode::Off) &&
                    value <= static_cast<int>(SsaoMode::Balanced)) {
                    return static_cast<SsaoMode>(value);
                }
                return fallback;
            }
            if (!in.is_string()) {
                return fallback;
            }
            const std::string value = in.get<std::string>();
            if (value == "Off") return SsaoMode::Off;
            if (value == "Reference") return SsaoMode::Reference;
            if (value == "OptimizedHigh") return SsaoMode::OptimizedHigh;
            if (value == "Balanced") return SsaoMode::Balanced;
            return fallback;
        }

        MATH::Vec3 ReflectionProbeRadiusBoxSize(float radius) {
            const float diameter = std::max(0.01f, radius * 2.0f);
            return { diameter, diameter, diameter };
        }

        void SerializeEnvironment(const SceneEnvironment& environment, json& out) {
            out["ambient"]["color"] = ToVec3(environment.ambient.color);
            out["ambient"]["intensity"] = environment.ambient.intensity;
            out["ambient"]["useSkyColor"] = environment.ambient.useSkyColor;
            out["ambient"]["skyBlend"] = environment.ambient.skyBlend;

            out["directional"]["enabled"] = environment.directional.enabled;
            out["directional"]["direction"] = ToVec3(environment.directional.direction);
            out["directional"]["intensity"] = environment.directional.intensity;
            out["directional"]["color"] = ToVec3(environment.directional.color);

            out["directionalShadow"]["enabled"] = environment.directionalShadow.enabled;
            out["directionalShadow"]["resolution"] = environment.directionalShadow.resolution;
            out["directionalShadow"]["orthoSize"] = environment.directionalShadow.orthoSize;
            out["directionalShadow"]["nearPlane"] = environment.directionalShadow.nearPlane;
            out["directionalShadow"]["farPlane"] = environment.directionalShadow.farPlane;
            out["directionalShadow"]["depthBias"] = environment.directionalShadow.depthBias;
            out["directionalShadow"]["normalBias"] = environment.directionalShadow.normalBias;
            out["directionalShadow"]["strength"] = environment.directionalShadow.strength;
            out["directionalShadow"]["pcfEnabled"] = environment.directionalShadow.pcfEnabled;
            out["directionalShadow"]["pcfRadius"] = environment.directionalShadow.pcfRadius;
            out["directionalShadow"]["stabilize"] = environment.directionalShadow.stabilize;
            out["directionalShadow"]["showDebugTexture"] = environment.directionalShadow.showDebugTexture;
            out["directionalShadow"]["shadowDistance"] = environment.directionalShadow.shadowDistance;
            out["directionalShadow"]["showDebugFrustum"] = environment.directionalShadow.showDebugFrustum;

            out["pointLights"] = json::array();
            for (const PointLight& pointLight : environment.pointLights) {
                json p;
                p["enabled"] = pointLight.enabled;
                p["position"] = ToVec3(pointLight.position);
                p["range"] = pointLight.range;
                p["color"] = ToVec3(pointLight.color);
                p["intensity"] = pointLight.intensity;
                out["pointLights"].push_back(p);
            }

            out["sky"]["enabled"] = environment.sky.enabled;
            out["sky"]["mode"] = ToString(environment.sky.mode);
            out["sky"]["skyAsset"] = environment.sky.skyAsset;
            out["sky"]["scale"] = environment.sky.scale;
            out["sky"]["yaw"] = environment.sky.yaw;
            out["sky"]["exposure"] = environment.sky.exposure;
            out["sky"]["tint"] = ToVec3(environment.sky.tint);
            out["sky"]["followCamera"] = environment.sky.followCamera;
            out["sky"]["zenithColor"] = ToVec3(environment.sky.zenithColor);
            out["sky"]["horizonColor"] = ToVec3(environment.sky.horizonColor);
            out["sky"]["groundColor"] = ToVec3(environment.sky.groundColor);
            out["sky"]["horizonPower"] = environment.sky.horizonPower;
            out["sky"]["showSunDisk"] = environment.sky.showSunDisk;
            out["sky"]["sunDiskIntensity"] = environment.sky.sunDiskIntensity;
            out["sky"]["sunDiskSize"] = environment.sky.sunDiskSize;
            out["sky"]["ambientFromSky"] = environment.sky.ambientFromSky;
            out["sky"]["reflectionIntensity"] = environment.sky.reflectionIntensity;
            out["sky"]["showDebugTexture"] = environment.sky.showDebugTexture;

            out["reflectionProbe"]["enabled"] = environment.reflectionProbe.enabled;
            out["reflectionProbe"]["sourceCubemapAsset"] = environment.reflectionProbe.sourceCubemapAsset;
            out["reflectionProbe"]["position"] = ToVec3(environment.reflectionProbe.position);
            out["reflectionProbe"]["radius"] = environment.reflectionProbe.radius;
            out["reflectionProbe"]["intensity"] = environment.reflectionProbe.intensity;
            out["reflectionProbe"]["influenceShape"] = ToString(environment.reflectionProbe.influenceShape);
            out["reflectionProbe"]["influenceBoxCenter"] = ToVec3(environment.reflectionProbe.influenceBoxCenter);
            out["reflectionProbe"]["influenceBoxSize"] = ToVec3(environment.reflectionProbe.influenceBoxSize);
            out["reflectionProbe"]["projectionShape"] = ToString(environment.reflectionProbe.projectionShape);
            out["reflectionProbe"]["projectionBoxCenter"] = ToVec3(environment.reflectionProbe.projectionBoxCenter);
            out["reflectionProbe"]["projectionBoxSize"] = ToVec3(environment.reflectionProbe.projectionBoxSize);
            out["reflectionProbe"]["blendDistance"] = environment.reflectionProbe.blendDistance;
            out["reflectionProbe"]["priority"] = environment.reflectionProbe.priority;

            out["ambientOcclusion"]["enabled"] = environment.ambientOcclusion.enabled;
            out["ambientOcclusion"]["mode"] = ToString(environment.ambientOcclusion.mode);
            out["ambientOcclusion"]["radius"] = environment.ambientOcclusion.radius;
            out["ambientOcclusion"]["bias"] = environment.ambientOcclusion.bias;
            out["ambientOcclusion"]["strength"] = environment.ambientOcclusion.strength;
            out["ambientOcclusion"]["power"] = environment.ambientOcclusion.power;
            out["ambientOcclusion"]["diffuseStrength"] = environment.ambientOcclusion.diffuseStrength;
            out["ambientOcclusion"]["specularStrength"] = environment.ambientOcclusion.specularStrength;
            out["ambientOcclusion"]["sampleCount"] = environment.ambientOcclusion.sampleCount;
            out["ambientOcclusion"]["blurIterations"] = environment.ambientOcclusion.blurIterations;

            out["bloom"]["enabled"] = environment.bloom.enabled;
            out["bloom"]["threshold"] = environment.bloom.threshold;
            out["bloom"]["intensity"] = environment.bloom.intensity;
            out["bloom"]["radius"] = environment.bloom.radius;
            out["bloom"]["downsampleCount"] = environment.bloom.downsampleCount;

            out["fog"]["enabled"] = environment.fog.enabled;
            out["fog"]["volumetric"] = environment.fog.volumetric;
            out["fog"]["color"] = ToVec3(environment.fog.color);
            out["fog"]["density"] = environment.fog.density;
            out["fog"]["startDistance"] = environment.fog.startDistance;
            out["fog"]["endDistance"] = environment.fog.endDistance;
            out["fog"]["heightFalloff"] = environment.fog.heightFalloff;
            out["fog"]["anisotropy"] = environment.fog.anisotropy;
            out["fog"]["temporalWeight"] = environment.fog.temporalWeight;
            out["fog"]["useSkyHorizonColor"] = environment.fog.useSkyHorizonColor;

            out["toneMapping"]["enabled"] = environment.toneMapping.enabled;
            out["toneMapping"]["exposure"] = environment.toneMapping.exposure;
            out["toneMapping"]["gamma"] = environment.toneMapping.gamma;
            out["toneMapping"]["mode"] = environment.toneMapping.mode;

            out["specularIntensity"] = environment.specularIntensity;
            out["specularPower"] = environment.specularPower;
            out["showLightDebug"] = environment.showLightDebug;
            out["showPointLightMarkers"] = environment.showPointLightMarkers;
            out["showSkyDebugInfo"] = environment.showSkyDebugInfo;

            out["post"]["enabled"] = environment.post.enabled;
            out["post"]["globalPostProfileId"] = environment.post.globalPostProfileId;
            out["post"]["valuesInitialized"] = environment.post.valuesInitialized;
            out["post"]["paramValues"] = json::array();
            for (const DirectX::XMFLOAT4& value : environment.post.paramValues) {
                out["post"]["paramValues"].push_back(ToFloat4(value));
            }
        }

        void DeserializeEnvironment(const json& in, SceneEnvironment& environment) {
            if (in.contains("ambient")) {
                const json& ambient = in["ambient"];
                environment.ambient.color = JSONREAD::Vec3Or(ambient.value("color", json::array()), environment.ambient.color);
                environment.ambient.intensity = ambient.value("intensity", environment.ambient.intensity);
                environment.ambient.useSkyColor = ambient.value("useSkyColor", environment.ambient.useSkyColor);
                environment.ambient.skyBlend = ambient.value("skyBlend", environment.ambient.skyBlend);
            }

            if (in.contains("directional")) {
                const json& directional = in["directional"];
                environment.directional.enabled = directional.value("enabled", environment.directional.enabled);
                environment.directional.direction = JSONREAD::Vec3Or(directional.value("direction", json::array()), environment.directional.direction);
                environment.directional.intensity = directional.value("intensity", environment.directional.intensity);
                environment.directional.color = JSONREAD::Vec3Or(directional.value("color", json::array()), environment.directional.color);
            }

            if (in.contains("directionalShadow") && in["directionalShadow"].is_object()) {
                const json& shadow = in["directionalShadow"];
                environment.directionalShadow.enabled = shadow.value("enabled", environment.directionalShadow.enabled);
                environment.directionalShadow.resolution = shadow.value("resolution", environment.directionalShadow.resolution);
                environment.directionalShadow.orthoSize = shadow.value("orthoSize", environment.directionalShadow.orthoSize);
                environment.directionalShadow.nearPlane = shadow.value("nearPlane", environment.directionalShadow.nearPlane);
                environment.directionalShadow.farPlane = shadow.value("farPlane", environment.directionalShadow.farPlane);
                environment.directionalShadow.depthBias = shadow.value("depthBias", environment.directionalShadow.depthBias);
                environment.directionalShadow.normalBias = shadow.value("normalBias", environment.directionalShadow.normalBias);
                environment.directionalShadow.strength = shadow.value("strength", environment.directionalShadow.strength);
                environment.directionalShadow.pcfEnabled = shadow.value("pcfEnabled", environment.directionalShadow.pcfEnabled);
                environment.directionalShadow.pcfRadius = shadow.value("pcfRadius", environment.directionalShadow.pcfRadius);
                environment.directionalShadow.stabilize = shadow.value("stabilize", environment.directionalShadow.stabilize);
                environment.directionalShadow.showDebugTexture = shadow.value("showDebugTexture", environment.directionalShadow.showDebugTexture);
                environment.directionalShadow.shadowDistance = shadow.value("shadowDistance", environment.directionalShadow.shadowDistance);
                environment.directionalShadow.showDebugFrustum = shadow.value("showDebugFrustum", environment.directionalShadow.showDebugFrustum);
            }

            if (in.contains("pointLights") && in["pointLights"].is_array()) {
                environment.pointLights.clear();
                for (const json& node : in["pointLights"]) {
                    PointLight pointLight{};
                    pointLight.enabled = node.value("enabled", pointLight.enabled);
                    pointLight.position = JSONREAD::Vec3Or(node.value("position", json::array()), pointLight.position);
                    pointLight.range = node.value("range", pointLight.range);
                    pointLight.color = JSONREAD::Vec3Or(node.value("color", json::array()), pointLight.color);
                    pointLight.intensity = node.value("intensity", pointLight.intensity);
                    environment.pointLights.push_back(pointLight);
                }
            }

            if (in.contains("sky")) {
                const json& sky = in["sky"];
                environment.sky.enabled = sky.value("enabled", environment.sky.enabled);
                environment.sky.mode = ParseSkyMode(sky.value("mode", json{}), environment.sky.mode);
                environment.sky.skyAsset = sky.value("skyAsset", environment.sky.skyAsset);
                environment.sky.scale = sky.value("scale", environment.sky.scale);
                environment.sky.yaw = sky.value("yaw", environment.sky.yaw);
                environment.sky.exposure = sky.value("exposure", environment.sky.exposure);
                environment.sky.tint = JSONREAD::Vec3Or(sky.value("tint", json::array()), environment.sky.tint);
                environment.sky.followCamera = sky.value("followCamera", environment.sky.followCamera);
                environment.sky.zenithColor = JSONREAD::Vec3Or(sky.value("zenithColor", json::array()), environment.sky.zenithColor);
                environment.sky.horizonColor = JSONREAD::Vec3Or(sky.value("horizonColor", json::array()), environment.sky.horizonColor);
                environment.sky.groundColor = JSONREAD::Vec3Or(sky.value("groundColor", json::array()), environment.sky.groundColor);
                environment.sky.horizonPower = sky.value("horizonPower", environment.sky.horizonPower);
                environment.sky.showSunDisk = sky.value("showSunDisk", environment.sky.showSunDisk);
                environment.sky.sunDiskIntensity = sky.value("sunDiskIntensity", environment.sky.sunDiskIntensity);
                environment.sky.sunDiskSize = sky.value("sunDiskSize", environment.sky.sunDiskSize);
                environment.sky.ambientFromSky = sky.value("ambientFromSky", environment.sky.ambientFromSky);
                environment.sky.reflectionIntensity = sky.value("reflectionIntensity", environment.sky.reflectionIntensity);
                environment.sky.showDebugTexture = sky.value("showDebugTexture", environment.sky.showDebugTexture);
            }

            if (in.contains("reflectionProbe") && in["reflectionProbe"].is_object()) {
                const json& probe = in["reflectionProbe"];
                environment.reflectionProbe.enabled = probe.value("enabled", environment.reflectionProbe.enabled);
                environment.reflectionProbe.sourceCubemapAsset = probe.value("sourceCubemapAsset", environment.reflectionProbe.sourceCubemapAsset);
                environment.reflectionProbe.position = JSONREAD::Vec3Or(probe.value("position", json::array()), environment.reflectionProbe.position);
                environment.reflectionProbe.radius = probe.value("radius", environment.reflectionProbe.radius);
                environment.reflectionProbe.intensity = probe.value("intensity", environment.reflectionProbe.intensity);
                environment.reflectionProbe.influenceShape = ParseReflectionProbeInfluenceShape(
                    probe.value("influenceShape", json{}),
                    environment.reflectionProbe.influenceShape);
                environment.reflectionProbe.projectionShape = ParseReflectionProbeProjectionShape(
                    probe.value("projectionShape", json{}),
                    environment.reflectionProbe.projectionShape);

                const MATH::Vec3 radiusBoxSize = ReflectionProbeRadiusBoxSize(environment.reflectionProbe.radius);
                const bool hasInfluenceBoxCenter = probe.contains("influenceBoxCenter");
                const bool hasInfluenceBoxSize = probe.contains("influenceBoxSize");
                const bool hasProjectionBoxCenter = probe.contains("projectionBoxCenter");
                const bool hasProjectionBoxSize = probe.contains("projectionBoxSize");

                environment.reflectionProbe.influenceBoxCenter = JSONREAD::Vec3Or(
                    probe.value("influenceBoxCenter", json::array()),
                    hasInfluenceBoxCenter ? environment.reflectionProbe.influenceBoxCenter : environment.reflectionProbe.position);
                environment.reflectionProbe.influenceBoxSize = JSONREAD::Vec3Or(
                    probe.value("influenceBoxSize", json::array()),
                    hasInfluenceBoxSize ? environment.reflectionProbe.influenceBoxSize : radiusBoxSize);
                environment.reflectionProbe.projectionBoxCenter = JSONREAD::Vec3Or(
                    probe.value("projectionBoxCenter", json::array()),
                    hasProjectionBoxCenter ? environment.reflectionProbe.projectionBoxCenter : environment.reflectionProbe.position);
                environment.reflectionProbe.projectionBoxSize = JSONREAD::Vec3Or(
                    probe.value("projectionBoxSize", json::array()),
                    hasProjectionBoxSize ? environment.reflectionProbe.projectionBoxSize : radiusBoxSize);
                environment.reflectionProbe.blendDistance = probe.value("blendDistance", environment.reflectionProbe.blendDistance);
                environment.reflectionProbe.priority = probe.value("priority", environment.reflectionProbe.priority);
            }

            if (in.contains("ambientOcclusion") && in["ambientOcclusion"].is_object()) {
                const json& ao = in["ambientOcclusion"];
                const bool hasSsaoMode = ao.contains("mode");
                const bool legacyEnabled = ao.value("enabled", environment.ambientOcclusion.enabled);
                if (hasSsaoMode) {
                    environment.ambientOcclusion.mode = ParseSsaoMode(
                        ao.value("mode", json{}),
                        environment.ambientOcclusion.mode);
                    environment.ambientOcclusion.enabled =
                        environment.ambientOcclusion.mode != SsaoMode::Off;
                } else {
                    environment.ambientOcclusion.enabled = legacyEnabled;
                    environment.ambientOcclusion.mode =
                        legacyEnabled ? environment.ambientOcclusion.mode : SsaoMode::Off;
                }
                environment.ambientOcclusion.radius = ao.value("radius", environment.ambientOcclusion.radius);
                environment.ambientOcclusion.bias = ao.value("bias", environment.ambientOcclusion.bias);
                environment.ambientOcclusion.strength = ao.value("strength", environment.ambientOcclusion.strength);
                environment.ambientOcclusion.power = ao.value("power", environment.ambientOcclusion.power);
                environment.ambientOcclusion.diffuseStrength = ao.value("diffuseStrength", environment.ambientOcclusion.diffuseStrength);
                environment.ambientOcclusion.specularStrength = ao.value("specularStrength", environment.ambientOcclusion.specularStrength);
                environment.ambientOcclusion.sampleCount = ao.value("sampleCount", environment.ambientOcclusion.sampleCount);
                environment.ambientOcclusion.blurIterations = ao.value("blurIterations", environment.ambientOcclusion.blurIterations);
                environment.ambientOcclusion.sampleCount = std::clamp<uint32_t>(environment.ambientOcclusion.sampleCount, 8u, 32u);
                environment.ambientOcclusion.blurIterations = std::clamp<uint32_t>(environment.ambientOcclusion.blurIterations, 0u, 4u);
            }

            if (in.contains("bloom") && in["bloom"].is_object()) {
                const json& bloom = in["bloom"];
                environment.bloom.enabled = bloom.value("enabled", environment.bloom.enabled);
                environment.bloom.threshold = bloom.value("threshold", environment.bloom.threshold);
                environment.bloom.intensity = bloom.value("intensity", environment.bloom.intensity);
                environment.bloom.radius = bloom.value("radius", environment.bloom.radius);
                environment.bloom.downsampleCount = bloom.value("downsampleCount", environment.bloom.downsampleCount);
            }

            if (in.contains("fog") && in["fog"].is_object()) {
                const json& fog = in["fog"];
                environment.fog.enabled = fog.value("enabled", environment.fog.enabled);
                environment.fog.volumetric = fog.value("volumetric", environment.fog.volumetric);
                environment.fog.color = JSONREAD::Vec3Or(fog.value("color", json::array()), environment.fog.color);
                environment.fog.density = fog.value("density", environment.fog.density);
                environment.fog.startDistance = fog.value("startDistance", environment.fog.startDistance);
                environment.fog.endDistance = fog.value("endDistance", environment.fog.endDistance);
                environment.fog.heightFalloff = fog.value("heightFalloff", environment.fog.heightFalloff);
                environment.fog.anisotropy = fog.value("anisotropy", environment.fog.anisotropy);
                environment.fog.temporalWeight = fog.value("temporalWeight", environment.fog.temporalWeight);
                environment.fog.useSkyHorizonColor = fog.value("useSkyHorizonColor", environment.fog.useSkyHorizonColor);
            }

            if (in.contains("toneMapping") && in["toneMapping"].is_object()) {
                const json& toneMapping = in["toneMapping"];
                environment.toneMapping.enabled = toneMapping.value("enabled", environment.toneMapping.enabled);
                environment.toneMapping.exposure = toneMapping.value("exposure", environment.toneMapping.exposure);
                environment.toneMapping.gamma = toneMapping.value("gamma", environment.toneMapping.gamma);
                environment.toneMapping.mode = toneMapping.value("mode", environment.toneMapping.mode);
            }
            environment.specularIntensity = in.value("specularIntensity", environment.specularIntensity);
            environment.specularPower = in.value("specularPower", environment.specularPower);
            environment.showLightDebug = in.value("showLightDebug", environment.showLightDebug);
            environment.showPointLightMarkers = in.value("showPointLightMarkers", environment.showPointLightMarkers);
            environment.showSkyDebugInfo = in.value("showSkyDebugInfo", environment.showSkyDebugInfo);

            if (in.contains("post") && in["post"].is_object()) {
                const json& post = in["post"];
                environment.post.enabled = post.value("enabled", environment.post.enabled);
                environment.post.globalPostProfileId = post.value("globalPostProfileId", environment.post.globalPostProfileId);
                bool hasParamValues = false;
                if (post.contains("paramValues") && post["paramValues"].is_array()) {
                    const json& paramValues = post["paramValues"];
                    const size_t count = std::min<size_t>(paramValues.size(), std::size(environment.post.paramValues));
                    for (size_t i = 0; i < count; ++i) {
                        environment.post.paramValues[i] = FromFloat4(paramValues[i], environment.post.paramValues[i]);
                    }
                    hasParamValues = true;
                } else if (post.contains("userOverrides") && post["userOverrides"].is_array()) {
                    const json& overrides = post["userOverrides"];
                    const size_t count = std::min<size_t>(overrides.size(), std::size(environment.post.paramValues));
                    for (size_t i = 0; i < count; ++i) {
                        environment.post.paramValues[i] = FromFloat4(overrides[i], environment.post.paramValues[i]);
                    }
                    hasParamValues = true;
                }
                environment.post.valuesInitialized = post.value("valuesInitialized", hasParamValues);
            }
        }

        void SerializeLightingBakeSettings(const SceneLightingBakeSettings& settings, json& out) {
            LightProbeVolumeSettings lightProbe = settings.lightProbeVolume;
            ClampLightProbeVolumeSettings(lightProbe);

            // Bake 逕ｨ縺ｮ authoring 險ｭ螳壹・ SceneDocument 蛛ｴ縺ｫ謖√◆縺帙ｋ縲・            out["lightProbeVolume"]["enabled"] = lightProbe.enabled;
            out["lightProbeVolume"]["origin"] = ToVec3(lightProbe.origin);
            out["lightProbeVolume"]["size"] = ToVec3(lightProbe.size);
            out["lightProbeVolume"]["count"] = json::array({
                lightProbe.countX,
                lightProbe.countY,
                lightProbe.countZ
            });
            out["lightProbeVolume"]["intensity"] = lightProbe.intensity;
            out["lightProbeVolume"]["captureResolution"] = lightProbe.captureResolution;
            out["lightProbeVolume"]["shOrder"] = lightProbe.shOrder;
        }

        void DeserializeLightingBakeSettings(const json& in, SceneLightingBakeSettings& settings) {
            if (!in.is_object()) {
                ClampLightProbeVolumeSettings(settings.lightProbeVolume);
                return;
            }

            if (in.contains("lightProbeVolume") && in["lightProbeVolume"].is_object()) {
                const json& lightProbe = in["lightProbeVolume"];
                LightProbeVolumeSettings& out = settings.lightProbeVolume;
                out.enabled = lightProbe.value("enabled", out.enabled);
                out.origin = JSONREAD::Vec3Or(lightProbe.value("origin", json::array()), out.origin);
                out.size = JSONREAD::Vec3Or(lightProbe.value("size", json::array()), out.size);
                if (lightProbe.contains("count") && lightProbe["count"].is_array() && lightProbe["count"].size() >= 3) {
                    const json& count = lightProbe["count"];
                    const auto readCount = [](const json& value, uint32_t fallback) {
                        if (!value.is_number_integer()) {
                            return fallback;
                        }
                        const int64_t signedValue = value.get<int64_t>();
                        return signedValue >= 0 ? static_cast<uint32_t>(signedValue) : fallback;
                    };
                    out.countX = readCount(count[0], out.countX);
                    out.countY = readCount(count[1], out.countY);
                    out.countZ = readCount(count[2], out.countZ);
                } else {
                    out.countX = lightProbe.value("countX", out.countX);
                    out.countY = lightProbe.value("countY", out.countY);
                    out.countZ = lightProbe.value("countZ", out.countZ);
                }
                out.intensity = lightProbe.value("intensity", out.intensity);
                out.captureResolution = lightProbe.value("captureResolution", out.captureResolution);
                out.shOrder = lightProbe.value("shOrder", out.shOrder);
            }

            ClampLightProbeVolumeSettings(settings.lightProbeVolume);
        }

        void DeserializeSystems(const json& in, SceneDocument& outDocument) {
            outDocument.systems.clear();
            if (!in.is_array()) {
                outDocument.systems = CreateDefaultSceneSystems();
                return;
            }

            for (const json& node : in) {
                if (!node.is_object()) {
                    continue;
                }

                SceneSystemData system{};
                system.systemId = node.value("systemId", std::string{});
                if (system.systemId.empty()) {
                    continue;
                }
                system.enabled = node.value("enabled", true);
                system.executionOrder = node.value("executionOrder", 0);
                system.settings = node.value("settings", json::object());
                if (!system.settings.is_object()) {
                    system.settings = json::object();
                }
                outDocument.systems.push_back(std::move(system));
            }

            if (outDocument.systems.empty()) {
                outDocument.systems = CreateDefaultSceneSystems();
            }
        }

        void SerializeSystems(const SceneDocument& document, json& out) {
            out = json::array();
            const std::vector<SceneSystemData> systems = document.systems.empty()
                ? CreateDefaultSceneSystems()
                : document.systems;

            for (const SceneSystemData& system : systems) {
                out.push_back({
                    { "systemId", system.systemId },
                    { "enabled", system.enabled },
                    { "executionOrder", system.executionOrder },
                    { "settings", system.settings.is_object() ? system.settings : json::object() }
                });
            }
        }
    }

    bool SceneSerializer::LoadFromFile(const std::string& path, SceneDocument& outDocument) const {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return false;
        }

        json root = json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }

        outDocument = SceneDocument{};
        outDocument.version = root.value("version", 1u);
        outDocument.sceneName = root.value("sceneName", std::string("Untitled"));

        if (root.contains("environment") && root["environment"].is_object()) {
            DeserializeEnvironment(root["environment"], outDocument.environment);
        }

        DeserializeLightingBakeSettings(root.value("lightingBake", json::object()), outDocument.lightingBake);

        DeserializeSystems(root.value("systems", json{}), outDocument);

        if (root.contains("objects") && root["objects"].is_array()) {
            for (const json& node : root["objects"]) {
                if (!node.is_object()) {
                    continue;
                }

                SceneObjectData objectData{};
                objectData.id.value = node.value("id", 0ull);
                objectData.name = node.value("name", std::string("GameObject"));
                objectData.sourcePrefabId = node.value("sourcePrefabId", std::string{});
                objectData.enabled = node.value("enabled", true);
                objectData.editorVisible = node.value("editorVisible", true);

                if (node.contains("parent") && node["parent"].is_number_unsigned()) {
                    objectData.parent = SceneObjectId{ node["parent"].get<uint64_t>() };
                }

                if (node.contains("transform") && node["transform"].is_object()) {
                    const json& transform = node["transform"];
                    objectData.transform.position = JSONREAD::Vec3Or(transform.value("position", json::array()), objectData.transform.position);
                    objectData.transform.rotationEulerDeg = JSONREAD::Vec3Or(transform.value("rotationEulerDeg", json::array()), objectData.transform.rotationEulerDeg);
                    objectData.transform.scale = JSONREAD::Vec3Or(transform.value("scale", json::array()), objectData.transform.scale);
                }

                if (node.contains("components") && node["components"].is_array()) {
                    for (const json& componentNode : node["components"]) {
                        if (!componentNode.is_object()) {
                            continue;
                        }
                        SceneComponentData componentData{};
                        componentData.type = componentNode.value("type", std::string{});
                        componentData.properties = componentNode.value("properties", json::object());
                        objectData.components.push_back(std::move(componentData));
                    }
                }

                outDocument.objects.push_back(std::move(objectData));
            }
        }

        return true;
    }

    bool SceneSerializer::SaveToFile(const std::string& path, const SceneDocument& document) const {
        json root = json::object();
        root["version"] = document.version;
        root["sceneName"] = document.sceneName;

        SerializeEnvironment(document.environment, root["environment"]);
        SerializeLightingBakeSettings(document.lightingBake, root["lightingBake"]);
        SerializeSystems(document, root["systems"]);

        root["objects"] = json::array();
        for (const SceneObjectData& object : document.objects) {
            json node = json::object();
            node["id"] = object.id.value;
            node["name"] = object.name;
            node["sourcePrefabId"] = object.sourcePrefabId;
            if (object.parent.has_value()) {
                node["parent"] = object.parent->value;
            } else {
                node["parent"] = nullptr;
            }
            node["enabled"] = object.enabled;
            node["editorVisible"] = object.editorVisible;
            node["transform"]["position"] = ToVec3(object.transform.position);
            node["transform"]["rotationEulerDeg"] = ToVec3(object.transform.rotationEulerDeg);
            node["transform"]["scale"] = ToVec3(object.transform.scale);

            node["components"] = json::array();
            for (const SceneComponentData& component : object.components) {
                json componentNode;
                componentNode["type"] = component.type;
                componentNode["properties"] = component.properties;
                node["components"].push_back(std::move(componentNode));
            }

            root["objects"].push_back(std::move(node));
        }

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            return false;
        }

        ofs << root.dump(2);
        return true;
    }

} // namespace HIKARI
