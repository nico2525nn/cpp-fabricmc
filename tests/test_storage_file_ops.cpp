#include "platform/StorageFileOps.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

int main() {
#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(::getpid());
#endif
    auto directory = fs::temp_directory_path() /
        ("cppfm-storage-read-" + std::to_string(processId));
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

    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    const auto regular = directory / "regular.dat";
    const std::array<std::uint8_t, 4> contents{1, 2, 3, 4};
    {
        std::ofstream output(regular, std::ios::binary);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
    }
    std::vector<std::uint8_t> bytes;
    auto status = cppfm::storage_file_ops::readRegularFileBounded(
        regular, 4, bytes, error);
    check(status == cppfm::storage_file_ops::BoundedReadStatus::Read &&
              bytes == std::vector<std::uint8_t>(contents.begin(), contents.end()) &&
              !error,
          "bounded regular-file read returns exact bytes");
    const auto copied = directory / "regular-copy.dat";
    const bool copyOk = cppfm::storage_file_ops::copyFileExclusively(
        regular, copied, error, nullptr, 0600);
    status = cppfm::storage_file_ops::readRegularFileBounded(
        copied, 4, bytes, error);
    check(copyOk &&
              status == cppfm::storage_file_ops::BoundedReadStatus::Read &&
              bytes == std::vector<std::uint8_t>(contents.begin(), contents.end()),
          "exclusive regular-file copy streams exact bytes");
    const auto boundedCopy = directory / "bounded-copy.dat";
    const bool boundedCopyOk =
        cppfm::storage_file_ops::copyFileExclusively(
            regular, boundedCopy, error, nullptr, 0600, 3);
    check(!boundedCopyOk && error == std::errc::file_too_large &&
              !fs::exists(boundedCopy) && fs::file_size(regular) == contents.size(),
          "recovery-copy bound rejects dense oversized input without modifying the source");

#ifndef _WIN32
    const auto moveSource = directory / "quarantine-source.dat";
    const auto moveDestination = directory / "quarantine-copy.dat";
    {
        std::ofstream output(moveSource, std::ios::binary);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
    }
    cppfm::storage_file_ops::failNextDirectorySyncForTesting();
    const auto preUnlinkFailure =
        cppfm::storage_file_ops::moveFileWithoutReplace(
            moveSource, moveDestination, error);
    check(preUnlinkFailure == cppfm::storage_file_ops::
                                   MoveWithoutReplaceStatus::
                                       DestinationCreatedSourceRetained &&
              error == std::errc::io_error && fs::exists(moveSource) &&
              fs::exists(moveDestination),
          "quarantine retains the active source when the new directory entry cannot be synced");
    fs::remove(moveDestination, error);
    error.clear();
    cppfm::storage_file_ops::failDirectorySyncAfterForTesting(1);
    const auto postUnlinkFailure =
        cppfm::storage_file_ops::moveFileWithoutReplace(
            moveSource, moveDestination, error);
    check(postUnlinkFailure == cppfm::storage_file_ops::
                                    MoveWithoutReplaceStatus::
                                        MovedWithUncertainSourceRemoval &&
              error == std::errc::io_error && !fs::exists(moveSource) &&
              fs::exists(moveDestination),
          "quarantine reports uncertain unlink durability while retaining the already-synced evidence copy");
#endif
    status = cppfm::storage_file_ops::readRegularFileBounded(
        regular, 3, bytes, error);
    check(status == cppfm::storage_file_ops::BoundedReadStatus::TooLarge &&
              bytes.empty(),
          "oversized regular file is rejected before allocation");
    status = cppfm::storage_file_ops::readRegularFileBounded(
        directory / "missing.dat", 16, bytes, error);
    check(status == cppfm::storage_file_ops::BoundedReadStatus::Missing &&
              bytes.empty(),
          "missing path is distinguished from I/O failure");

#ifndef _WIN32
    const auto fifo = directory / "level.dat.fifo";
    if (::mkfifo(fifo.c_str(), 0600) != 0) {
        std::cerr << "FAIL: create FIFO fixture\n";
        ++failures;
    } else {
        status = cppfm::storage_file_ops::readRegularFileBounded(
            fifo, 16, bytes, error);
        check(status == cppfm::storage_file_ops::BoundedReadStatus::NonRegular &&
                  bytes.empty(),
              "FIFO is rejected without waiting for a writer");
        const auto fifoCopy = directory / "fifo-copy.dat";
        const bool fifoCopied =
            cppfm::storage_file_ops::copyFileExclusively(
                fifo, fifoCopy, error);
        check(!fifoCopied && !fs::exists(fifoCopy),
              "FIFO backup source is rejected without blocking or output");
    }

    const auto link = directory / "regular-link.dat";
    fs::create_symlink(regular.filename(), link, error);
    if (error) {
        std::cerr << "FAIL: create symlink fixture: " << error.message()
                  << '\n';
        ++failures;
    } else {
        status = cppfm::storage_file_ops::readRegularFileBounded(
            link, 16, bytes, error);
        check(status != cppfm::storage_file_ops::BoundedReadStatus::Read &&
                  bytes.empty(),
              "bounded regular-file reader refuses symlink sources");
        const auto linkCopy = directory / "symlink-copy.dat";
        const bool linkCopied =
            cppfm::storage_file_ops::copyFileExclusively(
                link, linkCopy, error);
        check(!linkCopied && !fs::exists(linkCopy),
              "backup copy refuses to follow a symlink source");
    }

    const auto sparse = directory / "large-sparse-region.mca";
    const int sparseFd = ::open(sparse.c_str(), O_WRONLY | O_CREAT | O_EXCL,
                                0600);
    if (sparseFd < 0) {
        std::cerr << "FAIL: create sparse-copy fixture\n";
        ++failures;
    } else {
        const auto sparseBytes = static_cast<off_t>(
            cppfm::storage_file_ops::kMaxSparseBackupLogicalBytes + 1);
        const bool resized = ::ftruncate(sparseFd, sparseBytes) == 0;
        const int closeResult = ::close(sparseFd);
        struct stat sparseStatus {};
        const bool inspected = resized && closeResult == 0 &&
                               ::stat(sparse.c_str(), &sparseStatus) == 0;
        if (!inspected) {
            std::cerr << "FAIL: size sparse-copy fixture\n";
            ++failures;
        } else if (static_cast<std::uint64_t>(sparseStatus.st_blocks) * 512 <
                   static_cast<std::uint64_t>(sparseStatus.st_size) / 2) {
            const auto sparseCopy = directory / "sparse-copy.mca";
            const bool copied =
                cppfm::storage_file_ops::copyFileExclusively(
                    sparse, sparseCopy, error);
            check(!copied && error == std::errc::file_too_large &&
                      !fs::exists(sparseCopy),
                  "backup copy refuses a huge sparse source before materializing holes");
        } else {
            std::cout << "SKIP: filesystem did not create a sparse fixture\n";
        }
    }
#endif

    fs::remove_all(directory, error);
    if (error) {
        std::cerr << "FAIL: clean test directory: " << error.message() << '\n';
        ++failures;
    }
    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES") << ": "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
