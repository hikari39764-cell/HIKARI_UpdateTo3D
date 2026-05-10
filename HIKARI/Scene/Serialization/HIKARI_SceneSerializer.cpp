#include "HIKARI_SceneSerializer.h"

#include <fstream>
#include <algorithm>
#include <array>

#include <json.hpp>

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

        MATH::Vec3 FromVec3(const json& in, const MATH::Vec3& fallback) {
            if (!in.is_array() || in.size() < 3) {
                return fallback;
            }
            return {
                in[0].is_number() ? in[0].get<float>() : fallback.x,
                in[1].is_number() ? in[1].get<float>() : fallback.y,
                in[2].is_number() ? in[2].get<float>() : fallback.z
            };
        }

        void SerializeEnvironment(const SceneEnvironment& environment, json& out) {
            out["ambient"]["color"] = ToVec3(environment.ambient.color);
            out["ambient"]["intensity"] = environment.ambient.intensity;

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
            out["sky"]["skyAsset"] = environment.sky.skyAsset;
            out["sky"]["scale"] = environment.sky.scale;
            out["sky"]["yaw"] = environment.sky.yaw;
            out["sky"]["exposure"] = environment.sky.exposure;
            out["sky"]["tint"] = ToVec3(environment.sky.tint);
            out["sky"]["followCamera"] = environment.sky.followCamera;

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
                environment.ambient.color = FromVec3(ambient.value("color", json::array()), environment.ambient.color);
                environment.ambient.intensity = ambient.value("intensity", environment.ambient.intensity);
            }

            if (in.contains("directional")) {
                const json& directional = in["directional"];
                environment.directional.enabled = directional.value("enabled", environment.directional.enabled);
                environment.directional.direction = FromVec3(directional.value("direction", json::array()), environment.directional.direction);
                environment.directional.intensity = directional.value("intensity", environment.directional.intensity);
                environment.directional.color = FromVec3(directional.value("color", json::array()), environment.directional.color);
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
                    pointLight.position = FromVec3(node.value("position", json::array()), pointLight.position);
                    pointLight.range = node.value("range", pointLight.range);
                    pointLight.color = FromVec3(node.value("color", json::array()), pointLight.color);
                    pointLight.intensity = node.value("intensity", pointLight.intensity);
                    environment.pointLights.push_back(pointLight);
                }
            }

            if (in.contains("sky")) {
                const json& sky = in["sky"];
                environment.sky.enabled = sky.value("enabled", environment.sky.enabled);
                environment.sky.skyAsset = sky.value("skyAsset", environment.sky.skyAsset);
                environment.sky.scale = sky.value("scale", environment.sky.scale);
                environment.sky.yaw = sky.value("yaw", environment.sky.yaw);
                environment.sky.exposure = sky.value("exposure", environment.sky.exposure);
                environment.sky.tint = FromVec3(sky.value("tint", json::array()), environment.sky.tint);
                environment.sky.followCamera = sky.value("followCamera", environment.sky.followCamera);
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
                    objectData.transform.position = FromVec3(transform.value("position", json::array()), objectData.transform.position);
                    objectData.transform.rotationEulerDeg = FromVec3(transform.value("rotationEulerDeg", json::array()), objectData.transform.rotationEulerDeg);
                    objectData.transform.scale = FromVec3(transform.value("scale", json::array()), objectData.transform.scale);
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
