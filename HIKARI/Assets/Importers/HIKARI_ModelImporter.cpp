#include "HIKARI_ModelImporter.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_TextureImportBackend_DirectXTex.h"
#include "Render3D/Core/HIKARI_ModelManager.h"

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool IsCookableModelExtension(const std::string& ext) {
            return ext == ".gltf" || ext == ".obj";
        }

        const char* ToSupportedModelExtensionsText() {
            return ".gltf, .obj";
        }

        std::filesystem::path ResolveProjectPath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            if (path.is_absolute()) {
                return path.lexically_normal();
            }
            return (projectRoot / path).lexically_normal();
        }

        std::filesystem::path MakeProjectRelative(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            std::error_code ec{};
            std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
            if (ec) {
                return path.lexically_normal();
            }
            return relative.lexically_normal();
        }

        bool ReplaceFileWithTemp(
            const std::filesystem::path& tempPath,
            const std::filesystem::path& finalPath,
            const char* artifactLabel,
            std::string& outMessage) {

            const BOOL moved = MoveFileExW(
                tempPath.wstring().c_str(),
                finalPath.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!moved) {
                const DWORD error = GetLastError();
                std::error_code removeEc{};
                std::filesystem::remove(tempPath, removeEc);

                std::ostringstream oss;
                oss << "[AssetImporter] failed to replace " << artifactLabel << " artifact. error=" << error
                    << " temp=" << tempPath.generic_string()
                    << " final=" << finalPath.generic_string();
                outMessage = oss.str();
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }
            return true;
        }

        bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }
            outJson = nlohmann::json::parse(ifs, nullptr, false);
            return !outJson.is_discarded() && outJson.is_object();
        }

        nlohmann::json ToJson(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        nlohmann::json ToJson(const MATH::Vec4& value) {
            return nlohmann::json::array({ value.x, value.y, value.z, value.w });
        }

        const char* ToString(AlphaMode alphaMode) {
            switch (alphaMode) {
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
            case AlphaMode::Opaque:
            default: return "Opaque";
            }
        }

        struct TextureCookDiagnostic {
            int index = -1;
            std::string name{};
            std::string originalPath{};
            std::string cookedPath{};
            std::string guid{};
            bool htexReady = false;
            bool dependencyResolved = false;
            bool sourceHasMeaningfulAlpha = false;
            bool sourceHasTranslucentAlpha = false;
            bool sourceHasCutoutAlpha = false;
        };

        nlohmann::json SlotToJson(
            const TextureSlot& slot,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics) {

            nlohmann::json json{
                { "textureIndex", slot.textureIndex },
                { "texCoord", slot.texCoord },
                { "scale", slot.scale },
                { "strength", slot.strength },
            };

            if (slot.textureIndex >= 0 &&
                static_cast<size_t>(slot.textureIndex) < textureDiagnostics.size()) {
                const TextureCookDiagnostic& texture = textureDiagnostics[static_cast<size_t>(slot.textureIndex)];
                json["name"] = texture.name;
                json["sourcePath"] = texture.originalPath;
                json["runtimePath"] = texture.cookedPath;
                json["guid"] = texture.guid;
                json["htexReady"] = texture.htexReady;
                json["sourceHasMeaningfulAlpha"] = texture.sourceHasMeaningfulAlpha;
                json["sourceHasTranslucentAlpha"] = texture.sourceHasTranslucentAlpha;
                json["sourceHasCutoutAlpha"] = texture.sourceHasCutoutAlpha;
            }
            return json;
        }

        nlohmann::json BuildModelDiagnostics(
            const ModelAsset& model,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics,
            int htexReferenceCount,
            int fallbackTextureCount,
            const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReport,
            const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidation,
            bool hcmeshReady,
            const std::string& hcmeshMessage) {

            int primitiveCount = 0;
            int staticVertexCount = 0;
            int skinnedVertexCount = 0;
            int indexCount = 0;
            for (const MeshAsset& mesh : model.meshes) {
                primitiveCount += static_cast<int>(mesh.primitives.size());
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    staticVertexCount += static_cast<int>(primitive.staticVertices.size());
                    skinnedVertexCount += static_cast<int>(primitive.skinnedVertices.size());
                    indexCount += static_cast<int>(primitive.indices.size());
                }
            }

            nlohmann::json textures = nlohmann::json::array();
            for (const TextureCookDiagnostic& texture : textureDiagnostics) {
                textures.push_back({
                    { "index", texture.index },
                    { "name", texture.name },
                    { "sourcePath", texture.originalPath },
                    { "runtimePath", texture.cookedPath },
                    { "guid", texture.guid },
                    { "htexReady", texture.htexReady },
                    { "dependencyResolved", texture.dependencyResolved },
                    { "sourceHasMeaningfulAlpha", texture.sourceHasMeaningfulAlpha },
                    { "sourceHasTranslucentAlpha", texture.sourceHasTranslucentAlpha },
                    { "sourceHasCutoutAlpha", texture.sourceHasCutoutAlpha },
                });
            }

            nlohmann::json materials = nlohmann::json::array();
            for (const MaterialAsset& material : model.materials) {
                materials.push_back({
                    { "name", material.name },
                    { "baseColorFactor", ToJson(material.baseColorFactor) },
                    { "metallicFactor", material.metallicFactor },
                    { "roughnessFactor", material.roughnessFactor },
                    { "emissiveFactor", ToJson(material.emissiveFactor) },
                    { "emissiveStrength", material.emissiveStrength },
                    { "alphaMode", ToString(material.alphaMode) },
                    { "doubleSided", material.doubleSided },
                    { "featureBits", material.featureBits },
                    { "slots", {
                        { "baseColor", SlotToJson(material.baseColorTexture, textureDiagnostics) },
                        { "normal", SlotToJson(material.normalTexture, textureDiagnostics) },
                        { "metallicRoughness", SlotToJson(material.metallicRoughnessTexture, textureDiagnostics) },
                        { "occlusion", SlotToJson(material.occlusionTexture, textureDiagnostics) },
                        { "emissive", SlotToJson(material.emissiveTexture, textureDiagnostics) },
                    } },
                });
            }

            nlohmann::json importMessages = nlohmann::json::array();
            for (const std::string& message : model.importDiagnostics.messages) {
                importMessages.push_back(message);
            }
            nlohmann::json unsupportedExtensions = nlohmann::json::array();
            for (const std::string& extension : model.importDiagnostics.unsupportedExtensions) {
                unsupportedExtensions.push_back(extension);
            }

            nlohmann::json formatReport = {
                { "sourceFormat", model.importDiagnostics.sourceFormat },
                { "objectCount", model.importDiagnostics.objectCount },
                { "groupCount", model.importDiagnostics.groupCount },
                { "triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount },
                { "missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount },
                { "missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount },
                { "unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount },
                { "unsupportedFeatureCount", model.importDiagnostics.unsupportedFeatureCount },
                { "unsupportedPrimitiveModeCount", model.importDiagnostics.unsupportedPrimitiveModeCount },
                { "unsupportedExtensions", unsupportedExtensions },
                { "skippedMorphPrimitiveCount", model.importDiagnostics.skippedMorphPrimitiveCount },
                { "clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount },
                { "fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount },
            };

            nlohmann::json diagnostics{
                { "format", "HMODEL" },
                { "summary", {
                    { "nodes", model.nodes.size() },
                    { "meshes", model.meshes.size() },
                    { "primitives", primitiveCount },
                    { "materials", model.materials.size() },
                    { "textures", model.textures.size() },
                    { "skins", model.skins.size() },
                    { "animations", model.animations.size() },
                    { "staticVertices", staticVertexCount },
                    { "skinnedVertices", skinnedVertexCount },
                    { "indices", indexCount },
                    { "htexRefs", htexReferenceCount },
                    { "fallbackTextures", fallbackTextureCount },
                    { "objectCount", model.importDiagnostics.objectCount },
                    { "groupCount", model.importDiagnostics.groupCount },
                    { "triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount },
                    { "missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount },
                    { "missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount },
                    { "unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount },
                    { "clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount },
                    { "fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount },
                    { "skippedMorphPrimitives", model.importDiagnostics.skippedMorphPrimitiveCount },
                    { "unsupportedPrimitiveModes", model.importDiagnostics.unsupportedPrimitiveModeCount },
                    { "unsupportedFeatures", model.importDiagnostics.unsupportedFeatureCount },
                } },
                { "importMessages", std::move(importMessages) },
                { "formatReport", std::move(formatReport) },
                { "textures", std::move(textures) },
                { "materials", std::move(materials) },
            };

            nlohmann::json clusterJson{
                { "format", "HCMESH" },
                { "ready", hcmeshReady },
                { "message", hcmeshMessage },
            };
            if (clusteredReport != nullptr) {
                nlohmann::json messages = nlohmann::json::array();
                for (const std::string& message : clusteredReport->messages) {
                    messages.push_back(message);
                }
                clusterJson["summary"] = {
                    { "surfaces", clusteredReport->surfaceCount },
                    { "clusters", clusteredReport->clusterCount },
                    { "pages", clusteredReport->pageCount },
                    { "triangles", clusteredReport->triangleCount },
                    { "vertices", clusteredReport->vertexCount },
                    { "maxVerticesPerCluster", clusteredReport->maxVerticesPerCluster },
                    { "avgTrianglesPerCluster", clusteredReport->clusterCount > 0u
                        ? static_cast<double>(clusteredReport->triangleCount) / static_cast<double>(clusteredReport->clusterCount)
                        : 0.0 },
                    { "avgVerticesPerCluster", clusteredReport->clusterCount > 0u
                        ? static_cast<double>(clusteredReport->vertexCount) / static_cast<double>(clusteredReport->clusterCount)
                        : 0.0 },
                    { "skippedSkinnedPrimitives", clusteredReport->skippedSkinnedPrimitiveCount },
                    { "skippedMorphPrimitives", clusteredReport->skippedMorphPrimitiveCount },
                    { "skippedInvalidPrimitives", clusteredReport->skippedInvalidPrimitiveCount },
                    { "unsupportedPrimitiveModes", clusteredReport->unsupportedPrimitiveModeCount },
                    { "unsupportedFeatures", clusteredReport->unsupportedFeatureCount },
                };
                clusterJson["messages"] = std::move(messages);
            }
            if (clusteredValidation != nullptr) {
                nlohmann::json validationMessages = nlohmann::json::array();
                for (const std::string& message : clusteredValidation->messages) {
                    validationMessages.push_back(message);
                }
                clusterJson["validation"] = {
                    { "valid", clusteredValidation->valid },
                    { "invalidSurfaces", clusteredValidation->invalidSurfaceCount },
                    { "invalidClusters", clusteredValidation->invalidClusterCount },
                    { "invalidPages", clusteredValidation->invalidPageCount },
                    { "invalidBounds", clusteredValidation->invalidBoundsCount },
                    { "invalidMaterials", clusteredValidation->invalidMaterialCount },
                    { "messages", std::move(validationMessages) },
                };
            }
            diagnostics["clusteredGeometry"] = std::move(clusterJson);
            return diagnostics;
        }

        bool ReadTextureAlphaDiagnostics(
            const std::filesystem::path& projectRoot,
            const std::string& textureGuid,
            TextureCookDiagnostic& inOutDiagnostic) {

            if (textureGuid.empty()) {
                return false;
            }

            const std::filesystem::path reportPath =
                projectRoot / "Library" / "Imported" / textureGuid / "import_report.json";
            nlohmann::json report;
            if (!ReadJsonFile(reportPath, report)) {
                return false;
            }

            if (!report.contains("diagnostics") || !report["diagnostics"].is_object()) {
                return false;
            }
            const nlohmann::json& diagnostics = report["diagnostics"];
            if (!diagnostics.contains("texture") || !diagnostics["texture"].is_object()) {
                return false;
            }
            const nlohmann::json& texture = diagnostics["texture"];
            if (!texture.contains("sourceHasMeaningfulAlpha") ||
                !texture.contains("sourceHasTranslucentAlpha") ||
                !texture.contains("sourceHasCutoutAlpha")) {
                return false;
            }
            inOutDiagnostic.sourceHasMeaningfulAlpha =
                texture.value("sourceHasMeaningfulAlpha", false);
            inOutDiagnostic.sourceHasTranslucentAlpha =
                texture.value("sourceHasTranslucentAlpha", false);
            inOutDiagnostic.sourceHasCutoutAlpha =
                texture.value("sourceHasCutoutAlpha", false);
            return true;
        }

        void ApplyTextureAlphaSettings(
            const TextureImportSettings& settings,
            TextureCookDiagnostic& inOutDiagnostic) {

            inOutDiagnostic.sourceHasMeaningfulAlpha = settings.sourceHasMeaningfulAlpha;
            inOutDiagnostic.sourceHasTranslucentAlpha = settings.sourceHasTranslucentAlpha;
            inOutDiagnostic.sourceHasCutoutAlpha = settings.sourceHasCutoutAlpha;
        }

        bool InspectSourceTextureAlpha(
            const std::filesystem::path& absoluteTexturePath,
            TextureCookDiagnostic& inOutDiagnostic) {

            if (absoluteTexturePath.empty()) {
                return false;
            }

            TextureImportSettings settings{};
            settings.usage = TextureUsage::BaseColor;
            std::string message{};
            if (!InspectTextureAlphaWithDirectXTex(absoluteTexturePath, settings, message)) {
                return false;
            }

            ApplyTextureAlphaSettings(settings, inOutDiagnostic);
            return true;
        }

        bool ResolveTextureToHtex(
            const std::filesystem::path& projectRoot,
            TextureAsset3D& texture,
            AssetDependencyDesc& outDependency,
            bool& outHasDependency,
            TextureCookDiagnostic& inOutDiagnostic) {

            outHasDependency = false;
            if (texture.sourcePath.empty()) {
                return false;
            }

            const std::filesystem::path absoluteTexturePath = ResolveProjectPath(projectRoot, texture.sourcePath);
            std::filesystem::path metaPath = absoluteTexturePath;
            metaPath += ".hikari.meta";

            nlohmann::json metaJson;
            if (!ReadJsonFile(metaPath, metaJson)) {
                return false;
            }

            const std::string guid = metaJson.value("guid", "");
            if (!guid.empty()) {
                outDependency.guid.value = guid;
                outDependency.path = MakeProjectRelative(projectRoot, absoluteTexturePath).generic_string();
                outDependency.role = "Texture";
                outHasDependency = true;
                inOutDiagnostic.guid = guid;
                inOutDiagnostic.dependencyResolved = true;
                if (!ReadTextureAlphaDiagnostics(projectRoot, guid, inOutDiagnostic)) {
                    // 古い texture report の場合でも、model cook は baseColor alpha を見落とさない。
                    InspectSourceTextureAlpha(absoluteTexturePath, inOutDiagnostic);
                }
            }

            if (!metaJson.contains("artifacts") || !metaJson["artifacts"].is_array()) {
                return false;
            }

            // テクスチャ meta の MainTexture が HTEX なら、モデル内参照を実行時向けに差し替える。
            for (const auto& artifact : metaJson["artifacts"]) {
                if (!artifact.is_object()) {
                    continue;
                }

                const std::string role = artifact.value("role", "");
                const std::string format = artifact.value("format", "");
                const std::string path = artifact.value("path", "");
                if (role == "MainTexture" && format == "HTEX" && !path.empty()) {
                    texture.sourcePath = path;
                    return true;
                }
            }
            return false;
        }

        void ApplyBaseColorAlphaMaterialPolicy(
            ModelAsset& model,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics) {

            for (MaterialAsset& material : model.materials) {
                const int textureIndex = material.baseColorTexture.textureIndex;
                if (textureIndex < 0 ||
                    static_cast<size_t>(textureIndex) >= textureDiagnostics.size()) {
                    continue;
                }

                const TextureCookDiagnostic& texture =
                    textureDiagnostics[static_cast<size_t>(textureIndex)];
                if (!texture.sourceHasMeaningfulAlpha) {
                    continue;
                }

                material.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
                material.doubleSided = true;

                if (material.alphaMode == AlphaMode::Opaque) {
                    // baseColor の alpha が実データとして存在する場合、旧 asset の OPAQUE 指定を補正する。
                    if (texture.sourceHasTranslucentAlpha) {
                        material.alphaMode = AlphaMode::Blend;
                    } else {
                        material.alphaMode = AlphaMode::Mask;
                        material.featureBits |= MATERIAL_FEATURES::AlphaMask;
                    }
                }
            }
        }
    }

    const char* ModelImporter::GetImporterId() const {
        return "ModelImporter";
    }

    uint32_t ModelImporter::GetImporterVersion() const {
        return 8;
    }

    bool ModelImporter::CanImport(const std::filesystem::path& sourcePath) const {
        const std::string ext = ToLowerCopy(sourcePath.extension().string());
        return IsCookableModelExtension(ext);
    }

    AssetMeta ModelImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Model;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "cookModel", true },
            { "outputFormat", "HMODEL" },
            { "futureMeshFormat", "HCMESH" },
            { "loadMaterials", true },
            { "loadTextures", true },
        }.dump(2);
        return meta;
    }

    AssetImportResult ModelImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        const std::string ext = ToLowerCopy(record.sourcePath.extension().string());
        if (!IsCookableModelExtension(ext)) {
            result.message = "[AssetImporter] HMODEL cook supports " +
                std::string(ToSupportedModelExtensionsText()) +
                " only. source=" +
                record.sourcePath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path absoluteSource = ResolveProjectPath(context.projectRoot, record.sourcePath);

        ModelManager loader{};
        ModelAsset model{};
        model.SetName(record.guid.value);
        model.SetSourcePath(absoluteSource.generic_string());
        if (!loader.LoadCpuAssetFromSource(model)) {
            result.message = "[AssetImporter] failed to read model source for HMODEL cook. source=" +
                record.sourcePath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        model.SetName(record.guid.value);
        model.SetSourcePath(record.sourcePath.generic_string());

        int htexReferenceCount = 0;
        int fallbackTextureCount = 0;
        std::vector<TextureCookDiagnostic> textureDiagnostics;
        textureDiagnostics.reserve(model.textures.size());
        for (size_t i = 0; i < model.textures.size(); ++i) {
            TextureAsset3D& texture = model.textures[i];
            TextureCookDiagnostic diagnostic{};
            diagnostic.index = static_cast<int>(i);
            diagnostic.name = texture.name;
            diagnostic.originalPath = texture.sourcePath;

            AssetDependencyDesc dependency{};
            bool hasDependency = false;
            if (ResolveTextureToHtex(context.projectRoot, texture, dependency, hasDependency, diagnostic)) {
                ++htexReferenceCount;
                diagnostic.htexReady = true;
            } else if (!texture.sourcePath.empty()) {
                ++fallbackTextureCount;
                InspectSourceTextureAlpha(
                    ResolveProjectPath(context.projectRoot, texture.sourcePath),
                    diagnostic);
            }

            if (hasDependency) {
                result.dependencies.push_back(std::move(dependency));
            }
            diagnostic.cookedPath = texture.sourcePath;
            textureDiagnostics.push_back(std::move(diagnostic));
        }

        ApplyBaseColorAlphaMaterialPolicy(model, textureDiagnostics);

        const std::filesystem::path finalPath = context.importedDirectory / "model.hmodel";
        const std::filesystem::path tempPath = context.importedDirectory / "model.importing.hmodel";

        std::error_code removeEc{};
        std::filesystem::remove(tempPath, removeEc);
        if (removeEc) {
            result.message = "[AssetImporter] failed to clear stale temporary HMODEL: " + tempPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string hmodelMessage{};
        if (!WriteHmodelFile(tempPath, model, hmodelMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = hmodelMessage.empty() ? "[AssetImporter] HMODEL write failed" : hmodelMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        if (!ReplaceFileWithTemp(tempPath, finalPath, "HMODEL", result.message)) {
            return result;
        }

        bool hcmeshReady = false;
        std::string hcmeshMessage{};
        RENDER3D::CLUSTER::ClusteredGeometryBuildReport clusteredReport{};
        ASSETS::GEOMETRY::ClusteredGeometryValidationResult clusteredValidation{};
        RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
        ASSETS::GEOMETRY::ClusterCookSettings clusterSettings{};
        clusterSettings.maxTrianglesPerCluster = 64u;
        clusterSettings.maxVerticesPerCluster = 128u;
        clusterSettings.maxClustersPerPage = 64u;
        if (ASSETS::GEOMETRY::CookClusteredGeometryFromModel(
                model,
                record.guid,
                clusterSettings,
                clusteredGeometry,
                clusteredReport)) {
            clusteredValidation = ASSETS::GEOMETRY::ValidateClusteredGeometryAsset(clusteredGeometry);
            if (clusteredValidation.valid) {
                const std::filesystem::path finalHcmeshPath = context.importedDirectory / "clustered_mesh.hcmesh";
                const std::filesystem::path tempHcmeshPath = context.importedDirectory / "clustered_mesh.importing.hcmesh";
                std::error_code removeHcmeshEc{};
                std::filesystem::remove(tempHcmeshPath, removeHcmeshEc);
                if (removeHcmeshEc) {
                    hcmeshMessage = "[AssetImporter] failed to clear stale temporary HCMESH: " +
                        tempHcmeshPath.generic_string();
                } else if (ASSETS::GEOMETRY::WriteHcmeshFile(tempHcmeshPath, clusteredGeometry, hcmeshMessage) &&
                    ReplaceFileWithTemp(tempHcmeshPath, finalHcmeshPath, "HCMESH", hcmeshMessage)) {
                    hcmeshReady = true;
                    result.artifacts.push_back(AssetArtifactDesc{
                        "ClusteredGeometry",
                        MakeProjectRelative(context.projectRoot, finalHcmeshPath).generic_string(),
                        "HCMESH"
                    });
                }
            } else {
                hcmeshMessage = clusteredValidation.messages.empty()
                    ? "[AssetImporter] HCMESH validation failed"
                    : clusteredValidation.messages.front();
            }
        } else {
            hcmeshMessage = clusteredReport.messages.empty()
                ? "[AssetImporter] HCMESH cook produced no clusterable primitive"
                : clusteredReport.messages.front();
        }

        result.success = true;
        result.message = "[AssetImporter] Wrote HMODEL meshes=" +
            std::to_string(model.meshes.size()) +
            " materials=" + std::to_string(model.materials.size()) +
            " textures=" + std::to_string(model.textures.size()) +
            " htexRefs=" + std::to_string(htexReferenceCount) +
            " fallbackTextures=" + std::to_string(fallbackTextureCount) +
            " hcmesh=" + (hcmeshReady ? "ready" : "fallback");
        result.diagnosticsJson = BuildModelDiagnostics(
            model,
            textureDiagnostics,
            htexReferenceCount,
            fallbackTextureCount,
            &clusteredReport,
            &clusteredValidation,
            hcmeshReady,
            hcmeshMessage).dump(2);
        result.artifacts.push_back(AssetArtifactDesc{
            "MainModel",
            MakeProjectRelative(context.projectRoot, finalPath).generic_string(),
            "HMODEL"
        });

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
