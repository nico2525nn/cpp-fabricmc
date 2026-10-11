#include "game/DatapackManager.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <zlib.h>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        static std::atomic<std::uint64_t> next{0};
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            const auto suffix = next.fetch_add(1, std::memory_order_relaxed);
            path = base / ("cppfm-datapack-test-" + std::to_string(suffix));
            std::error_code error;
            if (std::filesystem::create_directory(path, error)) {
#if !defined(_WIN32)
                std::filesystem::permissions(path,
                    std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::replace, error);
                if (error) {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                    throw std::filesystem::filesystem_error(
                        "protect datapack archive test directory", path, error);
                }
#endif
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "create datapack archive test directory", path, error);
        }
        throw std::runtime_error("could not reserve test directory");
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

#if !defined(_WIN32)
class ScopedUmask final {
public:
    explicit ScopedUmask(mode_t mask) : previous_(::umask(mask)) {}
    ScopedUmask(const ScopedUmask&) = delete;
    ScopedUmask& operator=(const ScopedUmask&) = delete;
    ~ScopedUmask() { (void)::umask(previous_); }

private:
    mode_t previous_;
};

bool hasPrivateMode(const std::filesystem::path& path, mode_t expected,
                    bool directory) {
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0 ||
        (directory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)))
        return false;
    return status.st_uid == ::geteuid() &&
           (status.st_mode & 0777) == expected;
}
#endif

struct ZipEntry {
    std::string path;
    std::string contents;
    bool useDataDescriptor = false;
    bool utf8Name = false;
    bool mismatchDataDescriptorFlag = false;
    bool mismatchLocalDescriptorCrc = false;
    bool mismatchLocalDescriptorCompressedSize = false;
    bool mismatchLocalDescriptorUncompressedSize = false;
    bool populateLocalDescriptorValues = false;
};

void append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool writeDeflatedZip(const std::filesystem::path& path,
                      const std::vector<ZipEntry>& entries) {
    struct CentralEntry {
        std::string path;
        std::uint32_t uncompressedSize;
        std::uint16_t centralFlags;
        std::vector<std::uint8_t> compressed;
        std::uint32_t crc;
        std::uint32_t localOffset;
    };
    std::vector<std::uint8_t> archive;
    std::vector<CentralEntry> central;
    central.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.path.size() > 0xffffU || entry.contents.size() > 0xffffffffU)
            return false;
        std::vector<std::uint8_t> input(entry.contents.begin(),
                                        entry.contents.end());
        std::vector<std::uint8_t> compressed(
            compressBound(static_cast<uLong>(input.size())));
        z_stream stream{};
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return false;
        stream.next_in = input.data();
        stream.avail_in = static_cast<uInt>(input.size());
        stream.next_out = compressed.data();
        stream.avail_out = static_cast<uInt>(compressed.size());
        const auto result = deflate(&stream, Z_FINISH);
        const auto compressedSize = stream.total_out;
        deflateEnd(&stream);
        if (result != Z_STREAM_END || compressedSize > 0xffffffffUL)
            return false;
        compressed.resize(static_cast<std::size_t>(compressedSize));
        const auto crc = static_cast<std::uint32_t>(crc32(
            0, input.data(), static_cast<uInt>(input.size())));
        const auto offset = static_cast<std::uint32_t>(archive.size());

        append32(archive, 0x04034b50U);
        append16(archive, 20);
        const auto localFlags = static_cast<std::uint16_t>(
            (entry.useDataDescriptor ? 0x0008U : 0U) |
            (entry.utf8Name ? 0x0800U : 0U));
        const auto centralFlags = static_cast<std::uint16_t>(
            localFlags ^ (entry.mismatchDataDescriptorFlag ? 0x0008U : 0U));
        append16(archive, localFlags);
        append16(archive, 8);
        append16(archive, 0);
        append16(archive, 0);
        const bool leaveLocalValuesBlank = entry.useDataDescriptor &&
            !entry.mismatchDataDescriptorFlag &&
            !entry.populateLocalDescriptorValues;
        auto localCrc = leaveLocalValuesBlank ? 0U : crc;
        auto localCompressedSize = leaveLocalValuesBlank
            ? 0U : static_cast<std::uint32_t>(compressed.size());
        auto localUncompressedSize = leaveLocalValuesBlank
            ? 0U : static_cast<std::uint32_t>(input.size());
        const auto differentNonzeroValue = [](std::uint32_t value) {
            return value == 0xffffffffU ? value - 1U : value + 1U;
        };
        if (entry.useDataDescriptor && entry.mismatchLocalDescriptorCrc)
            localCrc = differentNonzeroValue(crc);
        if (entry.useDataDescriptor &&
            entry.mismatchLocalDescriptorCompressedSize)
            localCompressedSize = differentNonzeroValue(
                static_cast<std::uint32_t>(compressed.size()));
        if (entry.useDataDescriptor &&
            entry.mismatchLocalDescriptorUncompressedSize)
            localUncompressedSize = differentNonzeroValue(
                static_cast<std::uint32_t>(input.size()));
        append32(archive, localCrc);
        append32(archive, localCompressedSize);
        append32(archive, localUncompressedSize);
        append16(archive, static_cast<std::uint16_t>(entry.path.size()));
        append16(archive, 0);
        archive.insert(archive.end(), entry.path.begin(), entry.path.end());
        archive.insert(archive.end(), compressed.begin(), compressed.end());
        if (entry.useDataDescriptor) {
            append32(archive, 0x08074b50U);
            append32(archive, crc);
            append32(archive, static_cast<std::uint32_t>(compressed.size()));
            append32(archive, static_cast<std::uint32_t>(input.size()));
        }
        central.push_back({entry.path,
                           static_cast<std::uint32_t>(input.size()),
                           centralFlags,
                           std::move(compressed), crc, offset});
    }

    const auto centralOffset = static_cast<std::uint32_t>(archive.size());
    for (const auto& entry : central) {
        append32(archive, 0x02014b50U);
        append16(archive, 0x0314U); // Unix creator, ZIP 2.0
        append16(archive, 20);
        append16(archive, entry.centralFlags);
        append16(archive, 8);
        append16(archive, 0);
        append16(archive, 0);
        append32(archive, entry.crc);
        append32(archive, static_cast<std::uint32_t>(entry.compressed.size()));
        append32(archive, entry.uncompressedSize);
        append16(archive,
                 static_cast<std::uint16_t>(entry.path.size()));
        append16(archive, 0);
        append16(archive, 0);
        append16(archive, 0);
        append16(archive, 0);
        append32(archive, 0100644U << 16U);
        append32(archive, entry.localOffset);
        archive.insert(archive.end(), entry.path.begin(), entry.path.end());
    }
    if (entries.size() > 0xffffU) return false;
    const auto centralSize = static_cast<std::uint32_t>(
        archive.size() - centralOffset);
    append32(archive, 0x06054b50U);
    append16(archive, 0);
    append16(archive, 0);
    append16(archive, static_cast<std::uint16_t>(entries.size()));
    append16(archive, static_cast<std::uint16_t>(entries.size()));
    append32(archive, centralSize);
    append32(archive, centralOffset);
    append16(archive, 0);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(archive.data()),
                 static_cast<std::streamsize>(archive.size()));
    return static_cast<bool>(output);
}

std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void write32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[shift / 8] = static_cast<std::uint8_t>(value >> shift);
}

bool corruptDeclaredCrc(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 22) return false;
    const auto fileSize = static_cast<std::uint64_t>(end);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        bytes[0] != 0x50 || bytes[1] != 0x4b ||
        bytes[2] != 0x03 || bytes[3] != 0x04)
        return false;
    const auto eocd = bytes.size() - 22;
    if (read32(bytes.data() + eocd) != 0x06054b50U) return false;
    const auto centralOffset = read32(bytes.data() + eocd + 16);
    if (centralOffset > bytes.size() - 46 ||
        read32(bytes.data() + centralOffset) != 0x02014b50U)
        return false;
    const auto localCrc = read32(bytes.data() + 14);
    const auto centralCrc = read32(bytes.data() + centralOffset + 16);
    if (localCrc != centralCrc) return false;
    const auto invalidCrc = localCrc ^ 1U;
    write32(bytes.data() + 14, invalidCrc);
    write32(bytes.data() + centralOffset + 16, invalidCrc);
    file.clear();
    file.seekp(0);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool corruptDataDescriptorCrc(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 22) return false;
    const auto fileSize = static_cast<std::uint64_t>(end);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) return false;
    const auto eocd = bytes.size() - 22;
    if (read32(bytes.data() + eocd) != 0x06054b50U) return false;
    const auto centralOffset = read32(bytes.data() + eocd + 16);
    if (centralOffset > bytes.size() - 46 ||
        read32(bytes.data() + centralOffset) != 0x02014b50U)
        return false;
    const auto localOffset = read32(bytes.data() + centralOffset + 42);
    if (localOffset > bytes.size() - 30 ||
        read32(bytes.data() + localOffset) != 0x04034b50U)
        return false;
    const auto localNameLength =
        static_cast<std::uint64_t>(bytes[localOffset + 26]) |
        (static_cast<std::uint64_t>(bytes[localOffset + 27]) << 8U);
    const auto localExtraLength =
        static_cast<std::uint64_t>(bytes[localOffset + 28]) |
        (static_cast<std::uint64_t>(bytes[localOffset + 29]) << 8U);
    const auto compressedSize = read32(bytes.data() + centralOffset + 20);
    const auto descriptorOffset = localOffset + 30 + localNameLength +
                                  localExtraLength + compressedSize;
    if (descriptorOffset > bytes.size() - 16 ||
        read32(bytes.data() + descriptorOffset) != 0x08074b50U)
        return false;
    write32(bytes.data() + descriptorOffset + 4,
            read32(bytes.data() + descriptorOffset + 4) ^ 1U);
    file.clear();
    file.seekp(0);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

const std::vector<ZipEntry> kValidPack = {
    {"pack.mcmeta",
     R"({"pack":{"pack_format":61,"description":"archive fixture"}})"},
    {"data/zipprobe/function/from_zip.mcfunction",
     "say loaded from a compressed datapack\n"},
    {"data/zipprobe/tags/item/zip_test.json",
     R"({"replace":true,"values":["minecraft:emerald"]})"},
    {"data/zipprobe/loot_table/blocks/archive_probe.json",
     R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald"}]}]})"},
    {"data/zipprobe/recipe/zip_diamond_block.json",
     R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:emerald"}],"result":{"id":"minecraft:diamond_block","count":1}})"},
    {"data/ziporder/tags/item/sort.json",
     R"({"replace":true,"values":["minecraft:diamond"]})"},
    {"data/ziporder/loot_table/blocks/sort_probe.json",
     R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})"},
};

bool hasLoadedZipContent(
    const cppfm::DatapackManager& datapacks,
    std::string_view expectedPriorityItem = "minecraft:emerald") {
    const auto function = datapacks.getFunction("zipprobe:from_zip");
    const auto tag = datapacks.tagManager.itemTags.find("zipprobe:zip_test");
    const auto loot = datapacks.lootTables.evaluateRegisteredTable(
        "zipprobe:blocks/archive_probe", {}, nullptr, 1);
    const auto sortedTag = datapacks.tagManager.itemTags.find("ziporder:sort");
    const auto sortedLoot = datapacks.lootTables.evaluateRegisteredTable(
        "ziporder:blocks/sort_probe", {}, nullptr, 1);
    const auto emerald = cppfm::gen::itemIdByName().at("minecraft:emerald");
    const auto expectedPriorityId =
        cppfm::gen::itemIdByName().at(std::string(expectedPriorityItem));
    return datapacks.isAvailable("compressed") &&
        datapacks.isEnabled("compressed") && function &&
        function->size() == 1 &&
        function->front() == "say loaded from a compressed datapack" &&
        tag != datapacks.tagManager.itemTags.end() &&
        tag->second == std::unordered_set<std::uint32_t>{emerald} &&
        loot && loot->size() == 1 &&
        loot->front().name() == "minecraft:emerald" &&
        sortedTag != datapacks.tagManager.itemTags.end() &&
        sortedTag->second ==
            std::unordered_set<std::uint32_t>{expectedPriorityId} &&
        sortedLoot && sortedLoot->size() == 1 &&
        sortedLoot->front().name() == expectedPriorityItem;
}

std::map<std::string, std::set<std::string>> liveExtractionWorkspaceTree() {
    std::map<std::string, std::set<std::string>> result;
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) throw std::filesystem::filesystem_error(
        "locate temp directory for datapack cleanup test", error);
    for (std::filesystem::directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (!name.starts_with("cppfm-datapack-")) continue;
        std::set<std::string> children;
        for (std::filesystem::directory_iterator child(it->path(), error), end;
             !error && child != end; child.increment(error)) {
            std::error_code statusError;
            if (child->is_directory(statusError) && !statusError)
                children.insert(child->path().filename().string());
            if (statusError) error = statusError;
        }
        if (error) throw std::filesystem::filesystem_error(
            "scan temporary datapack workspace", it->path(), error);
        result.emplace(name, std::move(children));
    }
    if (error) throw std::filesystem::filesystem_error(
        "scan temp directory for datapack cleanup test", root, error);
    return result;
}

std::set<std::string> liveExtractionWorkspaces() {
    std::set<std::string> result;
    for (const auto& [name, children] : liveExtractionWorkspaceTree()) {
        (void)children;
        result.insert(name);
    }
    return result;
}

void testCompressedDatapackLifecycle() {
    TempDirectory temp;
    const auto packRoot = temp.path / "world" / "datapacks";
    std::filesystem::create_directories(packRoot);
    const auto archivePath = packRoot / "compressed.zip";
    const auto folderData = packRoot / "z-folder" / "data" / "ziporder";
    std::filesystem::create_directories(
        folderData / "tags" / "item");
    std::filesystem::create_directories(
        folderData / "loot_table" / "blocks");
    std::filesystem::create_directories(folderData / "recipe");
    {
        std::ofstream tag(folderData / "tags" / "item" / "sort.json");
        tag << R"({"replace":true,"values":["minecraft:emerald"]})";
        std::ofstream loot(folderData / "loot_table" / "blocks" /
                           "sort_probe.json");
        loot << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald"}]}]})";
        std::ofstream recipe(folderData / "recipe" / "folder_sandstone.json");
        recipe << R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:emerald"}],"result":{"id":"minecraft:sandstone","count":1}})";
    }
    check(writeDeflatedZip(archivePath, kValidPack),
          "write a raw-deflate ZIP datapack fixture");

    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    cppfm::DatapackManager datapacks;
    datapacks.loadAll(recipes, (temp.path / "assets").string(),
                      packRoot.string());
    check(hasLoadedZipContent(datapacks),
          "ZIP datapack loads function/tag/loot resources and mixed folder/ZIP priority follows pack names");
    check(recipes.findById("zipprobe:zip_diamond_block") != nullptr &&
              recipes.findById("ziporder:folder_sandstone") != nullptr,
          "directory and ZIP datapack recipes load from 1.21.4 namespaced recipe paths");

    const bool disabled = datapacks.disablePack("compressed") &&
        !datapacks.getFunction("zipprobe:from_zip") &&
        !datapacks.tagManager.itemTags.contains("zipprobe:zip_test") &&
        datapacks.lootTables.find("zipprobe:blocks/archive_probe") == nullptr &&
        recipes.findById("zipprobe:zip_diamond_block") == nullptr &&
        recipes.findById("ziporder:folder_sandstone") != nullptr;
    check(disabled, "disabling a ZIP datapack removes all three resource types");

    datapacks.loadAll(recipes, (temp.path / "assets").string(),
                      packRoot.string());
    check(datapacks.isAvailable("compressed") &&
              !datapacks.isEnabled("compressed") &&
              !datapacks.getFunction("zipprobe:from_zip") &&
              recipes.findById("zipprobe:zip_diamond_block") == nullptr,
          "reload preserves an explicit disabled choice for a ZIP pack");
    check(datapacks.enablePack("compressed") &&
              hasLoadedZipContent(datapacks, "minecraft:diamond") &&
              recipes.findById("zipprobe:zip_diamond_block") != nullptr,
          "re-enabled ZIP pack is extracted and becomes the highest-priority pack");
}

void testRejectedArchivesPreservePublishedGeneration() {
    TempDirectory temp;
    const auto validRoot = temp.path / "valid";
    std::filesystem::create_directories(validRoot);
    check(writeDeflatedZip(validRoot / "compressed.zip", kValidPack),
          "write baseline ZIP for transactional reload test");
    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    cppfm::DatapackManager datapacks;
    datapacks.loadAll(recipes, (temp.path / "assets").string(),
                      validRoot.string());
    const bool baselinePublished =
        hasLoadedZipContent(datapacks, "minecraft:diamond");

    const auto corruptRoot = temp.path / "corrupt";
    std::filesystem::create_directories(corruptRoot);
    const auto corruptArchive = corruptRoot / "corrupt.zip";
    const bool corruptFixtureReady =
        writeDeflatedZip(corruptArchive, kValidPack) &&
        corruptDeclaredCrc(corruptArchive);
    bool crcFailureRejected = false;
    if (corruptFixtureReady) {
        try {
            datapacks.loadAll(recipes, (temp.path / "assets").string(),
                              corruptRoot.string());
        } catch (const std::exception&) {
            crcFailureRejected = true;
        }
    }
    check(corruptFixtureReady && crcFailureRejected &&
              hasLoadedZipContent(datapacks, "minecraft:diamond"),
          "CRC failure rejects reload without replacing the live datapack generation");

    const auto unsafeRoot = temp.path / "unsafe";
    std::filesystem::create_directories(unsafeRoot);
    const bool unsafeFixtureReady = writeDeflatedZip(
        unsafeRoot / "unsafe.zip",
        {{"data/example/function/noop.mcfunction", "say no\n"},
         {"../escape.txt", "must not be extracted\n"}});
    bool traversalRejected = false;
    if (unsafeFixtureReady) {
        try {
            datapacks.loadAll(recipes, (temp.path / "assets").string(),
                              unsafeRoot.string());
        } catch (const std::exception&) {
            traversalRejected = true;
        }
    }
    check(unsafeFixtureReady && traversalRejected && baselinePublished &&
              hasLoadedZipContent(datapacks, "minecraft:diamond"),
          "path traversal in a ZIP is rejected before publishing or replacing live resources");
}

void testPortableArchivePathValidation() {
    constexpr std::array<std::string_view, 11> unsafePaths = {
        "data/.. /escape.txt",
        "data/../escape.txt",
        "data/trailing./file.json",
        "data/trailing-space /file.json",
        "data/CON.txt",
        "data/aux/config.json",
        "data/COM1.log",
        "data/path/file:stream",
        "data/path/name?.json",
        "data/path/control\x1f.json",
        "C:/outside.txt",
    };
    for (const auto path : unsafePaths) {
        bool rejected = false;
        try {
            (void)cppfm::datapack_archive::checkedRelativePath(
                std::string(path));
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Windows-ambiguous or escaping ZIP path is rejected");
    }
    for (const auto prefix : {std::string_view("COM"),
                              std::string_view("LPT")}) {
        for (const auto& suffix : {std::string(1, static_cast<char>(0xb2)),
                                   std::string(1, static_cast<char>(0xb3)),
                                   std::string(1, static_cast<char>(0xb9)),
                                   std::string("\xc2\xb2", 2),
                                   std::string("\xc2\xb3", 2),
                                   std::string("\xc2\xb9", 2)}) {
            const auto path = std::string("data/") + std::string(prefix) +
                              suffix + ".txt";
            bool rejected = false;
            try {
                (void)cppfm::datapack_archive::checkedRelativePath(path);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected,
                  "Windows superscript COM/LPT device aliases are rejected");
        }
    }
    check(cppfm::datapack_archive::checkedRelativePath(
              "data/valid/functions/probe.mcfunction") ==
              "data/valid/functions/probe.mcfunction",
          "ordinary portable datapack paths remain accepted");

    TempDirectory temp;
    const auto archivePath = temp.path / "case-collision.zip";
    const bool fixtureReady = writeDeflatedZip(archivePath, {
        {"data/example/function/Probe.mcfunction", "say first\n"},
        {"data/example/function/probe.mcfunction", "say second\n"},
    });
    bool collisionRejected = false;
    if (fixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(archivePath);
        } catch (const std::exception&) {
            collisionRejected = true;
        }
    }
    check(fixtureReady && collisionRejected,
          "case-insensitive duplicate archive paths are rejected portably");

#ifdef _WIN32
    const auto unicodeCaseCollisionPath = temp.path / "unicode-case-collision.zip";
    const bool unicodeCaseCollisionFixtureReady = writeDeflatedZip(
        unicodeCaseCollisionPath, {
            {"data/example/function/\xc3\x84.mcfunction", "say upper\n",
             false, true},
            {"data/example/function/\xc3\xa4.mcfunction", "say lower\n",
             false, true},
        });
    bool unicodeCaseCollisionRejected = false;
    if (unicodeCaseCollisionFixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(unicodeCaseCollisionPath);
        } catch (const std::exception&) {
            unicodeCaseCollisionRejected = true;
        }
    }
    check(unicodeCaseCollisionFixtureReady && unicodeCaseCollisionRejected,
          "Windows rejects Unicode case-insensitive duplicate ZIP paths");
#endif

    const auto unicodePath = temp.path / "unicode-names.zip";
    const std::string cp437Path =
        std::string("data/unicode/caf") + static_cast<char>(0x82) + ".txt";
    const std::string utf8Path =
        "data/unicode/emoji-\xf0\x9f\xa7\xaa.txt";
    const bool unicodeFixtureReady = writeDeflatedZip(unicodePath, {
        {cp437Path, "cp437\n"},
        {utf8Path, "utf8\n", false, true},
    });
    bool unicodeNamesExtracted = false;
    if (unicodeFixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(unicodePath);
            const auto output = temp.path / "unicode-output";
            archive.extractTo(output);
            unicodeNamesExtracted =
                std::filesystem::exists(output /
                    cppfm::datapack_archive::pathFromUtf8(
                        "data/unicode/caf\xc3\xa9.txt")) &&
                std::filesystem::exists(output /
                    cppfm::datapack_archive::pathFromUtf8(utf8Path));
        } catch (const std::exception&) {
            unicodeNamesExtracted = false;
        }
    }
    check(unicodeFixtureReady && unicodeNamesExtracted,
          "UTF-8 and legacy CP437 ZIP paths extract through portable filesystem paths");

    const auto invalidUtf8Path = temp.path / "invalid-utf8-name.zip";
    const auto malformedName = std::string("data/invalid/") +
                               static_cast<char>(0xff) + ".txt";
    const bool invalidUtf8FixtureReady = writeDeflatedZip(
        invalidUtf8Path, {{malformedName, "bad\n", false, true}});
    bool invalidUtf8Rejected = false;
    if (invalidUtf8FixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(invalidUtf8Path);
        } catch (const std::exception&) {
            invalidUtf8Rejected = true;
        }
    }
    check(invalidUtf8FixtureReady && invalidUtf8Rejected,
          "malformed UTF-8 names with the ZIP UTF-8 flag are rejected");
}

void testDataDescriptorValidation() {
    TempDirectory temp;
    const std::vector<ZipEntry> descriptorEntries = {
        {"data/descriptor/function/probe.mcfunction", "say descriptor\n", true},
    };
    const auto validPath = temp.path / "descriptor.zip";
    const bool fixtureReady = writeDeflatedZip(validPath, descriptorEntries);
    bool validDescriptorRead = false;
    if (fixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(validPath);
            const auto output = temp.path / "valid-output";
            archive.extractTo(output);
            std::ifstream function(
                output / "data/descriptor/function/probe.mcfunction");
            std::string line;
            validDescriptorRead = std::getline(function, line) &&
                                  line == "say descriptor";
        } catch (const std::exception&) {
            validDescriptorRead = false;
        }
    }
    check(fixtureReady && validDescriptorRead,
          "zero local CRC/sizes with a valid signed ZIP data descriptor are accepted and extracted");

    const auto matchingLocalValuesPath = temp.path / "matching-local-values.zip";
    ZipEntry matchingLocalValuesEntry{
        "data/descriptor/matching-local-values.mcfunction",
        "say matching local values\n", true};
    matchingLocalValuesEntry.populateLocalDescriptorValues = true;
    bool matchingLocalValuesAccepted = false;
    if (writeDeflatedZip(matchingLocalValuesPath, {matchingLocalValuesEntry})) {
        try {
            cppfm::datapack_archive::Archive archive(matchingLocalValuesPath);
            const auto output = temp.path / "matching-local-values-output";
            archive.extractTo(output);
            matchingLocalValuesAccepted = std::filesystem::exists(
                output / "data/descriptor/matching-local-values.mcfunction");
        } catch (const std::exception&) {
            matchingLocalValuesAccepted = false;
        }
    }
    check(matchingLocalValuesAccepted,
          "bit-3 local CRC/sizes matching the central directory are accepted with a valid descriptor");

    const auto corruptPath = temp.path / "corrupt-descriptor.zip";
    const auto corruptOutput = temp.path / "corrupt-output";
    const bool corruptFixtureReady =
        writeDeflatedZip(corruptPath, descriptorEntries) &&
        corruptDataDescriptorCrc(corruptPath);
    bool corruptDescriptorRejected = false;
    if (corruptFixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(corruptPath);
            archive.extractTo(corruptOutput);
        } catch (const std::exception&) {
            corruptDescriptorRejected = true;
        }
    }
    check(corruptFixtureReady && corruptDescriptorRejected &&
              !std::filesystem::exists(corruptOutput),
          "a mismatched ZIP data descriptor is rejected and its partial extraction is removed");

    const auto mismatchedFlagsPath = temp.path / "mismatched-descriptor-flags.zip";
    const bool mismatchedFlagsFixtureReady = writeDeflatedZip(
        mismatchedFlagsPath,
        {{"data/descriptor/flags.mcfunction", "say mismatched flags\n",
          true, false, true}});
    bool mismatchedFlagsRejected = false;
    if (mismatchedFlagsFixtureReady) {
        try {
            cppfm::datapack_archive::Archive archive(mismatchedFlagsPath);
            archive.extractTo(temp.path / "mismatched-flags-output");
        } catch (const std::exception&) {
            mismatchedFlagsRejected = true;
        }
    }
    check(mismatchedFlagsFixtureReady && mismatchedFlagsRejected,
          "local and central ZIP data-descriptor flags must match exactly");

    const auto rejectsMismatchedLocalValue = [&](const char* filename,
                                                  bool mismatchCrc,
                                                  bool mismatchCompressedSize,
                                                  bool mismatchUncompressedSize) {
        ZipEntry entry{"data/descriptor/local-mismatch.mcfunction",
                       "say local mismatch\n", true};
        entry.mismatchLocalDescriptorCrc = mismatchCrc;
        entry.mismatchLocalDescriptorCompressedSize = mismatchCompressedSize;
        entry.mismatchLocalDescriptorUncompressedSize =
            mismatchUncompressedSize;
        const auto path = temp.path / filename;
        if (!writeDeflatedZip(path, {entry})) return false;
        try {
            cppfm::datapack_archive::Archive archive(path);
            archive.extractTo(temp.path / (std::string(filename) + "-output"));
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    check(rejectsMismatchedLocalValue("local-crc-mismatch.zip", true, false,
                                      false),
          "bit-3 ZIP entries reject nonzero local CRC values that disagree with the central directory");
    check(rejectsMismatchedLocalValue("local-compressed-size-mismatch.zip",
                                      false, true, false),
          "bit-3 ZIP entries reject nonzero local compressed sizes that disagree with the central directory");
    check(rejectsMismatchedLocalValue("local-uncompressed-size-mismatch.zip",
                                      false, false, true),
          "bit-3 ZIP entries reject nonzero local uncompressed sizes that disagree with the central directory");
}

void testExtractionRootSymlinkIsNotFollowed() {
#if !defined(_WIN32)
    TempDirectory temp;
    const auto archivePath = temp.path / "symlink-root.zip";
    const auto target = temp.path / "outside-target";
    const auto extractionLink = temp.path / "extraction-link";
    const bool fixtureReady = writeDeflatedZip(archivePath, {
        {"data/should-not-appear.txt", "outside write\n"},
    });
    std::error_code error;
    std::filesystem::create_directory(target, error);
    if (!error)
        std::filesystem::create_directory_symlink(target, extractionLink, error);
    bool rejected = false;
    if (fixtureReady && !error) {
        try {
            cppfm::datapack_archive::Archive archive(archivePath);
            archive.extractTo(extractionLink);
        } catch (const std::exception&) {
            rejected = true;
        }
    }
    check(fixtureReady && !error && rejected &&
              !std::filesystem::exists(target / "data/should-not-appear.txt") &&
              std::filesystem::is_symlink(extractionLink),
          "extraction refuses a symlink root without touching its target");
#else
    std::puts("  SKIP POSIX symlink-root race check is not available on Windows");
#endif
}

void testWorkspaceAggregateExtractionLimit() {
    TempDirectory temp;
    const auto firstArchive = temp.path / "first.zip";
    const auto secondArchive = temp.path / "second.zip";
    const std::vector<ZipEntry> firstEntries = {
        {"data/first/one.txt", "1234"},
    };
    const std::vector<ZipEntry> secondEntries = {
        {"data/second/two.txt", "5678"},
    };
    const bool fixturesReady = writeDeflatedZip(firstArchive, firstEntries) &&
        writeDeflatedZip(secondArchive, secondEntries);
    bool aggregateLimitEnforced = false;
    bool releasedReservationReused = false;
    if (fixturesReady) {
        cppfm::datapack_archive::Workspace workspace(6);
        const auto firstDataRoot = workspace.extract(firstArchive);
        try {
            (void)workspace.extract(secondArchive);
        } catch (const std::exception&) {
            aggregateLimitEnforced = true;
        }
        const bool firstPackStillPresent = std::filesystem::exists(
            firstDataRoot / "first/one.txt");
        workspace.discardExtractedDataRoot(firstDataRoot);
        try {
            const auto secondDataRoot = workspace.extract(secondArchive);
            releasedReservationReused = std::filesystem::exists(
                secondDataRoot / "second/two.txt");
        } catch (const std::exception&) {
            releasedReservationReused = false;
        }
        aggregateLimitEnforced = aggregateLimitEnforced &&
                                 firstPackStillPresent;
    }
    check(fixturesReady && aggregateLimitEnforced && releasedReservationReused,
          "workspace caps aggregate ZIP output and releases discarded extraction reservations");

    const auto firstZeroEntryArchive = temp.path / "first-zero-entry.zip";
    const auto secondZeroEntryArchive = temp.path / "second-zero-entry.zip";
    const std::vector<ZipEntry> firstZeroEntryFiles = {
        {"data/first/zero-one.txt", ""},
        {"data/first/zero-two.txt", ""},
    };
    const std::vector<ZipEntry> secondZeroEntryFiles = {
        {"data/second/zero-one.txt", ""},
        {"data/second/zero-two.txt", ""},
    };
    const bool zeroEntryFixturesReady =
        writeDeflatedZip(firstZeroEntryArchive, firstZeroEntryFiles) &&
        writeDeflatedZip(secondZeroEntryArchive, secondZeroEntryFiles);
    bool aggregateEntryLimitEnforced = false;
    bool releasedEntryReservationReused = false;
    if (zeroEntryFixturesReady) {
        cppfm::datapack_archive::Workspace workspace(16, 3);
        const auto firstDataRoot = workspace.extract(firstZeroEntryArchive);
        try {
            (void)workspace.extract(secondZeroEntryArchive);
        } catch (const std::exception&) {
            aggregateEntryLimitEnforced = true;
        }
        const bool firstPackStillPresent = std::filesystem::exists(
            firstDataRoot / "first/zero-one.txt");
        workspace.discardExtractedDataRoot(firstDataRoot);
        try {
            const auto secondDataRoot = workspace.extract(secondZeroEntryArchive);
            releasedEntryReservationReused = std::filesystem::exists(
                secondDataRoot / "second/zero-two.txt");
        } catch (const std::exception&) {
            releasedEntryReservationReused = false;
        }
        aggregateEntryLimitEnforced = aggregateEntryLimitEnforced &&
                                      firstPackStillPresent;
    }
    check(zeroEntryFixturesReady && aggregateEntryLimitEnforced &&
              releasedEntryReservationReused,
          "workspace caps aggregate ZIP entry counts, including zero-byte files, and releases discarded entry reservations");
}

void testPrivateExtractionPermissions() {
#if !defined(_WIN32)
    TempDirectory temp;
    const auto archivePath = temp.path / "private-permissions.zip";
    const bool fixtureReady = writeDeflatedZip(archivePath, {
        {"data/private/nested/payload.txt", "private datapack data\n"},
    });
    check(fixtureReady, "create private-permissions ZIP fixture");
    if (!fixtureReady) return;

    // A permissive umask exposes accidental dependence on the caller's umask.
    ScopedUmask permissiveUmask(0);
    try {
        cppfm::datapack_archive::Workspace workspace;
        const auto dataRoot = workspace.extract(archivePath);
        const auto extractionRoot = dataRoot.parent_path();
        const auto workspaceRoot = extractionRoot.parent_path();
        check(hasPrivateMode(workspaceRoot, 0700, true),
              "datapack workspace is owner-only despite umask 000");
        check(hasPrivateMode(extractionRoot, 0700, true),
              "archive extraction root is owner-only despite umask 000");
        check(hasPrivateMode(dataRoot / "private", 0700, true) &&
                  hasPrivateMode(dataRoot / "private/nested", 0700, true),
              "all extracted directories are owner-only despite umask 000");
        check(hasPrivateMode(dataRoot / "private/nested/payload.txt", 0600,
                             false),
              "extracted datapack file is owner-only despite umask 000");
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "permission test extraction threw: %s\n",
                     exception.what());
        check(false, "private datapack extraction completes under umask 000");
    }

    // A fully restrictive umask must not leave the owner's workspace or files
    // inaccessible; permissions are restored through pinned directory/file fds.
    {
        ScopedUmask restrictiveUmask(0777);
        try {
            cppfm::datapack_archive::Workspace workspace;
            const auto dataRoot = workspace.extract(archivePath);
            const auto extractionRoot = dataRoot.parent_path();
            const auto workspaceRoot = extractionRoot.parent_path();
            check(hasPrivateMode(workspaceRoot, 0700, true),
                  "datapack workspace is restored to 0700 under umask 0777");
            check(hasPrivateMode(extractionRoot, 0700, true),
                  "archive extraction root is restored to 0700 under umask 0777");
            check(hasPrivateMode(dataRoot / "private/nested", 0700, true),
                  "nested extraction directory is restored to 0700 under umask 0777");
            check(hasPrivateMode(dataRoot / "private/nested/payload.txt", 0600,
                                 false),
                  "extracted file is restored to 0600 under umask 0777");
        } catch (const std::exception& exception) {
            std::fprintf(stderr,
                         "restrictive-umask extraction threw: %s\n",
                         exception.what());
            check(false,
                  "private datapack extraction completes under umask 0777");
        }
    }
#else
    std::puts("  SKIP POSIX permission-bit checks are not available on Windows; inherited ACL restriction is not guaranteed");
#endif
}

void testRecipeBookCategoryLoading() {
    TempDirectory temp;
    const auto root = temp.path / "recipe-categories";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        check(false, "create recipe-category fixture directory");
        return;
    }

    struct CategoryFixture {
        const char* id;
        const char* type;
        const char* category;
        int expectedCategory;
        cppfm::Recipe::CookingStation cookingStation;
        int expectedCookingTicks = 0;
        const char* ingredient = "minecraft:iron_ingot";
    };
    const std::vector<CategoryFixture> fixtures = {
        {"building", "crafting_shaped", "building", 0,
         cppfm::Recipe::CookingStation::Furnace},
        {"redstone", "crafting_shapeless", "redstone", 1,
         cppfm::Recipe::CookingStation::Furnace},
        {"equipment", "crafting_shapeless", "equipment", 2,
         cppfm::Recipe::CookingStation::Furnace},
        {"misc", "crafting_shapeless", "misc", 3,
         cppfm::Recipe::CookingStation::Furnace},
        {"furnace_food", "smelting", "food", 4,
         cppfm::Recipe::CookingStation::Furnace, 37, "minecraft:beef"},
        {"furnace_blocks", "smelting", "blocks", 5,
         cppfm::Recipe::CookingStation::Furnace, 211, "minecraft:iron_ore"},
        {"furnace_misc", "smelting", "misc", 6,
         cppfm::Recipe::CookingStation::Furnace, 83, "minecraft:sand"},
        {"blast_blocks", "blasting", "blocks", 7,
         cppfm::Recipe::CookingStation::BlastFurnace, 19,
         "minecraft:gold_ore"},
        {"blast_misc", "blasting", "misc", 8,
         cppfm::Recipe::CookingStation::BlastFurnace, 41,
         "minecraft:diamond_ore"},
        {"smoker_food", "smoking", "food", 9,
         cppfm::Recipe::CookingStation::Smoker, 53,
         "minecraft:porkchop"},
        {"stonecutter", "stonecutting", "", 10,
         cppfm::Recipe::CookingStation::Furnace},
        {"smithing", "smithing_transform", "", 11,
         cppfm::Recipe::CookingStation::Furnace},
        {"campfire", "campfire_cooking", "food", 12,
         cppfm::Recipe::CookingStation::Campfire, 71, "minecraft:cod"},
    };

    bool fixturesReady = true;
    for (const auto& fixture : fixtures) {
        std::string document = "{\"type\":\"minecraft:";
        document += fixture.type;
        document += "\",\"category\":\"";
        document += fixture.category;
        document += "\",\"result\":{\"id\":\"minecraft:iron_ingot\"}";
        if (std::string_view(fixture.type) == "crafting_shaped") {
            document += ",\"pattern\":[\"X\"],\"key\":{\"X\":{\"item\":\"minecraft:iron_ingot\"}}";
        } else if (std::string_view(fixture.type) == "crafting_shapeless") {
            document += ",\"ingredients\":[{\"item\":\"minecraft:iron_ingot\"}]";
        } else if (std::string_view(fixture.type) == "smithing_transform") {
            document += ",\"template\":{\"item\":\"minecraft:netherite_upgrade_smithing_template\"},\"base\":{\"item\":\"minecraft:iron_sword\"},\"addition\":{\"item\":\"minecraft:netherite_ingot\"}";
        } else {
            document += ",\"ingredient\":{\"item\":\"";
            document += fixture.ingredient;
            document += "\"}";
        }
        if (std::string_view(fixture.type) == "smelting" ||
            std::string_view(fixture.type) == "smoking" ||
            std::string_view(fixture.type) == "blasting" ||
            std::string_view(fixture.type) == "campfire_cooking") {
            document += ",\"experience\":0.5,\"cookingtime\":";
            document += std::to_string(fixture.expectedCookingTicks);
        }
        document += "}";

        std::ofstream output(root / (std::string(fixture.id) + ".json"),
                             std::ios::binary | std::ios::trunc);
        output << document;
        fixturesReady = fixturesReady && static_cast<bool>(output);
    }

    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    if (fixturesReady) recipes.loadDirectory(root.string());
    bool allMappingsCorrect = fixturesReady;
    bool cookingContractsCorrect = fixturesReady;
    for (const auto& fixture : fixtures) {
        const auto* recipe = recipes.findById(
            std::string("minecraft:") + fixture.id);
        allMappingsCorrect = allMappingsCorrect && recipe &&
            recipe->category == fixture.expectedCategory;
        if (recipe && recipe->kind == cppfm::Recipe::Kind::Smelting)
            allMappingsCorrect = allMappingsCorrect &&
                                 recipe->cookingStation ==
                                     fixture.cookingStation;
        if (recipe && recipe->kind == cppfm::Recipe::Kind::Smelting) {
            const auto input = cppfm::gen::itemIdByName().at(fixture.ingredient);
            const auto* stationRecipe = recipes.findCooking(
                input, fixture.cookingStation);
            cookingContractsCorrect = cookingContractsCorrect &&
                recipe->cookingTicks == fixture.expectedCookingTicks &&
                stationRecipe && stationRecipe->id == recipe->id;
            for (const auto station : {
                     cppfm::Recipe::CookingStation::Furnace,
                     cppfm::Recipe::CookingStation::BlastFurnace,
                     cppfm::Recipe::CookingStation::Smoker,
                     cppfm::Recipe::CookingStation::Campfire}) {
                if (station != fixture.cookingStation)
                    cookingContractsCorrect = cookingContractsCorrect &&
                        recipes.findCooking(input, station) == nullptr;
            }
        }
    }
    check(allMappingsCorrect,
          "JSON recipe categories map to the 1.21.4 recipe-book registry and cooking stations");
    check(cookingContractsCorrect,
          "cookingtime is retained and recipes are only returned for their declared station");
}

void testFailedEnableReleasesOnlyItsExtraction() {
    TempDirectory temp;
    const auto packRoot = temp.path / "world" / "datapacks";
    std::filesystem::create_directories(packRoot);
    const std::vector<ZipEntry> goodEntries = {
        {"pack.mcmeta", R"({"pack":{"pack_format":61,"description":"good"}})"},
        {"data/good/function/ping.mcfunction", "say pong\n"},
    };
    const std::vector<ZipEntry> brokenEntries = {
        {"pack.mcmeta", R"({"pack":{"pack_format":61,"description":"broken"}})"},
        {"data/broken/predicate/invalid.json", "{not-json"},
    };
    const bool fixturesReady =
        writeDeflatedZip(packRoot / "good.zip", goodEntries) &&
        writeDeflatedZip(packRoot / "broken.zip", brokenEntries);
    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    cppfm::DatapackManager datapacks;
    datapacks.disabledPacks.insert("broken");
    bool initialLoadSucceeded = false;
    if (fixturesReady) {
        try {
            datapacks.loadAll(recipes, (temp.path / "assets").string(),
                              packRoot.string());
            initialLoadSucceeded = datapacks.isEnabled("good") &&
                !datapacks.isEnabled("broken");
        } catch (const std::exception&) {
            initialLoadSucceeded = false;
        }
    }
    const auto beforeEnable = liveExtractionWorkspaceTree();
    bool invalidPackRejected = false;
    if (initialLoadSucceeded) {
        try {
            (void)datapacks.enablePack("broken");
        } catch (const std::exception&) {
            invalidPackRejected = true;
        }
    }
    check(fixturesReady && initialLoadSucceeded && invalidPackRejected &&
              datapacks.isEnabled("good") && !datapacks.isEnabled("broken") &&
              liveExtractionWorkspaceTree() == beforeEnable,
          "failed ZIP enable preserves live packs and removes only its new extraction");
}

} // namespace

int main() {
    const auto workspacesBefore = liveExtractionWorkspaces();
    testPrivateExtractionPermissions();
    testCompressedDatapackLifecycle();
    testRejectedArchivesPreservePublishedGeneration();
    testPortableArchivePathValidation();
    testDataDescriptorValidation();
    testExtractionRootSymlinkIsNotFollowed();
    testWorkspaceAggregateExtractionLimit();
    testRecipeBookCategoryLoading();
    testFailedEnableReleasesOnlyItsExtraction();
    check(liveExtractionWorkspaces() == workspacesBefore,
          "successful and failed archive generations release only their owned extraction workspaces");
    std::printf("datapack_archive: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
