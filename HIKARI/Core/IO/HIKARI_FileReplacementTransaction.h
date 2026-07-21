#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace HIKARI::IO {

    struct FileReplacementOperation {
        std::filesystem::path finalPath{};
        std::filesystem::path stagedPath{};
        bool deleteFinal = false;
    };

    // Commits a group of same-volume file replacements as one transaction.
    // Existing files are moved to private backups and restored on any error.
    bool CommitFileReplacementTransaction(
        std::span<const FileReplacementOperation> operations,
        std::string& outMessage);

} // namespace HIKARI::IO
