#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../platform/StorageFileOps.hpp"

namespace cppfm::datapack_detail {

class LooseDataLimitExceeded : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One budget is shared by all loose datapack readers during a reload. This
// keeps untrusted directory packs within the same envelope regardless of which
// resource loader (tags, recipes, loot, predicates, or functions) reads them.
struct LooseDataBudget {
    static constexpr std::size_t kDefaultMaxFiles = 100'000;
    static constexpr std::size_t kDefaultMaxFileBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxTotalBytes = 512u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxFunctionLines = 1'000'000;
    static constexpr std::size_t kDefaultMaxDirectoryEntries = 500'000;
    static constexpr std::size_t kDefaultMaxResolvedTagEntries = 2'000'000;
    static constexpr std::size_t kDefaultMaxTagReferences = 250'000;
    static constexpr std::size_t kDefaultMaxTagResolutionWork = 10'000'000;

    explicit LooseDataBudget(
        std::size_t maxFiles = kDefaultMaxFiles,
        std::size_t maxFileBytes = kDefaultMaxFileBytes,
        std::size_t maxTotalBytes = kDefaultMaxTotalBytes,
        std::size_t maxFunctionLines = kDefaultMaxFunctionLines,
        std::size_t maxDirectoryEntries = kDefaultMaxDirectoryEntries,
        std::size_t maxResolvedTagEntries =
            kDefaultMaxResolvedTagEntries,
        std::size_t maxTagReferences = kDefaultMaxTagReferences,
        std::size_t maxTagResolutionWork =
            kDefaultMaxTagResolutionWork) noexcept
        : maxFiles(maxFiles), maxFileBytes(maxFileBytes),
          maxTotalBytes(maxTotalBytes),
          maxFunctionLines(maxFunctionLines),
          maxDirectoryEntries(maxDirectoryEntries),
          maxResolvedTagEntries(maxResolvedTagEntries),
          maxTagReferences(maxTagReferences),
          maxTagResolutionWork(maxTagResolutionWork) {}

    std::size_t maxFiles;
    std::size_t maxFileBytes;
    std::size_t maxTotalBytes;
    std::size_t maxFunctionLines;
    std::size_t maxDirectoryEntries;
    std::size_t maxResolvedTagEntries;
    std::size_t maxTagReferences;
    std::size_t maxTagResolutionWork;
    std::size_t files = 0;
    std::size_t totalBytes = 0;
    std::size_t functionLines = 0;
    std::size_t directoryEntries = 0;
    std::size_t resolvedTagEntries = 0;
    std::size_t tagReferences = 0;
    std::size_t tagResolutionWork = 0;

    void recordDirectoryEntry(const std::filesystem::path& path) {
        if (directoryEntries >= maxDirectoryEntries)
            throw LooseDataLimitExceeded(
                "datapack directory-entry limit exceeded: " + pathText(path));
        ++directoryEntries;
    }

    void recordResolvedTagEntry(const std::filesystem::path& path = {}) {
        if (resolvedTagEntries >= maxResolvedTagEntries)
            throw LooseDataLimitExceeded(
                "datapack resolved-tag entry limit exceeded" +
                (path.empty() ? std::string{} : ": " + pathText(path)));
        ++resolvedTagEntries;
    }

    void recordResolvedTagEntries(std::size_t count) {
        if (count > maxResolvedTagEntries -
                        std::min(resolvedTagEntries, maxResolvedTagEntries))
            throw LooseDataLimitExceeded(
                "datapack resolved-tag entry limit exceeded");
        resolvedTagEntries += count;
    }

    void recordTagReference(const std::filesystem::path& path = {}) {
        if (tagReferences >= maxTagReferences)
            throw LooseDataLimitExceeded(
                "datapack tag-reference limit exceeded" +
                (path.empty() ? std::string{} : ": " + pathText(path)));
        ++tagReferences;
    }

    void recordTagResolutionWork() {
        if (tagResolutionWork >= maxTagResolutionWork)
            throw LooseDataLimitExceeded(
                "datapack tag-resolution work limit exceeded");
        ++tagResolutionWork;
    }

    void recordFile(const std::filesystem::path& path) {
        if (files >= maxFiles)
            throw LooseDataLimitExceeded(
                "datapack loose-resource file limit exceeded: " +
                pathText(path));
        ++files;
    }

    void recordFunctionLine(const std::filesystem::path& path) {
        if (functionLines >= maxFunctionLines)
            throw LooseDataLimitExceeded(
                "datapack loose-function line limit exceeded: " +
                pathText(path));
        ++functionLines;
    }

    std::string readFile(const std::filesystem::path& path) {
        namespace fs = std::filesystem;
        std::error_code readError;
        storage_file_ops::RegularFileReader input;
        if (!input.open(path, readError))
            throw fs::filesystem_error("open datapack resource", path,
                                       readError);
        const auto declaredSize = input.sizeBytes();
        const auto remaining = maxTotalBytes -
            std::min(totalBytes, maxTotalBytes);
        const auto limit = static_cast<std::uint64_t>(
            std::min(maxFileBytes, remaining));
        if (declaredSize > limit)
            throw LooseDataLimitExceeded(
                "datapack loose-resource byte limit exceeded: " +
                pathText(path));

        std::string contents;
        contents.reserve(static_cast<std::size_t>(declaredSize));
        char buffer[8192];
        for (;;) {
            std::size_t count = 0;
            const auto status = input.read(buffer, sizeof(buffer), count,
                                           readError);
            if (status == storage_file_ops::RegularFileReader::ReadStatus::Failed)
                throw fs::filesystem_error("read datapack resource", path,
                                           readError);
            if (status == storage_file_ops::RegularFileReader::ReadStatus::End)
                break;
            if (count > limit - contents.size())
                throw LooseDataLimitExceeded(
                    "datapack loose-resource byte limit exceeded: " +
                    pathText(path));
            contents.append(buffer, count);
        }
        if (contents.size() != declaredSize)
            throw fs::filesystem_error("read complete datapack resource", path,
                std::make_error_code(std::errc::io_error));
        if (!input.close(readError))
            throw fs::filesystem_error("close datapack resource", path,
                                       readError);
        totalBytes += contents.size();
        return contents;
    }

private:
    static std::string pathText(const std::filesystem::path& path) {
        const auto encoded = path.u8string();
        std::string result;
        result.reserve(encoded.size());
        for (const char8_t byte : encoded)
            result.push_back(static_cast<char>(byte));
        return result;
    }
};

} // namespace cppfm::datapack_detail
