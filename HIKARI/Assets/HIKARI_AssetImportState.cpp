#include "HIKARI_AssetImportState.h"

#include "HIKARI_AssetRecord.h"

namespace HIKARI {

    AssetImportState GetImportState(const AssetRecord& record) {
        if (record.duplicateGuid) {
            return AssetImportState::DuplicateGuid;
        }
        if (!record.sourceExists) {
            return AssetImportState::MissingSource;
        }
        if (!record.metaExists) {
            return AssetImportState::MissingMeta;
        }
        if (record.importerMissing) {
            return AssetImportState::UnknownImporter;
        }
        if (record.artifactMissing) {
            return AssetImportState::MissingArtifact;
        }
        if (record.importOutdated) {
            return AssetImportState::Outdated;
        }
        if (!record.lastImportSucceeded && !record.lastImportMessage.empty()) {
            return AssetImportState::ImportFailed;
        }
        if (record.lastImportSucceeded) {
            return AssetImportState::Imported;
        }
        return AssetImportState::MetaOnly;
    }

    const char* ToString(AssetImportState state) {
        switch (state) {
        case AssetImportState::MetaOnly: return "Meta Only";
        case AssetImportState::Imported: return "Imported";
        case AssetImportState::Outdated: return "Outdated";
        case AssetImportState::MissingSource: return "Missing Source";
        case AssetImportState::MissingMeta: return "Missing Meta";
        case AssetImportState::MissingArtifact: return "Missing Artifact";
        case AssetImportState::UnknownImporter: return "Unknown Importer";
        case AssetImportState::DuplicateGuid: return "Duplicate GUID";
        case AssetImportState::ImportFailed: return "Import Failed";
        case AssetImportState::Unknown:
        default: return "Unknown";
        }
    }

} // namespace HIKARI
