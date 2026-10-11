// Header-only and loader-injected so recovery policy can be tested without a
// GameServer instance. Unsafe failures are surfaced so a caller cannot save a
// fresh player over a source it could not successfully load or isolate.
#pragma once
#include "WorldDataManager.hpp"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

namespace cppfm {

enum class PlayerDataQuarantineMoveResult {
    Moved,
    DestinationExists,
    Failed
};

// Wraps loader(path) -> bool. Returns loader's result on success. When the file
// exists but loading fails, quarantines it to path+".corrupt" unless the loader
// marks the source as valid but unsupported. Unsupported data must remain at
// the active path so a later save cannot overwrite it with fresh state.
// The sync callbacks make the ordering and fail-safe behavior testable without
// weakening the production path, which always delegates to the persistence
// durability helpers below.
template <typename Loader, typename SyncFile, typename SyncDirectory,
          typename DurableMove>
bool loadPlayerDataIsolatedWithOperations(
    const std::string& path, Loader&& loader, SyncFile&& syncFile,
    SyncDirectory&& syncDirectory, DurableMove&& durableMove,
    bool useDurableMove, std::string* quarantinedOut = nullptr,
    bool* preserveOriginalOnFailure = nullptr,
    bool* unsafeExistingDataOut = nullptr) {
    if (preserveOriginalOnFailure) *preserveOriginalOnFailure = false;
    if (unsafeExistingDataOut) *unsafeExistingDataOut = false;
    const auto markUnsafe = [&] {
        if (unsafeExistingDataOut) *unsafeExistingDataOut = true;
    };
    std::error_code existenceError;
    bool exists = false;
    try {
        exists = std::filesystem::exists(path, existenceError);
    } catch (const std::exception& error) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s existence check failed: %s\n",
                     path.c_str(), error.what());
        return false;
    } catch (...) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s existence check failed\n",
                     path.c_str());
        return false;
    }
    if (existenceError) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s existence check failed: %s\n",
                     path.c_str(), existenceError.message().c_str());
        return false;
    }
    if (!exists) return false;
    bool ok = false;
    try {
        ok = loader(path);
    } catch (...) { ok = false; }
    if (ok) return true;
    if (preserveOriginalOnFailure && *preserveOriginalOnFailure) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s could not be read safely or contains unsupported content; original preserved\n",
                     path.c_str());
        return false;
    }
    const auto safelySyncFile = [&](const std::filesystem::path& syncPath) {
        try {
            return syncFile(syncPath);
        } catch (...) {
            return false;
        }
    };
    const auto safelySyncDirectory = [&](const std::filesystem::path& syncPath) {
        try {
            return syncDirectory(syncPath);
        } catch (...) {
            return false;
        }
    };

    if (useDurableMove) {
        try {
            // Windows cannot use persistence_detail::syncDirectory here: that
            // helper is a no-op whose contract depends on replaceFile's
            // MOVEFILE_WRITE_THROUGH. Flush the source contents, then move the
            // source itself to a collision-free sibling using a write-through,
            // no-replace operation. That single atomic move durably installs
            // the quarantine name and removes the active name.
            if (!safelySyncFile(path)) {
                markUnsafe();
                std::fprintf(stderr,
                             "[cppfm] playerdata %s unreadable; source could not be synced before durable quarantine move\n",
                             path.c_str());
                return false;
            }

            const std::string quarantineBase = path + ".corrupt";
            for (unsigned int attempt = 0; attempt < 10000; ++attempt) {
                const std::string candidate = attempt == 0
                    ? quarantineBase
                    : quarantineBase + "." + std::to_string(attempt);
                const auto result = durableMove(path, candidate);
                if (result == PlayerDataQuarantineMoveResult::DestinationExists)
                    continue;
                if (result != PlayerDataQuarantineMoveResult::Moved) {
                    markUnsafe();
                    std::fprintf(stderr,
                                 "[cppfm] playerdata %s unreadable; durable quarantine move failed, active source retained\n",
                                 path.c_str());
                    return false;
                }
                if (quarantinedOut) *quarantinedOut = candidate;
                std::fprintf(stderr,
                             "[cppfm] playerdata %s unreadable, durably moved to %s (fresh spawn)\n",
                             path.c_str(), candidate.c_str());
                return false;
            }
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable, no free quarantine path\n",
                         path.c_str());
        } catch (const std::exception& error) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s durable quarantine move failed: %s\n",
                         path.c_str(), error.what());
        } catch (...) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s durable quarantine move failed\n",
                         path.c_str());
        }
        return false;
    }

    try {
        const std::filesystem::path quarantineBase = path + ".corrupt";
        std::filesystem::path quarantinePath;
        std::error_code copyError;
        if (!storage_file_ops::copyFileToNewSibling(
                path, quarantineBase, quarantinePath, copyError,
                nullptr, 0666)) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable, quarantine copy failed (%s)\n",
                         path.c_str(), copyError ? copyError.message().c_str()
                                                 : "no destination available");
            return false;
        }
        if (quarantinedOut) *quarantinedOut = quarantinePath.string();

        const auto quarantineDirectory =
            quarantinePath.parent_path();
        if (!safelySyncFile(quarantinePath)) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable; quarantine copy at %s could not be synced, active source retained\n",
                         path.c_str(), quarantinePath.string().c_str());
            return false;
        }
        if (!safelySyncDirectory(quarantineDirectory)) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable; quarantine entry at %s could not be made durable, active source retained\n",
                         path.c_str(), quarantinePath.string().c_str());
            return false;
        }

        std::error_code removeError;
        const bool removed = std::filesystem::remove(path, removeError);
        if (removeError || !removed) {
            markUnsafe();
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable; quarantine copy at %s but active source could not be removed (%s)\n",
                         path.c_str(), quarantinePath.string().c_str(),
                         removeError ? removeError.message().c_str()
                                     : "source was not removed");
            return false;
        }
        if (!safelySyncDirectory(quarantineDirectory)) {
            markUnsafe();

            // The quarantine copy and its directory entry were synced before
            // unlinking the source, so data is retained there. Still restore
            // the active source best-effort: a failed post-unlink directory
            // sync leaves the unlink's crash outcome uncertain, and callers
            // must not proceed as if the player were safely fresh.
            bool sourceRestored = false;
            try {
                std::error_code restoreError;
                const bool copiedBack =
                    storage_file_ops::copyFileExclusively(
                        quarantinePath, path, restoreError, nullptr, 0666);
                if (copiedBack && !restoreError && safelySyncFile(path) &&
                    safelySyncDirectory(quarantineDirectory)) {
                    sourceRestored = true;
                }
            } catch (...) {
                // The durable quarantine remains available even if restoring
                // the active name fails for an exceptional filesystem error.
            }
            std::fprintf(stderr,
                         "[cppfm] playerdata %s quarantine copy at %s is durable but source unlink could not be synced; source restoration %s\n",
                         path.c_str(), quarantinePath.string().c_str(),
                         sourceRestored ? "succeeded" : "failed");
            return false;
        }
        std::fprintf(stderr,
                     "[cppfm] playerdata %s unreadable, quarantined to %s (fresh spawn)\n",
                     path.c_str(), quarantinePath.string().c_str());
    } catch (const std::exception& error) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s quarantine failed: %s\n",
                     path.c_str(), error.what());
    } catch (...) {
        markUnsafe();
        std::fprintf(stderr,
                     "[cppfm] playerdata %s quarantine failed\n",
                     path.c_str());
    }
    return false;
}

template <typename Loader, typename SyncFile, typename SyncDirectory>
bool loadPlayerDataIsolatedWithSync(
    const std::string& path, Loader&& loader, SyncFile&& syncFile,
    SyncDirectory&& syncDirectory, std::string* quarantinedOut = nullptr,
    bool* preserveOriginalOnFailure = nullptr,
    bool* unsafeExistingDataOut = nullptr) {
    return loadPlayerDataIsolatedWithOperations(
        path, std::forward<Loader>(loader), std::forward<SyncFile>(syncFile),
        std::forward<SyncDirectory>(syncDirectory),
        [](const std::filesystem::path&, const std::filesystem::path&) {
            return PlayerDataQuarantineMoveResult::Failed;
        },
        false, quarantinedOut, preserveOriginalOnFailure,
        unsafeExistingDataOut);
}

template <typename Loader, typename SyncFile, typename SyncDirectory,
          typename DurableMove>
bool loadPlayerDataIsolatedWithDurableMove(
    const std::string& path, Loader&& loader, SyncFile&& syncFile,
    SyncDirectory&& syncDirectory, DurableMove&& durableMove,
    std::string* quarantinedOut = nullptr,
    bool* preserveOriginalOnFailure = nullptr,
    bool* unsafeExistingDataOut = nullptr) {
    return loadPlayerDataIsolatedWithOperations(
        path, std::forward<Loader>(loader), std::forward<SyncFile>(syncFile),
        std::forward<SyncDirectory>(syncDirectory),
        std::forward<DurableMove>(durableMove), true, quarantinedOut,
        preserveOriginalOnFailure, unsafeExistingDataOut);
}

inline PlayerDataQuarantineMoveResult movePlayerDataQuarantineWriteThrough(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    std::error_code existsError;
    if (std::filesystem::exists(destination, existsError))
        return PlayerDataQuarantineMoveResult::DestinationExists;
    if (existsError) return PlayerDataQuarantineMoveResult::Failed;

    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                    MOVEFILE_WRITE_THROUGH))
        return PlayerDataQuarantineMoveResult::Moved;
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
        return PlayerDataQuarantineMoveResult::DestinationExists;
    if (error == ERROR_ACCESS_DENIED) {
        existsError.clear();
        if (std::filesystem::exists(destination, existsError))
            return PlayerDataQuarantineMoveResult::DestinationExists;
    }
    return PlayerDataQuarantineMoveResult::Failed;
#else
    (void)source;
    (void)destination;
    return PlayerDataQuarantineMoveResult::Failed;
#endif
}

template <typename Loader>
bool loadPlayerDataIsolated(const std::string& path, Loader&& loader,
                            std::string* quarantinedOut = nullptr,
                            bool* preserveOriginalOnFailure = nullptr,
                            bool* unsafeExistingDataOut = nullptr) {
#ifdef _WIN32
    return loadPlayerDataIsolatedWithDurableMove(
        path, std::forward<Loader>(loader),
        [](const std::filesystem::path& syncPath) {
            return persistence_detail::syncFile(syncPath);
        },
        [](const std::filesystem::path& syncPath) {
            return persistence_detail::syncDirectory(syncPath);
        },
        movePlayerDataQuarantineWriteThrough, quarantinedOut,
        preserveOriginalOnFailure, unsafeExistingDataOut);
#else
    return loadPlayerDataIsolatedWithSync(
        path, std::forward<Loader>(loader),
        [](const std::filesystem::path& syncPath) {
            return persistence_detail::syncFile(syncPath);
        },
        [](const std::filesystem::path& syncPath) {
            return persistence_detail::syncDirectory(syncPath);
        },
        quarantinedOut, preserveOriginalOnFailure, unsafeExistingDataOut);
#endif
}

} // namespace cppfm
