#include "HIKARI_PrefabSerializer.h"

#include <fstream>

#include <json.hpp>

#include "HIKARI_PrefabDocument.h"

namespace HIKARI {

    using nlohmann::json;

    namespace {
        json ToVec3(const MATH::Vec3& v) {
            return json::array({ v.x, v.y, v.z });
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

        void SerializeObject(const SceneObjectData& object, json& out) {
            out = json::object();
            out["id"] = object.id.value;
            out["name"] = object.name;
            out["enabled"] = object.enabled;
            out["editorVisible"] = object.editorVisible;
            out["sourcePrefabId"] = object.sourcePrefabId;
            out["parent"] = nullptr;

            out["transform"]["position"] = ToVec3(object.transform.position);
            out["transform"]["rotationEulerDeg"] = ToVec3(object.transform.rotationEulerDeg);
            out["transform"]["scale"] = ToVec3(object.transform.scale);

            out["components"] = json::array();
            for (const SceneComponentData& component : object.components) {
                json componentNode = json::object();
                componentNode["type"] = component.type;
                componentNode["properties"] = component.properties;
                out["components"].push_back(std::move(componentNode));
            }
        }

        void DeserializeObject(const json& node, SceneObjectData& object) {
            if (!node.is_object()) {
                return;
            }

            object.id.value = node.value("id", 0ull);
            object.name = node.value("name", std::string("PrefabObject"));
            object.enabled = node.value("enabled", true);
            object.editorVisible = node.value("editorVisible", true);
            object.sourcePrefabId = node.value("sourcePrefabId", std::string{});
            object.parent.reset();

            if (node.contains("transform") && node["transform"].is_object()) {
                const json& transform = node["transform"];
                object.transform.position = FromVec3(transform.value("position", json::array()), object.transform.position);
                object.transform.rotationEulerDeg = FromVec3(transform.value("rotationEulerDeg", json::array()), object.transform.rotationEulerDeg);
                object.transform.scale = FromVec3(transform.value("scale", json::array()), object.transform.scale);
            }

            object.components.clear();
            if (node.contains("components") && node["components"].is_array()) {
                for (const json& componentNode : node["components"]) {
                    if (!componentNode.is_object()) {
                        continue;
                    }

                    SceneComponentData componentData{};
                    componentData.type = componentNode.value("type", std::string{});
                    componentData.properties = componentNode.value("properties", json::object());
                    object.components.push_back(std::move(componentData));
                }
            }
        }
    }

    bool PrefabSerializer::LoadFromFile(const std::string& path, PrefabDocument& outDocument) const {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return false;
        }

        json root = json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }

        outDocument = PrefabDocument{};
        outDocument.version = root.value("version", 1u);
        outDocument.prefabName = root.value("prefabName", std::string("Prefab"));
        DeserializeObject(root.value("rootObject", json::object()), outDocument.rootObject);
        return true;
    }

    bool PrefabSerializer::SaveToFile(const std::string& path, const PrefabDocument& document) const {
        json root = json::object();
        root["version"] = document.version;
        root["prefabName"] = document.prefabName;
        SerializeObject(document.rootObject, root["rootObject"]);

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << root.dump(2);
        return true;
    }

} // namespace HIKARI
