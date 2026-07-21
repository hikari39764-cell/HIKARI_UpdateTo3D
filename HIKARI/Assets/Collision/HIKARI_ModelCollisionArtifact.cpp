#include "Assets/Collision/HIKARI_ModelCollisionArtifact.h"

#include <algorithm>
#include <filesystem>

#include "Assets/Collision/HIKARI_HcollisionFormat.h"
#include "Assets/Collision/HIKARI_ModelCollisionCompiler.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        std::filesystem::path ResolvePath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            return path.is_absolute()
                ? path.lexically_normal()
                : (projectRoot / path).lexically_normal();
        }

        bool ReplaceWithTemp(
            const std::filesystem::path& temporary,
            const std::filesystem::path& finalPath,
            std::string& outMessage) {

            std::error_code ec{};
            std::filesystem::remove(finalPath, ec);
            ec.clear();
            std::filesystem::rename(temporary, finalPath, ec);
            if (ec) {
                std::filesystem::remove(temporary);
                outMessage = "failed to replace HCOLLISION artifact: " +
                    ec.message();
                return false;
            }
            return true;
        }
    }

    ModelCollisionArtifactResult BuildModelCollisionArtifact(
        const AssetRecord& record,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& importedDirectory) {

        ModelCollisionArtifactResult result{};
        const std::filesystem::path setupPath = ResolvePath(
            projectRoot,
            GetModelCollisionSetupPath(record));
        ModelCollisionSetup setup{};
        const ModelCollisionSetupLoadResult load = LoadModelCollisionSetup(
            setupPath,
            record.guid.value,
            setup);
        if (!load.success) {
            result.message = load.message;
            return result;
        }

        const std::filesystem::path finalPath =
            ResolvePath(projectRoot, importedDirectory) /
            "collision_geometry.hcollision";
        const std::filesystem::path tempPath =
            ResolvePath(projectRoot, importedDirectory) /
            "collision_geometry.importing.hcollision";
        result.path = finalPath;

        const bool hasEnabledShape = std::any_of(
            setup.shapes.begin(),
            setup.shapes.end(),
            [](const ModelCollisionShape& shape) {
                return shape.enabled;
            });
        if (!load.exists || !hasEnabledShape) {
            std::error_code ec{};
            std::filesystem::remove(tempPath, ec);
            ec.clear();
            std::filesystem::remove(finalPath, ec);
            result.success = true;
            result.message = load.exists
                ? "collision setup has no enabled shapes"
                : "model has no collision setup";
            return result;
        }

        CollisionGeometryAsset asset{};
        const ModelCollisionCompileResult compile =
            CompileModelCollisionSetup(setup, asset);
        if (!compile.success) {
            result.message = compile.message;
            return result;
        }

        std::error_code cleanupEc{};
        std::filesystem::remove(tempPath, cleanupEc);
        std::string writeMessage{};
        if (!WriteHcollisionFile(tempPath, asset, writeMessage) ||
            !ReplaceWithTemp(tempPath, finalPath, result.message)) {
            if (result.message.empty()) {
                result.message = writeMessage;
            }
            return result;
        }

        result.success = true;
        result.ready = true;
        result.shapeCount = compile.shapeCount;
        result.message = compile.message;
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
