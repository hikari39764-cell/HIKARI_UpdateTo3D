#include "Render3D/Core/HIKARI_AssimpModelLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI {

    namespace {

        struct VertexInfluence {
            uint16_t joint = 0;
            float weight = 0.0f;
        };

        std::string ToString(const aiString& value) {
            return value.length > 0 ? std::string(value.C_Str()) : std::string{};
        }

        std::string NormalizePathString(const std::filesystem::path& path) {
            return path.lexically_normal().generic_string();
        }

        std::string SanitizeAssimpPath(std::string value) {
            std::replace(value.begin(), value.end(), '\\', '/');
            constexpr const char* kFileUriPrefix = "file://";
            if (value.rfind(kFileUriPrefix, 0) == 0) {
                value.erase(0, std::char_traits<char>::length(kFileUriPrefix));
            }
            return value;
        }

        bool IsEmbeddedTextureReference(const std::string& value) {
            return !value.empty() && value.front() == '*';
        }

        bool IsUnsupportedUri(const std::string& value) {
            const size_t scheme = value.find("://");
            return scheme != std::string::npos && value.rfind("file://", 0) != 0;
        }

        const TextureAsset3D* FindTextureBySlot(const ModelAsset& asset, const TextureSlot& slot) {
            if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
                return nullptr;
            }
            return &asset.textures[static_cast<size_t>(slot.textureIndex)];
        }

        MATH::Vec3 ToVec3(const aiVector3D& value) {
            return { value.x, value.y, value.z };
        }

        MATH::Vec4 ToVec4(const aiColor4D& value) {
            return { value.r, value.g, value.b, value.a };
        }

        MATH::Mat4 ToMat4(const aiMatrix4x4& value) {
            MATH::Mat4 out = MATH::Mat4::Identity();
            out.m[0][0] = value.a1;
            out.m[1][0] = value.a2;
            out.m[2][0] = value.a3;
            out.m[3][0] = value.a4;

            out.m[0][1] = value.b1;
            out.m[1][1] = value.b2;
            out.m[2][1] = value.b3;
            out.m[3][1] = value.b4;

            out.m[0][2] = value.c1;
            out.m[1][2] = value.c2;
            out.m[2][2] = value.c3;
            out.m[3][2] = value.c4;

            out.m[0][3] = value.d1;
            out.m[1][3] = value.d2;
            out.m[2][3] = value.d3;
            out.m[3][3] = value.d4;
            return out;
        }

        MATH::Quat ToQuat(const aiQuaternion& value) {
            return MATH::NormalizeQ({ value.x, value.y, value.z, value.w });
        }

        float Clamp01(float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float RoughnessFromPhongShininess(float shininess) {
            if (!std::isfinite(shininess) || shininess <= 0.0f) {
                return 1.0f;
            }
            return Clamp01(std::sqrt(2.0f / (shininess + 2.0f)));
        }

        MATH::Vec3 BuildFallbackTangent(const MATH::Vec3& normal) {
            const MATH::Vec3 reference =
                std::abs(normal.y) < 0.95f
                ? MATH::Vec3{ 0.0f, 1.0f, 0.0f }
                : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
            return MATH::Normalize(MATH::Cross(reference, normal));
        }

        void GeneratePrimitiveNormals(MeshPrimitive& primitive) {
            for (Vertex3D& vertex : primitive.staticVertices) {
                vertex.normal = {};
            }
            for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
                const uint32_t i0 = primitive.indices[i + 0u];
                const uint32_t i1 = primitive.indices[i + 1u];
                const uint32_t i2 = primitive.indices[i + 2u];
                if (i0 >= primitive.staticVertices.size() ||
                    i1 >= primitive.staticVertices.size() ||
                    i2 >= primitive.staticVertices.size()) {
                    continue;
                }

                const MATH::Vec3 p0 = primitive.staticVertices[i0].position;
                const MATH::Vec3 p1 = primitive.staticVertices[i1].position;
                const MATH::Vec3 p2 = primitive.staticVertices[i2].position;
                const MATH::Vec3 normal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
                if (MATH::Length(normal) <= 1.0e-6f) {
                    continue;
                }
                primitive.staticVertices[i0].normal = primitive.staticVertices[i0].normal + normal;
                primitive.staticVertices[i1].normal = primitive.staticVertices[i1].normal + normal;
                primitive.staticVertices[i2].normal = primitive.staticVertices[i2].normal + normal;
            }

            for (Vertex3D& vertex : primitive.staticVertices) {
                vertex.normal = MATH::Normalize(vertex.normal);
                if (MATH::Length(vertex.normal) <= 1.0e-6f) {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }
            }
        }

        void GeneratePrimitiveTangents(MeshPrimitive& primitive) {
            std::vector<MATH::Vec3> tangentAccum(primitive.staticVertices.size());
            std::vector<MATH::Vec3> bitangentAccum(primitive.staticVertices.size());

            for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
                const uint32_t i0 = primitive.indices[i + 0u];
                const uint32_t i1 = primitive.indices[i + 1u];
                const uint32_t i2 = primitive.indices[i + 2u];
                if (i0 >= primitive.staticVertices.size() ||
                    i1 >= primitive.staticVertices.size() ||
                    i2 >= primitive.staticVertices.size()) {
                    continue;
                }

                const Vertex3D& v0 = primitive.staticVertices[i0];
                const Vertex3D& v1 = primitive.staticVertices[i1];
                const Vertex3D& v2 = primitive.staticVertices[i2];
                const MATH::Vec3 edge1 = v1.position - v0.position;
                const MATH::Vec3 edge2 = v2.position - v0.position;
                const MATH::Vec2 duv1 = v1.uv0 - v0.uv0;
                const MATH::Vec2 duv2 = v2.uv0 - v0.uv0;

                const float det = duv1.x * duv2.y - duv1.y * duv2.x;
                if (std::abs(det) <= 1.0e-8f) {
                    continue;
                }

                const float inv = 1.0f / det;
                const MATH::Vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * inv;
                const MATH::Vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * inv;
                tangentAccum[i0] = tangentAccum[i0] + tangent;
                tangentAccum[i1] = tangentAccum[i1] + tangent;
                tangentAccum[i2] = tangentAccum[i2] + tangent;
                bitangentAccum[i0] = bitangentAccum[i0] + bitangent;
                bitangentAccum[i1] = bitangentAccum[i1] + bitangent;
                bitangentAccum[i2] = bitangentAccum[i2] + bitangent;
            }

            for (size_t i = 0; i < primitive.staticVertices.size(); ++i) {
                MATH::Vec3 normal = MATH::Normalize(primitive.staticVertices[i].normal);
                if (MATH::Length(normal) <= 1.0e-6f) {
                    normal = { 0.0f, 1.0f, 0.0f };
                }

                MATH::Vec3 tangent = tangentAccum[i] - normal * MATH::Dot(normal, tangentAccum[i]);
                tangent = MATH::Normalize(tangent);
                if (MATH::Length(tangent) <= 1.0e-6f) {
                    tangent = BuildFallbackTangent(normal);
                }
                if (MATH::Length(tangent) <= 1.0e-6f) {
                    tangent = { 1.0f, 0.0f, 0.0f };
                }

                const float handedness =
                    MATH::Dot(MATH::Cross(normal, tangent), bitangentAccum[i]) < 0.0f ? -1.0f : 1.0f;
                primitive.staticVertices[i].tangent = { tangent.x, tangent.y, tangent.z, handedness };
            }
        }

        int AddTexture(
            ModelAsset& asset,
            std::unordered_map<std::string, int>& textureToIndex,
            const std::filesystem::path& sourceDirectory,
            std::string rawPath,
            const char* usageName) {

            rawPath = SanitizeAssimpPath(std::move(rawPath));
            if (rawPath.empty()) {
                return -1;
            }
            if (IsEmbeddedTextureReference(rawPath)) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                ++asset.importDiagnostics.unresolvedTextureCount;
                asset.importDiagnostics.messages.push_back(
                    "[Assimp] embedded texture is not extracted yet usage=" +
                    std::string(usageName) +
                    " ref=" + rawPath);
                return -1;
            }
            if (IsUnsupportedUri(rawPath)) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                ++asset.importDiagnostics.unresolvedTextureCount;
                asset.importDiagnostics.messages.push_back(
                    "[Assimp] unsupported texture URI usage=" +
                    std::string(usageName) +
                    " uri=" + rawPath);
                return -1;
            }

            std::filesystem::path texturePath(rawPath);
            if (texturePath.is_relative()) {
                texturePath = sourceDirectory / texturePath;
            }
            const std::string normalizedPath = NormalizePathString(texturePath);
            const auto found = textureToIndex.find(normalizedPath);
            if (found != textureToIndex.end()) {
                return found->second;
            }

            TextureAsset3D texture{};
            texture.name = texturePath.filename().string();
            texture.sourcePath = normalizedPath;
            const int textureIndex = static_cast<int>(asset.textures.size());
            asset.textures.push_back(std::move(texture));
            textureToIndex.emplace(normalizedPath, textureIndex);
            return textureIndex;
        }

        bool ReadTextureSlot(
            ModelAsset& asset,
            std::unordered_map<std::string, int>& textureToIndex,
            const std::filesystem::path& sourceDirectory,
            const aiMaterial& material,
            const std::string& materialName,
            const std::vector<aiTextureType>& textureTypes,
            TextureSlot& outSlot,
            const char* usageName) {

            for (aiTextureType textureType : textureTypes) {
                if (material.GetTextureCount(textureType) == 0u) {
                    continue;
                }

                aiString texturePath{};
                unsigned int uvIndex = 0u;
                if (material.GetTexture(textureType, 0u, &texturePath, nullptr, &uvIndex) != AI_SUCCESS) {
                    continue;
                }

                const int textureIndex = AddTexture(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    ToString(texturePath),
                    usageName);
                if (textureIndex < 0) {
                    return false;
                }

                outSlot.textureIndex = textureIndex;
                outSlot.texCoord = static_cast<int>((std::min)(uvIndex, 1u));
                if (uvIndex > 1u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] texture uses UV set above 1; HIKARI currently keeps UV0/UV1 only. material=" +
                        materialName +
                        " usage=" + std::string(usageName) +
                        " texCoord=" + std::to_string(uvIndex));
                }

                aiUVTransform uvTransform{};
                if (material.Get(AI_MATKEY_UVTRANSFORM(textureType, 0), uvTransform) == AI_SUCCESS) {
                    const bool hasUvTransform =
                        std::abs(uvTransform.mTranslation.x) > 1.0e-6f ||
                        std::abs(uvTransform.mTranslation.y) > 1.0e-6f ||
                        std::abs(uvTransform.mScaling.x - 1.0f) > 1.0e-6f ||
                        std::abs(uvTransform.mScaling.y - 1.0f) > 1.0e-6f ||
                        std::abs(uvTransform.mRotation) > 1.0e-6f;
                    if (hasUvTransform) {
                        outSlot.uvScale = { uvTransform.mScaling.x, uvTransform.mScaling.y };
                        outSlot.uvOffset = { uvTransform.mTranslation.x, uvTransform.mTranslation.y };
                        outSlot.uvRotation = uvTransform.mRotation;
                    }
                }
                if (material.GetTextureCount(textureType) > 1u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] only first texture is used usage=" +
                        std::string(usageName) +
                        " count=" + std::to_string(material.GetTextureCount(textureType)));
                }
                return true;
            }
            return false;
        }

        void ReadAssimpMaterials(
            const aiScene& scene,
            const std::filesystem::path& sourceDirectory,
            ModelAsset& asset) {

            std::unordered_map<std::string, int> textureToIndex;
            asset.materials.reserve(scene.mNumMaterials);

            for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
                const aiMaterial* source = scene.mMaterials[i];
                if (source == nullptr) {
                    continue;
                }

                MaterialAsset material{};

                aiString materialName{};
                if (source->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS) {
                    material.name = ToString(materialName);
                }
                if (material.name.empty()) {
                    material.name = "AssimpMaterial" + std::to_string(i);
                }

                aiColor4D color{};
                if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &color) == AI_SUCCESS ||
                    aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) {
                    material.baseColorFactor = ToVec4(color);
                }

                if (aiGetMaterialColor(source, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS) {
                    material.specularColorFactor = { color.r, color.g, color.b };
                    material.specularFactor = (std::max)({ color.r, color.g, color.b, 0.0f });
                }

                float value = 0.0f;
                if (source->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) {
                    material.metallicFactor = Clamp01(value);
                }
                if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
                    material.roughnessFactor = Clamp01(value);
                } else if (source->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
                    material.roughnessFactor = RoughnessFromPhongShininess(value);
                }
                if (source->Get(AI_MATKEY_SPECULAR_FACTOR, value) == AI_SUCCESS) {
                    material.specularFactor = Clamp01(value);
                }
                if (source->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
                    material.baseColorFactor.w = Clamp01(value);
                    if (material.baseColorFactor.w < 0.999f) {
                        material.alphaMode = AlphaMode::Blend;
                    }
                }

                int boolValue = 0;
                if (source->Get(AI_MATKEY_TWOSIDED, boolValue) == AI_SUCCESS) {
                    material.doubleSided = boolValue != 0;
                }

                int shadingMode = 0;
                if (source->Get(AI_MATKEY_SHADING_MODEL, shadingMode) == AI_SUCCESS &&
                    (shadingMode == aiShadingMode_NoShading || shadingMode == aiShadingMode_Unlit)) {
                    material.featureBits |= MATERIAL_FEATURES::Unlit;
                }

                if (aiGetMaterialColor(source, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS) {
                    material.emissiveFactor = { color.r, color.g, color.b };
                    if (std::abs(color.r) > 1.0e-6f ||
                        std::abs(color.g) > 1.0e-6f ||
                        std::abs(color.b) > 1.0e-6f) {
                        material.featureBits |= MATERIAL_FEATURES::Emissive;
                    }
                }
                if (source->Get(AI_MATKEY_EMISSIVE_INTENSITY, value) == AI_SUCCESS) {
                    material.emissiveStrength = (std::max)(0.0f, value);
                }

                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE },
                    material.baseColorTexture,
                    "BaseColor");
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT },
                    material.normalTexture,
                    "Normal");
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_GLTF_METALLIC_ROUGHNESS },
                    material.metallicRoughnessTexture,
                    "MetallicRoughness");
                if (source->GetTextureCount(aiTextureType_METALNESS) > 0u ||
                    source->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0u ||
                    source->GetTextureCount(aiTextureType_SHININESS) > 0u ||
                    source->GetTextureCount(aiTextureType_MAYA_SPECULAR_ROUGHNESS) > 0u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] separate metal/roughness/gloss maps are preserved as diagnostics; HIKARI currently expects glTF packed metallicRoughness texture. material=" +
                        material.name);
                }
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP },
                    material.occlusionTexture,
                    "Occlusion");
                if (ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE },
                    material.emissiveTexture,
                    "Emissive")) {
                    material.featureBits |= MATERIAL_FEATURES::Emissive;
                }
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_MAYA_SPECULAR_COLOR, aiTextureType_SPECULAR, aiTextureType_MAYA_SPECULAR },
                    material.specularColorTexture,
                    "SpecularColor");

                if (source->GetTextureCount(aiTextureType_OPACITY) > 0u) {
                    material.alphaMode = AlphaMode::Mask;
                    material.featureBits |= MATERIAL_FEATURES::AlphaMask;
                    material.doubleSided = true;
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] opacity texture is detected, but HIKARI does not have a dedicated opacity slot yet. material=" +
                        material.name);
                }

                const TextureAsset3D* baseColorTexture = FindTextureBySlot(asset, material.baseColorTexture);
                const std::string_view baseColorTextureName =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->name) : std::string_view{};
                const std::string_view baseColorTexturePath =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->sourcePath) : std::string_view{};
                if (MATERIAL_POLICY::HasThinTransparentCue(material.name) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTextureName) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTexturePath)) {
                    material.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
                }

                asset.materials.push_back(std::move(material));
            }

            if (asset.materials.empty()) {
                MaterialAsset fallback{};
                fallback.name = "DefaultAssimpMaterial";
                asset.materials.push_back(std::move(fallback));
            }
        }

        void AppendInfluence(
            std::vector<std::vector<VertexInfluence>>& influences,
            unsigned int vertexId,
            uint16_t joint,
            float weight) {

            if (vertexId >= influences.size() || weight <= 0.0f) {
                return;
            }
            influences[vertexId].push_back({ joint, weight });
        }

        void WriteTopInfluences(
            const std::vector<VertexInfluence>& source,
            SkinnedVertex3D& outVertex) {

            std::array<VertexInfluence, 4> best{};
            for (const VertexInfluence& influence : source) {
                for (size_t i = 0; i < best.size(); ++i) {
                    if (influence.weight <= best[i].weight) {
                        continue;
                    }
                    for (size_t move = best.size() - 1u; move > i; --move) {
                        best[move] = best[move - 1u];
                    }
                    best[i] = influence;
                    break;
                }
            }

            float sum = 0.0f;
            for (const VertexInfluence& influence : best) {
                sum += influence.weight;
            }
            if (sum <= 1.0e-6f) {
                outVertex.joints[0] = 0;
                outVertex.weights[0] = 1.0f;
                return;
            }

            for (size_t i = 0; i < best.size(); ++i) {
                outVertex.joints[i] = best[i].joint;
                outVertex.weights[i] = best[i].weight / sum;
            }
        }

        void ReadAssimpMesh(
            const aiScene& scene,
            unsigned int meshIndex,
            const std::unordered_map<std::string, int>& nodeNameToIndex,
            std::vector<int>& assimpMeshToModelMesh,
            std::vector<int>& meshToSkin,
            ModelAsset& asset) {

            const aiMesh* source = scene.mMeshes[meshIndex];
            if (source == nullptr || source->mNumVertices == 0u) {
                return;
            }

            MeshPrimitive primitive{};
            primitive.name = source->mName.length > 0
                ? ToString(source->mName)
                : "AssimpPrimitive" + std::to_string(meshIndex);
            primitive.materialIndex = source->mMaterialIndex < asset.materials.size()
                ? source->mMaterialIndex
                : 0u;
            primitive.layout = source->HasBones()
                ? VertexLayoutKind::SkinnedPNTTJW
                : VertexLayoutKind::StaticPNTT;

            primitive.staticVertices.resize(source->mNumVertices);
            for (unsigned int i = 0; i < source->mNumVertices; ++i) {
                Vertex3D vertex{};
                vertex.position = ToVec3(source->mVertices[i]);
                if (source->HasNormals()) {
                    vertex.normal = MATH::Normalize(ToVec3(source->mNormals[i]));
                } else {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }
                if (source->HasTangentsAndBitangents()) {
                    vertex.tangent = {
                        source->mTangents[i].x,
                        source->mTangents[i].y,
                        source->mTangents[i].z,
                        1.0f
                    };
                    const MATH::Vec3 normal = MATH::Normalize(vertex.normal);
                    const MATH::Vec3 tangent = MATH::Normalize({ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z });
                    const MATH::Vec3 bitangent = ToVec3(source->mBitangents[i]);
                    vertex.tangent.w = MATH::Dot(MATH::Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                } else {
                    vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                }
                if (source->HasTextureCoords(0u) && source->mTextureCoords[0] != nullptr) {
                    vertex.uv0 = { source->mTextureCoords[0][i].x, source->mTextureCoords[0][i].y };
                }
                if (source->HasTextureCoords(1u) && source->mTextureCoords[1] != nullptr) {
                    vertex.uv1 = { source->mTextureCoords[1][i].x, source->mTextureCoords[1][i].y };
                }
                if (source->HasVertexColors(0u) && source->mColors[0] != nullptr) {
                    vertex.color0 = ToVec4(source->mColors[0][i]);
                }
                primitive.staticVertices[i] = vertex;
            }

            primitive.indices.reserve(static_cast<size_t>(source->mNumFaces) * 3u);
            for (unsigned int faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
                const aiFace& face = source->mFaces[faceIndex];
                if (face.mNumIndices != 3u) {
                    ++asset.importDiagnostics.unsupportedPrimitiveModeCount;
                    continue;
                }
                primitive.indices.push_back(face.mIndices[0]);
                primitive.indices.push_back(face.mIndices[1]);
                primitive.indices.push_back(face.mIndices[2]);
            }

            if (!source->HasNormals()) {
                GeneratePrimitiveNormals(primitive);
                ++asset.importDiagnostics.missingNormalGeneratedCount;
            }
            if (!source->HasTangentsAndBitangents() &&
                primitive.materialIndex < asset.materials.size() &&
                asset.materials[primitive.materialIndex].normalTexture.textureIndex >= 0) {
                GeneratePrimitiveTangents(primitive);
                ++asset.importDiagnostics.missingTangentGeneratedCount;
            }

            if (source->HasBones()) {
                std::vector<std::vector<VertexInfluence>> influences(source->mNumVertices);
                SkeletonAsset skeleton{};
                skeleton.name = primitive.name + "_Skeleton";

                std::unordered_map<int, int> nodeToJoint;
                for (unsigned int boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
                    const aiBone* bone = source->mBones[boneIndex];
                    if (bone == nullptr) {
                        continue;
                    }

                    const std::string boneName = ToString(bone->mName);
                    SkeletonJoint joint{};
                    joint.name = boneName;
                    joint.inverseBindMatrix = ToMat4(bone->mOffsetMatrix);

                    const auto foundNode = nodeNameToIndex.find(boneName);
                    if (foundNode != nodeNameToIndex.end()) {
                        joint.nodeIndex = foundNode->second;
                    }

                    const uint16_t compactJoint = static_cast<uint16_t>(skeleton.joints.size());
                    if (joint.nodeIndex >= 0) {
                        nodeToJoint[joint.nodeIndex] = compactJoint;
                    }
                    skeleton.joints.push_back(std::move(joint));

                    for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                        const aiVertexWeight& weight = bone->mWeights[weightIndex];
                        AppendInfluence(influences, weight.mVertexId, compactJoint, weight.mWeight);
                    }
                }

                for (SkeletonJoint& joint : skeleton.joints) {
                    if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }
                    const int parentNode = asset.nodes[static_cast<size_t>(joint.nodeIndex)].parent;
                    const auto foundParentJoint = nodeToJoint.find(parentNode);
                    joint.parentJoint = foundParentJoint != nodeToJoint.end() ? foundParentJoint->second : -1;
                }

                primitive.skinnedVertices.resize(source->mNumVertices);
                for (unsigned int i = 0; i < source->mNumVertices; ++i) {
                    const Vertex3D& staticVertex = primitive.staticVertices[i];
                    SkinnedVertex3D skinned{};
                    skinned.position = staticVertex.position;
                    skinned.normal = staticVertex.normal;
                    skinned.tangent = staticVertex.tangent;
                    skinned.uv0 = staticVertex.uv0;
                    skinned.uv1 = staticVertex.uv1;
                    skinned.color0 = staticVertex.color0;
                    WriteTopInfluences(influences[i], skinned);
                    primitive.skinnedVertices[i] = skinned;
                }

                meshToSkin[meshIndex] = static_cast<int>(asset.skins.size());
                asset.skins.push_back(std::move(skeleton));
            }

            primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);

            MeshAsset mesh{};
            mesh.name = source->mName.length > 0
                ? ToString(source->mName)
                : "AssimpMesh" + std::to_string(meshIndex);
            mesh.primitives.push_back(std::move(primitive));
            mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
            const int modelMeshIndex = static_cast<int>(asset.meshes.size());
            asset.meshes.push_back(std::move(mesh));
            if (meshIndex < assimpMeshToModelMesh.size()) {
                assimpMeshToModelMesh[meshIndex] = modelMeshIndex;
            }
            ++asset.importDiagnostics.clusteredStaticPrimitiveCount;
        }

        int ReadAssimpNodeRecursive(
            const aiNode& source,
            int parent,
            ModelAsset& asset,
            std::unordered_map<std::string, int>& nodeNameToIndex) {

            ModelNode node{};
            node.name = source.mName.length > 0
                ? ToString(source.mName)
                : "AssimpNode" + std::to_string(asset.nodes.size());
            node.parent = parent;
            node.hasLocalMatrix = true;
            node.localMatrix = ToMat4(source.mTransformation);

            aiVector3D scale{};
            aiVector3D translation{};
            aiQuaternion rotation{};
            source.mTransformation.Decompose(scale, rotation, translation);
            node.localTransform.position = ToVec3(translation);
            node.localTransform.rotation = ToQuat(rotation);
            node.localTransform.scale = ToVec3(scale);

            if (source.mNumMeshes > 0u) {
                node.meshIndex = static_cast<int>(source.mMeshes[0]);
            }

            const int nodeIndex = static_cast<int>(asset.nodes.size());
            asset.nodes.push_back(std::move(node));
            if (parent >= 0 && parent < static_cast<int>(asset.nodes.size())) {
                asset.nodes[static_cast<size_t>(parent)].children.push_back(nodeIndex);
            }
            nodeNameToIndex[asset.nodes[static_cast<size_t>(nodeIndex)].name] = nodeIndex;

            for (unsigned int meshSlot = 1u; meshSlot < source.mNumMeshes; ++meshSlot) {
                ModelNode meshNode{};
                meshNode.name = asset.nodes[static_cast<size_t>(nodeIndex)].name +
                    "/Mesh" +
                    std::to_string(meshSlot);
                meshNode.parent = nodeIndex;
                meshNode.hasLocalMatrix = true;
                meshNode.localMatrix = MATH::Mat4::Identity();
                meshNode.localTransform.scale = { 1.0f, 1.0f, 1.0f };
                meshNode.meshIndex = static_cast<int>(source.mMeshes[meshSlot]);
                const int meshNodeIndex = static_cast<int>(asset.nodes.size());
                asset.nodes.push_back(std::move(meshNode));
                asset.nodes[static_cast<size_t>(nodeIndex)].children.push_back(meshNodeIndex);
            }

            for (unsigned int childIndex = 0; childIndex < source.mNumChildren; ++childIndex) {
                if (source.mChildren[childIndex] != nullptr) {
                    ReadAssimpNodeRecursive(*source.mChildren[childIndex], nodeIndex, asset, nodeNameToIndex);
                }
            }
            return nodeIndex;
        }

        void AssignSkinsToMeshNodes(const std::vector<int>& meshToSkin, ModelAsset& asset) {
            for (ModelNode& node : asset.nodes) {
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(meshToSkin.size())) {
                    continue;
                }
                node.skinIndex = meshToSkin[static_cast<size_t>(node.meshIndex)];
            }
        }

        void RemapNodeMeshIndices(
            const std::vector<int>& assimpMeshToModelMesh,
            ModelAsset& asset) {

            uint32_t missingMeshReferenceCount = 0;
            for (ModelNode& node : asset.nodes) {
                if (node.meshIndex < 0) {
                    continue;
                }

                const int assimpMeshIndex = node.meshIndex;
                if (assimpMeshIndex >= 0 &&
                    assimpMeshIndex < static_cast<int>(assimpMeshToModelMesh.size()) &&
                    assimpMeshToModelMesh[static_cast<size_t>(assimpMeshIndex)] >= 0) {
                    node.meshIndex = assimpMeshToModelMesh[static_cast<size_t>(assimpMeshIndex)];
                    continue;
                }

                node.meshIndex = -1;
                node.skinIndex = -1;
                ++missingMeshReferenceCount;
            }

            if (missingMeshReferenceCount > 0u) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                asset.importDiagnostics.messages.push_back(
                    "[Assimp] node mesh references were dropped because the referenced source meshes were not renderable. count=" +
                    std::to_string(missingMeshReferenceCount));
            }
        }

        void ReadAssimpAnimations(
            const aiScene& scene,
            const std::unordered_map<std::string, int>& nodeNameToIndex,
            ModelAsset& asset) {

            for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
                const aiAnimation* source = scene.mAnimations[animationIndex];
                if (source == nullptr) {
                    continue;
                }

                const double ticksPerSecond = source->mTicksPerSecond > 0.0
                    ? source->mTicksPerSecond
                    : 25.0;

                AnimationClip clip{};
                clip.name = source->mName.length > 0
                    ? ToString(source->mName)
                    : "AssimpClip" + std::to_string(animationIndex);
                clip.durationSec = static_cast<float>(source->mDuration / ticksPerSecond);

                for (unsigned int channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex) {
                    const aiNodeAnim* channel = source->mChannels[channelIndex];
                    if (channel == nullptr) {
                        continue;
                    }

                    const std::string nodeName = ToString(channel->mNodeName);
                    const auto foundNode = nodeNameToIndex.find(nodeName);
                    if (foundNode == nodeNameToIndex.end()) {
                        continue;
                    }

                    if (channel->mNumPositionKeys > 0u) {
                        NodeAnimationChannel out{};
                        out.targetNode = foundNode->second;
                        out.path = AnimationTargetPath::Translation;
                        out.interpolation = AnimationInterpolation::Linear;
                        out.vec3Keys.reserve(channel->mNumPositionKeys);
                        for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex) {
                            const aiVectorKey& key = channel->mPositionKeys[keyIndex];
                            AnimationKeyframe<MATH::Vec3> frame{};
                            frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                            frame.value = ToVec3(key.mValue);
                            out.vec3Keys.push_back(frame);
                        }
                        clip.channels.push_back(std::move(out));
                    }

                    if (channel->mNumRotationKeys > 0u) {
                        NodeAnimationChannel out{};
                        out.targetNode = foundNode->second;
                        out.path = AnimationTargetPath::Rotation;
                        out.interpolation = AnimationInterpolation::Linear;
                        out.quatKeys.reserve(channel->mNumRotationKeys);
                        for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex) {
                            const aiQuatKey& key = channel->mRotationKeys[keyIndex];
                            AnimationKeyframe<MATH::Quat> frame{};
                            frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                            frame.value = ToQuat(key.mValue);
                            out.quatKeys.push_back(frame);
                        }
                        clip.channels.push_back(std::move(out));
                    }

                    if (channel->mNumScalingKeys > 0u) {
                        NodeAnimationChannel out{};
                        out.targetNode = foundNode->second;
                        out.path = AnimationTargetPath::Scale;
                        out.interpolation = AnimationInterpolation::Linear;
                        out.vec3Keys.reserve(channel->mNumScalingKeys);
                        for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex) {
                            const aiVectorKey& key = channel->mScalingKeys[keyIndex];
                            AnimationKeyframe<MATH::Vec3> frame{};
                            frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                            frame.value = ToVec3(key.mValue);
                            out.vec3Keys.push_back(frame);
                        }
                        clip.channels.push_back(std::move(out));
                    }
                }

                if (!clip.channels.empty()) {
                    asset.animations.push_back(std::move(clip));
                }
            }
        }

    } // namespace

    bool LoadModelAssetFromAssimpSource(ModelAsset& asset) {
        const std::filesystem::path sourcePath(asset.GetSourcePath());
        const std::filesystem::path sourceDirectory = sourcePath.parent_path();

        Assimp::Importer importer{};
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

        constexpr unsigned int kPostProcessFlags =
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights |
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType |
            aiProcess_FindInvalidData |
            aiProcess_ValidateDataStructure;

        const aiScene* scene = importer.ReadFile(sourcePath.string(), kPostProcessFlags);
        if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0u) {
            asset.importDiagnostics.messages.push_back(
                "[Assimp] failed to read source: " +
                std::string(importer.GetErrorString()));
            return false;
        }

        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = -1;
        asset.bounds = {};
        asset.importDiagnostics = {};
        asset.importDiagnostics.sourceFormat = "Assimp/FBX";
        asset.importDiagnostics.objectCount = scene->mNumMeshes;
        asset.importDiagnostics.groupCount = scene->mRootNode->mNumChildren;

        ReadAssimpMaterials(*scene, sourceDirectory, asset);

        std::unordered_map<std::string, int> nodeNameToIndex;
        asset.defaultSceneRootNode = ReadAssimpNodeRecursive(*scene->mRootNode, -1, asset, nodeNameToIndex);

        std::vector<int> assimpMeshToModelMesh(scene->mNumMeshes, -1);
        std::vector<int> meshToSkin(scene->mNumMeshes, -1);
        asset.meshes.reserve(scene->mNumMeshes);
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            ReadAssimpMesh(*scene, meshIndex, nodeNameToIndex, assimpMeshToModelMesh, meshToSkin, asset);
        }

        AssignSkinsToMeshNodes(meshToSkin, asset);
        RemapNodeMeshIndices(assimpMeshToModelMesh, asset);
        ReadAssimpAnimations(*scene, nodeNameToIndex, asset);

        BOUNDS::EnsureModelBounds(asset);

        if (asset.meshes.empty()) {
            asset.importDiagnostics.messages.push_back("[Assimp] no renderable triangle mesh was produced");
            return false;
        }
        return true;
    }

} // namespace HIKARI
