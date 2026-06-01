#include "HIKARI_LightingBakeManifest.h"

#include <fstream>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::LIGHTING {

    namespace {

        nlohmann::json ToJson(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        MATH::Vec3 ReadVec3(const nlohmann::json& node) {
            if (node.is_array() && node.size() >= 3) {
                return MATH::Vec3{
                    node[0].get<float>(),
                    node[1].get<float>(),
                    node[2].get<float>()
                };
            }

            if (node.is_object()) {
                return MATH::Vec3{
                    node.value("x", 0.0f),
                    node.value("y", 0.0f),
                    node.value("z", 0.0f)
                };
            }

            return MATH::Vec3{};
        }

        ReflectionProbeBakeRecord ReadReflectionProbe(const nlohmann::json& node) {
            ReflectionProbeBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.name = node.value("name", std::string{});
            record.position = ReadVec3(node.value("position", nlohmann::json::array()));
            record.radius = node.value("radius", 0.0f);
            record.intensity = node.value("intensity", 1.0f);
            record.captureCubemapPath = node.value("captureCubemapPath", std::string{});
            record.prefilteredCubemapPath = node.value("prefilteredCubemapPath", std::string{});
            record.brdfLutPath = node.value("brdfLutPath", std::string{});
            record.prefilteredMipCount = node.value("prefilteredMipCount", 1u);
            return record;
        }

        LightProbeBakeRecord ReadLightProbe(const nlohmann::json& node) {
            LightProbeBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.position = ReadVec3(node.value("position", nlohmann::json::array()));
            record.shDataPath = node.value("shDataPath", std::string{});
            return record;
        }

        LightmapBakeRecord ReadLightmap(const nlohmann::json& node) {
            LightmapBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.texturePath = node.value("texturePath", std::string{});
            record.width = node.value("width", 0u);
            record.height = node.value("height", 0u);
            return record;
        }

        nlohmann::json ToJson(const ReflectionProbeBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "name", record.name },
                { "position", ToJson(record.position) },
                { "radius", record.radius },
                { "intensity", record.intensity },
                { "captureCubemapPath", record.captureCubemapPath },
                { "prefilteredCubemapPath", record.prefilteredCubemapPath },
                { "brdfLutPath", record.brdfLutPath },
                { "prefilteredMipCount", record.prefilteredMipCount },
            };
        }

        nlohmann::json ToJson(const LightProbeBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "position", ToJson(record.position) },
                { "shDataPath", record.shDataPath },
            };
        }

        nlohmann::json ToJson(const LightmapBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "texturePath", record.texturePath },
                { "width", record.width },
                { "height", record.height },
            };
        }

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage) {
                *outMessage = std::move(message);
            }
        }

    } // namespace

    bool LoadLightingBakeManifest(
        const std::filesystem::path& path,
        LightingBakeManifest& outManifest,
        std::string* outMessage) {

        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            SetMessage(outMessage, "manifest not found: " + path.generic_string());
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            SetMessage(outMessage, "manifest parse failed: " + path.generic_string());
            return false;
        }

        LightingBakeManifest manifest{};
        manifest.version = root.value("version", kLightingBakeManifestVersion);
        manifest.sceneGuid = root.value("sceneGuid", std::string{});
        manifest.bakeGuid = root.value("bakeGuid", std::string{});
        manifest.bakeVersion = root.value("bakeVersion", 1u);
        manifest.generatedRoot = root.value("generatedRoot", std::string{});

        for (const nlohmann::json& node : root.value("reflectionProbes", nlohmann::json::array())) {
            manifest.reflectionProbes.push_back(ReadReflectionProbe(node));
        }
        for (const nlohmann::json& node : root.value("lightProbes", nlohmann::json::array())) {
            manifest.lightProbes.push_back(ReadLightProbe(node));
        }
        for (const nlohmann::json& node : root.value("lightmaps", nlohmann::json::array())) {
            manifest.lightmaps.push_back(ReadLightmap(node));
        }

        outManifest = std::move(manifest);
        SetMessage(outMessage, "manifest loaded");
        return true;
    }

    bool SaveLightingBakeManifest(
        const std::filesystem::path& path,
        const LightingBakeManifest& manifest,
        std::string* outMessage) {

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            SetMessage(outMessage, "failed to create manifest directory: " + ec.message());
            return false;
        }

        nlohmann::json reflectionProbes = nlohmann::json::array();
        for (const ReflectionProbeBakeRecord& record : manifest.reflectionProbes) {
            reflectionProbes.push_back(ToJson(record));
        }

        nlohmann::json lightProbes = nlohmann::json::array();
        for (const LightProbeBakeRecord& record : manifest.lightProbes) {
            lightProbes.push_back(ToJson(record));
        }

        nlohmann::json lightmaps = nlohmann::json::array();
        for (const LightmapBakeRecord& record : manifest.lightmaps) {
            lightmaps.push_back(ToJson(record));
        }

        // Bake manifest は editor baker が生成し、runtime loader が消費する。
        const nlohmann::json root{
            { "version", manifest.version },
            { "sceneGuid", manifest.sceneGuid },
            { "bakeGuid", manifest.bakeGuid },
            { "bakeVersion", manifest.bakeVersion },
            { "generatedRoot", manifest.generatedRoot },
            { "reflectionProbes", reflectionProbes },
            { "lightProbes", lightProbes },
            { "lightmaps", lightmaps },
        };

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            SetMessage(outMessage, "failed to write manifest: " + path.generic_string());
            return false;
        }

        ofs << root.dump(2) << '\n';
        SetMessage(outMessage, "manifest saved");
        return true;
    }

    std::filesystem::path BuildLightingBakeRoot(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) {

        return (projectRoot / "Library" / "Generated" / "Lighting" / sceneGuid).lexically_normal();
    }

    std::filesystem::path BuildLightingBakeManifestPath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) {

        return BuildLightingBakeRoot(projectRoot, sceneGuid) / "bake_manifest.json";
    }

} // namespace HIKARI::ASSETS::LIGHTING
