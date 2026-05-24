#include "HIKARI_AssetImporterRegistry.h"

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    void AssetImporterRegistry::Register(std::unique_ptr<IAssetImporter> importer) {
        if (!importer || importer->GetImporterId() == nullptr || importer->GetImporterId()[0] == '\0') {
            HIKARI_LOG_WARN("[AssetImporter] Skip invalid importer registration.");
            return;
        }

        if (FindById(importer->GetImporterId()) != nullptr) {
            HIKARI_LOG_WARN(std::string("[AssetImporter] Duplicate importer ignored: ") + importer->GetImporterId());
            return;
        }

        importers_.push_back(std::move(importer));
    }

    const IAssetImporter* AssetImporterRegistry::FindById(std::string_view importerId) const {
        for (const auto& importer : importers_) {
            if (importer && importerId == importer->GetImporterId()) {
                return importer.get();
            }
        }
        return nullptr;
    }

    IAssetImporter* AssetImporterRegistry::FindById(std::string_view importerId) {
        for (auto& importer : importers_) {
            if (importer && importerId == importer->GetImporterId()) {
                return importer.get();
            }
        }
        return nullptr;
    }

    const IAssetImporter* AssetImporterRegistry::FindForSource(const std::filesystem::path& sourcePath) const {
        for (const auto& importer : importers_) {
            if (importer && importer->CanImport(sourcePath)) {
                return importer.get();
            }
        }
        return nullptr;
    }

    IAssetImporter* AssetImporterRegistry::FindForSource(const std::filesystem::path& sourcePath) {
        for (auto& importer : importers_) {
            if (importer && importer->CanImport(sourcePath)) {
                return importer.get();
            }
        }
        return nullptr;
    }

} // namespace HIKARI
