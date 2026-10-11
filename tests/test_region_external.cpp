#define CPPFM_STORAGE_FILE_OPS_TESTING
#include "game/RegionFile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
    if (!condition) ++failures;
}

void writeBytes(const fs::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty())
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("cannot write region fixture");
}

void writeRegion(const fs::path& path, std::uint8_t compression,
                 const std::vector<std::uint8_t>& payload, bool external,
                 std::int32_t localX = 0, std::int32_t localZ = 0,
                 std::uint32_t timestamp = 0) {
    if (localX < 0 || localX >= 32 || localZ < 0 || localZ >= 32)
        throw std::runtime_error("invalid local chunk fixture coordinates");
    const std::size_t sectors = external
        ? 1
        : (5 + payload.size() + 4095) / 4096;
    if (sectors == 0 || sectors > 255)
        throw std::runtime_error("invalid region fixture sector count");
    std::vector<std::uint8_t> region((2 + sectors) * 4096, 0);
    const auto index = static_cast<std::size_t>(localX) +
                       static_cast<std::size_t>(localZ) * 32;
    region[index * 4 + 2] = 2; // fixture chunk begins at sector two
    region[index * 4 + 3] = static_cast<std::uint8_t>(sectors);
    const auto timestampOffset = 4096 + index * 4;
    region[timestampOffset] = static_cast<std::uint8_t>(timestamp >> 24);
    region[timestampOffset + 1] = static_cast<std::uint8_t>(timestamp >> 16);
    region[timestampOffset + 2] = static_cast<std::uint8_t>(timestamp >> 8);
    region[timestampOffset + 3] = static_cast<std::uint8_t>(timestamp);
    const auto recordLength = static_cast<std::uint32_t>(
        external ? 1 : payload.size() + 1);
    const auto dataOffset = 2U * 4096U;
    region[dataOffset] = static_cast<std::uint8_t>(recordLength >> 24);
    region[dataOffset + 1] = static_cast<std::uint8_t>(recordLength >> 16);
    region[dataOffset + 2] = static_cast<std::uint8_t>(recordLength >> 8);
    region[dataOffset + 3] = static_cast<std::uint8_t>(recordLength);
    region[dataOffset + 4] = static_cast<std::uint8_t>(
        compression | (external ? 0x80U : 0U));
    if (!external)
        std::copy(payload.begin(), payload.end(),
                  region.begin() + static_cast<std::ptrdiff_t>(dataOffset + 5));
    writeBytes(path, region);
}

bool hasExternalStub(const fs::path& path, std::int32_t localX,
                     std::int32_t localZ) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const auto index = static_cast<std::uint64_t>(localX) +
                       static_cast<std::uint64_t>(localZ) * 32U;
    std::array<std::uint8_t, 4> location{};
    file.seekg(static_cast<std::streamoff>(index * 4U));
    file.read(reinterpret_cast<char*>(location.data()), location.size());
    if (!file) return false;
    const auto offset = (static_cast<std::uint32_t>(location[0]) << 16) |
                        (static_cast<std::uint32_t>(location[1]) << 8) |
                        static_cast<std::uint32_t>(location[2]);
    if (offset < 2 || location[3] != 1) return false;
    std::array<std::uint8_t, 5> stub{};
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset) * 4096);
    file.read(reinterpret_cast<char*>(stub.data()), stub.size());
    return file && stub[0] == 0 && stub[1] == 0 && stub[2] == 0 &&
           stub[3] == 1 && stub[4] == 0x82;
}

bool hasSidecarTemporaryArtifacts(const fs::path& directory,
                                  const fs::path& sidecar) {
    const auto prefix = sidecar.filename().string() + ".cppfm-";
    for (const auto& entry : fs::directory_iterator(directory))
        if (entry.path().filename().string().starts_with(prefix)) return true;
    return false;
}

std::vector<std::uint8_t> makeLargeIncompressibleChunk() {
    std::mt19937 generator(0x21a4U);
    std::vector<std::uint8_t> bytes(1'200'000);
    for (auto& byte : bytes)
        byte = static_cast<std::uint8_t>(generator());
    return bytes;
}

} // namespace

int main() {
#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(::getpid());
#endif
    auto directory = fs::temp_directory_path() /
        ("cppfm-region-external-" + std::to_string(processId));
    std::error_code error;
    bool created = false;
    for (unsigned suffix = 0; suffix < 100; ++suffix) {
        auto candidate = directory;
        if (suffix != 0) candidate += "-" + std::to_string(suffix);
        error.clear();
        if (fs::create_directory(candidate, error)) {
            directory = std::move(candidate);
            created = true;
            break;
        }
        if (error && error != std::errc::file_exists) break;
    }
    if (!created) {
        std::cerr << "FAIL: create unique test directory: " << error.message()
                  << '\n';
        return 1;
    }

    const auto regionPath = directory / "r.0.0.mca";
    const auto sidecarPath = directory / "c.0.0.mcc";
    const std::vector<std::uint8_t> rawPayload{
        'e', 'x', 't', 'e', 'r', 'n', 'a', 'l'
    };
    // Golden sidecar bytes emitted by lz4-java 1.8.0's default
    // LZ4BlockOutputStream constructor for the payload "external".
    const std::vector<std::uint8_t> compressedPayload{
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x16, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x2e, 0xdf, 0x16, 0x05,
        0x65, 0x78, 0x74, 0x65, 0x72, 0x6e, 0x61, 0x6c,
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    // RFC 1952 fixture from `gzip -n -c` over "external" (mtime omitted).
    const std::vector<std::uint8_t> gzipPayload{
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
        0x4b, 0xad, 0x28, 0x49, 0x2d, 0xca, 0x4b, 0xcc, 0x01, 0x00,
        0xb8, 0xa1, 0x52, 0x58, 0x08, 0x00, 0x00, 0x00
    };
    try {
        writeRegion(regionPath, 3, rawPayload, true);
        writeBytes(sidecarPath, rawPayload);
        check(cppfm::RegionFile(regionPath.string()).load(0, 0) == rawPayload,
              "external raw codec reads coordinate-named .mcc payload");

        writeRegion(regionPath, 4, compressedPayload, true);
        writeBytes(sidecarPath, compressedPayload);
        check(cppfm::RegionFile(regionPath.string()).load(0, 0) == rawPayload,
              "external LZ4Block codec reads independent lz4-java 1.8.0 sidecar bytes");

        writeRegion(regionPath, 4, compressedPayload, false);
        check(cppfm::RegionFile(regionPath.string()).load(0, 0) == rawPayload,
              "inline LZ4Block codec reads the same stream format");

        writeRegion(regionPath, 1, gzipPayload, false);
        check(cppfm::RegionFile(regionPath.string()).load(0, 0) == rawPayload,
              "Anvil gzip codec reads an independent RFC 1952 fixture");

        auto concatenatedGzip = gzipPayload;
        concatenatedGzip.insert(concatenatedGzip.end(), gzipPayload.begin(),
                                gzipPayload.end());
        std::vector<std::uint8_t> concatenatedOutput;
        cppfm::decompressGzipUnknown(concatenatedGzip.data(),
                                     concatenatedGzip.size(),
                                     concatenatedOutput, 64);
        const std::vector<std::uint8_t> twiceExternal{
            'e', 'x', 't', 'e', 'r', 'n', 'a', 'l',
            'e', 'x', 't', 'e', 'r', 'n', 'a', 'l'
        };
        check(concatenatedOutput == twiceExternal,
              "gzip decoder accepts concatenated Java-compatible members");

        auto gzipWithTrailingByte = gzipPayload;
        gzipWithTrailingByte.push_back(0);
        bool trailingGzipRejected = false;
        try {
            cppfm::decompressGzipUnknown(gzipWithTrailingByte.data(),
                                         gzipWithTrailingByte.size(),
                                         concatenatedOutput, 64);
        } catch (const std::exception&) {
            trailingGzipRejected = true;
        }
        check(trailingGzipRejected,
              "gzip decoder rejects trailing non-member bytes");
        auto gzipWithBadCrc = gzipPayload;
        gzipWithBadCrc[20] ^= 1;
        bool badGzipCrcRejected = false;
        try {
            cppfm::decompressGzipUnknown(gzipWithBadCrc.data(),
                                         gzipWithBadCrc.size(),
                                         concatenatedOutput, 64);
        } catch (const std::exception&) {
            badGzipCrcRejected = true;
        }
        check(badGzipCrcRejected,
              "gzip decoder validates the RFC 1952 CRC trailer");
        bool truncatedGzipRejected = false;
        try {
            cppfm::decompressGzipUnknown(gzipPayload.data(),
                                         gzipPayload.size() - 1,
                                         concatenatedOutput, 64);
        } catch (const std::exception&) {
            truncatedGzipRejected = true;
        }
        check(truncatedGzipRejected,
              "gzip decoder rejects a truncated trailer");
        bool gzipOutputCapEnforced = false;
        try {
            cppfm::decompressGzipUnknown(gzipPayload.data(), gzipPayload.size(),
                                         concatenatedOutput, 7);
        } catch (const std::length_error&) {
            gzipOutputCapEnforced = true;
        }
        check(gzipOutputCapEnforced,
              "gzip decoder enforces the aggregate output limit");

        const auto malformedSidecarRegion = directory / "r.5.0.mca";
        const auto malformedSidecarPath = directory / "c.160.0.mcc";
        writeRegion(malformedSidecarRegion, 4, compressedPayload, true);
        writeBytes(malformedSidecarPath, {0xde, 0xad});
        bool malformedCompressedSidecarIsCorruption = false;
        try {
            (void)cppfm::RegionFile(malformedSidecarRegion.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            malformedCompressedSidecarIsCorruption = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(malformedCompressedSidecarIsCorruption,
              "invalid known-codec sidecar is corruption and can be skipped while salvaging siblings");

        const auto emptyCompressedRegion = directory / "r.4.0.mca";
        const std::vector<std::uint8_t> validEmptyZlib{
            0x78, 0x9c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01
        };
        writeRegion(emptyCompressedRegion, 2, validEmptyZlib, false);
        bool occupiedEmptyChunkIsCorruption = false;
        try {
            (void)cppfm::RegionFile(emptyCompressedRegion.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            occupiedEmptyChunkIsCorruption = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(occupiedEmptyChunkIsCorruption,
              "occupied chunk with a valid zero-output zlib stream is corruption, never missing");

        const std::vector<std::uint8_t> stagedPayload{
            's', 't', 'a', 'g', 'e', 'd'
        };
        writeRegion(regionPath, 3, rawPayload, true, 0, 0, 7);
        writeBytes(sidecarPath, rawPayload);
        const auto stagedPath = fs::path(regionPath.string() + ".new");
        fs::copy_file(regionPath, stagedPath,
                      fs::copy_options::overwrite_existing);
        const auto stagedSidecarPath = fs::path(sidecarPath.string() + ".new");
        writeBytes(stagedSidecarPath, stagedPayload);
        const auto stagedExternalFailsClosed = [&] {
            try {
                (void)cppfm::RegionFile(stagedPath.string()).load(0, 0);
            } catch (const cppfm::RegionLoadError& failure) {
                return failure.kind() ==
                    cppfm::RegionLoadFailureKind::Unsupported;
            }
            return false;
        };
        check(stagedExternalFailsClosed(),
              "staged external MCA is not accepted without a coordinated sidecar transaction");
        cppfm::RegionFile stagedRegion(stagedPath.string());
        cppfm::RegionFile canonicalRegion(regionPath.string());
        check(stagedRegion.loadWithCanonicalExternalReference(
                  canonicalRegion, 0, 0) == stagedPayload,
              "staged external MCA reads its paired staged sidecar rather than the primary generation");
        writeBytes(stagedSidecarPath, {});
        bool corruptStagedSidecarRejected = false;
        try {
            (void)stagedRegion.loadWithCanonicalExternalReference(
                canonicalRegion, 0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            corruptStagedSidecarRejected = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(corruptStagedSidecarRejected,
              "corrupt staged sidecar does not silently fall back to primary content");
        fs::remove(stagedSidecarPath);
        check(stagedRegion.loadWithCanonicalExternalReference(
                  canonicalRegion, 0, 0) == rawPayload,
              "unchanged staged stub may reuse its exact canonical sidecar generation");
        writeRegion(regionPath, 3, rawPayload, true, 0, 0, 8);
        bool mismatchedPrimaryStubFailsClosed = false;
        try {
            (void)stagedRegion.loadWithCanonicalExternalReference(
                canonicalRegion, 0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            mismatchedPrimaryStubFailsClosed =
                failure.kind() ==
                cppfm::RegionLoadFailureKind::Unsupported;
        }
        check(mismatchedPrimaryStubFailsClosed,
              "staged external recovery rejects a same-codec primary stub from another generation");
        writeRegion(regionPath, 3, rawPayload, true, 0, 0, 7);
        check(stagedExternalFailsClosed(),
              "staged external MCA does not fall back to a different sidecar generation");
        check(stagedRegion.loadWithCanonicalExternalReference(
                  canonicalRegion, 0, 0) == rawPayload,
              "staged external recovery remains bound to the canonical sidecar after staged sidecar removal");

        const auto savedMca = [&] {
            std::ifstream file(regionPath, std::ios::binary);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }();
        const auto savedMcc = [&] {
            std::ifstream file(sidecarPath, std::ios::binary);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }();
        writeBytes(sidecarPath, {});
        bool malformedSidecarIsCorruption = false;
        try {
            (void)cppfm::RegionFile(regionPath.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            malformedSidecarIsCorruption = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(malformedSidecarIsCorruption,
              "malformed external sidecar is classified as chunk corruption for sibling salvage");
        writeBytes(sidecarPath, savedMcc);
        fs::remove(sidecarPath);
        bool preservedFailure = false;
        try {
            (void)cppfm::RegionFile(regionPath.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            preservedFailure = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(preservedFailure,
              "missing external sidecar fails closed instead of treating it as an absent chunk");
        bool stagedCanonicalSidecarMissingFailsClosed = false;
        try {
            (void)stagedRegion.loadWithCanonicalExternalReference(
                canonicalRegion, 0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            stagedCanonicalSidecarMissingFailsClosed =
                failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(stagedCanonicalSidecarMissingFailsClosed,
              "staged external recovery fails closed when its canonical sidecar is missing");
        check([&] {
                  std::ifstream file(regionPath, std::ios::binary);
                  return std::vector<std::uint8_t>(
                             std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()) == savedMca;
              }(),
              "failed external load leaves the MCA source unchanged");
        writeBytes(sidecarPath, savedMcc);

        const auto malformed = directory / "r.1.0.mca";
        writeRegion(malformed, 4, compressedPayload, true);
        bool missingCoordinatesFailClosed = false;
        try {
            (void)cppfm::RegionFile(malformed.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError& failure) {
            missingCoordinatesFailClosed = failure.kind() ==
                cppfm::RegionLoadFailureKind::Corruption;
        }
        check(missingCoordinatesFailClosed,
              "external sidecar lookup uses region and local chunk coordinates");

        const auto negativeRegion = directory / "r.-1.0.mca";
        const auto negativeSidecar = directory / "c.-1.0.mcc";
        writeRegion(negativeRegion, 3, rawPayload, true, 31, 0);
        writeBytes(negativeSidecar, rawPayload);
        check(cppfm::RegionFile(negativeRegion.string()).load(31, 0) ==
                  rawPayload,
              "negative region coordinates map local chunk 31 to world chunk -1");

        const auto shortHighOffset = directory / "r.2.0.mca";
        std::vector<std::uint8_t> damagedHeader(8192, 0);
        damagedHeader[0] = 0xff;
        damagedHeader[1] = 0xff;
        damagedHeader[2] = 0x00;
        damagedHeader[3] = 1; // sector 0xffff00, absent from the short file
        writeBytes(shortHighOffset, damagedHeader);
        cppfm::RegionFile(shortHighOffset.string()).store(0, 0, rawPayload);
        std::ifstream repairedHeader(shortHighOffset, std::ios::binary);
        std::array<std::uint8_t, 4> repairedLocation{};
        repairedHeader.read(reinterpret_cast<char*>(repairedLocation.data()),
                            static_cast<std::streamsize>(
                                repairedLocation.size()));
        check(fs::file_size(shortHighOffset) == 3 * 4096 &&
                  repairedLocation[0] == 0 && repairedLocation[1] == 0 &&
                  repairedLocation[2] == 2 && repairedLocation[3] == 1 &&
                  cppfm::RegionFile(shortHighOffset.string()).load(0, 0) ==
                      rawPayload,
              "store ignores a high sector pointer beyond physical EOF instead of creating a huge sparse region");

        const auto largeChunk = makeLargeIncompressibleChunk();
        check(cppfm::RegionFile::canStoreBytes(largeChunk),
              "large bounded chunk is admissible through external Anvil storage");
        const auto externalWriteRegion = directory / "r.12.-3.mca";
        const auto externalWriteSidecar = directory / "c.389.-89.mcc";
        cppfm::RegionFile externalWriter(externalWriteRegion.string());
        externalWriter.store(5, 7, largeChunk);
        const auto externalSidecarBytes = fs::file_size(externalWriteSidecar);
        check(externalSidecarBytes > 255U * 4096U - 5U &&
                  hasExternalStub(externalWriteRegion, 5, 7) &&
                  cppfm::RegionFile(externalWriteRegion.string()).load(5, 7) ==
                      largeChunk,
              "oversized compressed chunk round-trips through a vanilla external stub and coordinate-named .mcc");

        auto replacementChunk = largeChunk;
        replacementChunk[0] ^= 0xffU;
        cppfm::storage_file_ops::failDirectorySyncAfterForTesting(1);
        bool externalReplacementFailed = false;
        try {
            externalWriter.store(5, 7, replacementChunk);
        } catch (const std::exception&) {
            externalReplacementFailed = true;
        }
        check(externalReplacementFailed &&
                  cppfm::RegionFile(externalWriteRegion.string()).load(5, 7) ==
                      largeChunk,
              "failed replacement of an existing external chunk restores the prior .mcc payload");
        check(!hasSidecarTemporaryArtifacts(directory, externalWriteSidecar),
              "failed external replacement cleans its staged and backup sidecars");
        externalWriter.store(5, 7, replacementChunk);
        check(cppfm::RegionFile(externalWriteRegion.string()).load(5, 7) ==
                  replacementChunk &&
                  !hasSidecarTemporaryArtifacts(directory, externalWriteSidecar),
              "successful external replacement commits the new payload and removes its backup");

        const auto directMarker = fs::path(
            externalWriteRegion.string() + ".cppfm-direct-mcc-txn");
        const auto interruptDirectStore = [&](
            cppfm::RegionFile::ExternalTransactionTestPoint point,
            const std::vector<std::uint8_t>& candidate,
            const std::vector<std::uint8_t>& expectedAfterRecovery,
            const char* pointLabel, const char* recoveryLabel) {
            cppfm::RegionFile::setExternalTransactionInterruptionForTesting(
                [point](cppfm::RegionFile::ExternalTransactionTestPoint reached) {
                    return reached == point;
                });
            bool interrupted = false;
            try {
                externalWriter.store(5, 7, candidate);
            } catch (const std::exception&) {
                interrupted = true;
            }
            cppfm::RegionFile::setExternalTransactionInterruptionForTesting({});
            check(interrupted && fs::exists(directMarker), pointLabel);
            check(cppfm::RegionFile(externalWriteRegion.string()).load(5, 7) ==
                      expectedAfterRecovery &&
                      !fs::exists(directMarker) &&
                      !hasSidecarTemporaryArtifacts(directory,
                                                    externalWriteSidecar),
                  recoveryLabel);
        };

        auto interruptedReplacement = replacementChunk;
        interruptedReplacement[1] ^= 0x7fU;
        interruptDirectStore(
            cppfm::RegionFile::ExternalTransactionTestPoint::SidecarPublished,
            interruptedReplacement, replacementChunk,
            "direct canonical write leaves a durable rollback marker after publishing the new .mcc",
            "load recovery restores the old .mcc and old MCA header after the sidecar-before-header crash window");
        interruptDirectStore(
            cppfm::RegionFile::ExternalTransactionTestPoint::RegionHeaderPublished,
            interruptedReplacement, replacementChunk,
            "direct canonical write can be interrupted after the MCA header is durable but before its commit decision",
            "prepared recovery rolls back both canonical files after an MCA-header-before-commit interruption");

        auto committedReplacement = replacementChunk;
        committedReplacement[2] ^= 0x3dU;
        interruptDirectStore(
            cppfm::RegionFile::ExternalTransactionTestPoint::CommitMarkerPublished,
            committedReplacement, committedReplacement,
            "direct canonical write can be interrupted after its durable commit decision",
            "committed recovery retains the verified new MCA/.mcc pair and removes transaction artifacts");
        replacementChunk = std::move(committedReplacement);

        const auto failedExternalRegion = directory / "r.10.11.mca";
        const auto failedExternalSidecar = directory / "c.320.352.mcc";
        const std::vector<std::uint8_t> priorChunk(96, 0x5a);
        cppfm::RegionFile failedExternalWriter(failedExternalRegion.string());
        failedExternalWriter.store(0, 0, priorChunk);
        cppfm::storage_file_ops::failNextDirectorySyncForTesting();
        bool externalPublishFailed = false;
        try {
            failedExternalWriter.store(0, 0, largeChunk);
        } catch (const std::exception&) {
            externalPublishFailed = true;
        }
        check(externalPublishFailed &&
                  cppfm::RegionFile(failedExternalRegion.string()).load(0, 0) ==
                      priorChunk,
              "failed external sidecar publication preserves the prior inline chunk");
        check(!fs::exists(failedExternalSidecar) &&
                  !hasSidecarTemporaryArtifacts(directory,
                                               failedExternalSidecar),
              "failed external sidecar publication removes the unreferenced sidecar and all owned temporary files");

#ifndef _WIN32
        const auto linkedRegion = directory / "r.3.0.mca";
        fs::create_symlink(shortHighOffset, linkedRegion);
        const auto targetBefore = [&] {
            std::ifstream file(shortHighOffset, std::ios::binary);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }();
        bool symlinkStoreRejected = false;
        try {
            cppfm::RegionFile(linkedRegion.string()).store(0, 0, rawPayload);
        } catch (const std::exception&) {
            symlinkStoreRejected = true;
        }
        bool symlinkLoadRejected = false;
        try {
            (void)cppfm::RegionFile(linkedRegion.string()).load(0, 0);
        } catch (const cppfm::RegionLoadError&) {
            symlinkLoadRejected = true;
        }
        const auto targetAfter = [&] {
            std::ifstream file(shortHighOffset, std::ios::binary);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }();
        check(symlinkStoreRejected && symlinkLoadRejected &&
                  fs::is_symlink(fs::symlink_status(linkedRegion)) &&
                  targetBefore == targetAfter,
              "region read/write handles reject symlinks and never follow them to the target");
#endif
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: unexpected region fixture error: "
                  << exception.what() << '\n';
        ++failures;
    }

    fs::remove_all(directory, error);
    if (error) {
        std::cerr << "FAIL: clean test directory: " << error.message() << '\n';
        ++failures;
    }
    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES") << ": "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
