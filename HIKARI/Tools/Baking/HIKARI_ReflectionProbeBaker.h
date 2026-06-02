#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace TOOLS::BAKING {

        struct ReflectionProbeBakeRequest {
            std::filesystem::path projectRoot{};
            std::string sceneGuid{};
            std::string sceneName{};
            MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
            float radius = 8.0f;
            float intensity = 1.0f;
            uint32_t resolution = 128;
            uint32_t prefilteredMipCount = 7;
            uint32_t prefilteredSampleCount = 128;
            uint32_t brdfLutSize = 256;
            uint32_t brdfSampleCount = 256;
            bool forceRebake = false;
            std::filesystem::path sourceCubemapPath{};
        };

        struct ReflectionProbeBakeResult {
            bool success = false;
            bool captured = false;
            bool prefiltered = false;
            bool brdfReady = false;
            ASSETS::LIGHTING::ReflectionProbeBakeRecord record{};
            std::filesystem::path capturePath{};
            std::filesystem::path prefilteredPath{};
            std::filesystem::path brdfLutPath{};
            std::vector<std::string> messages{};
            std::vector<std::string> warnings{};
            std::vector<std::string> errors{};
        };

        class ReflectionProbeBaker {
        public:
            ReflectionProbeBakeResult BakeSingleProbe(
                DocumentSceneBase& scene,
                const ReflectionProbeBakeRequest& request) const;
        };

    } // namespace TOOLS::BAKING

} // namespace HIKARI
