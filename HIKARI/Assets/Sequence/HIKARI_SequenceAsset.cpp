#include "Assets/Sequence/HIKARI_SequenceAsset.h"

#include <fstream>

#include <json.hpp>

#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"

namespace HIKARI {

    namespace {
        void SetError(std::string* outError, std::string message) {
            if (outError != nullptr) {
                *outError = std::move(message);
            }
        }
    }

    bool LoadSequenceAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        SequenceAsset& outAsset,
        std::string* outError) {

        std::ifstream input(path);
        if (!input.is_open()) {
            SetError(outError, "Unable to open sequence asset");
            return false;
        }
        nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
        if (!root.is_object()) {
            SetError(outError, "Invalid sequence asset JSON");
            return false;
        }
        const uint32_t version = root.value("formatVersion", 0u);
        if (version == 0 || version > SequenceAsset::kFormatVersion) {
            SetError(outError, "Unsupported sequence asset version");
            return false;
        }
        const auto sequenceIt = root.find("sequence");
        if (sequenceIt == root.end() || !sequenceIt->is_object()) {
            SetError(outError, "Sequence asset has no sequence payload");
            return false;
        }

        SequenceAsset loaded{};
        loaded.guid = authoritativeGuid;
        if (!loaded.guid.IsValid()) {
            loaded.guid.value = root.value("guid", std::string{});
        }
        loaded.displayName = root.value("displayName", path.stem().string());
        DeserializeCinematicSequenceJson(*sequenceIt, loaded.sequence);
        if (loaded.displayName.empty()) {
            loaded.displayName = loaded.sequence.name;
        }
        outAsset = std::move(loaded);
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }

    bool SaveSequenceAsset(
        const std::filesystem::path& path,
        const SequenceAsset& asset,
        std::string* outError) {

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            SetError(outError, "Unable to create sequence asset directory");
            return false;
        }
        CinematicSequence normalized = asset.sequence;
        NormalizeCinematicSequence(normalized);
        nlohmann::json sequenceJson{};
        SerializeCinematicSequenceJson(normalized, sequenceJson);
        nlohmann::json root{
            { "assetType", "Sequence" },
            { "formatVersion", SequenceAsset::kFormatVersion },
            { "guid", asset.guid.value },
            { "displayName", asset.displayName.empty()
                ? normalized.name
                : asset.displayName },
            { "sequence", std::move(sequenceJson) }
        };

        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open()) {
            SetError(outError, "Unable to write sequence asset");
            return false;
        }
        output << root.dump(2) << '\n';
        if (!output.good()) {
            SetError(outError, "Failed while writing sequence asset");
            return false;
        }
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }

} // namespace HIKARI
