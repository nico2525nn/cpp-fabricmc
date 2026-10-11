#include "game/Persistence.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("cppfm-region-transaction-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

bool writeByte(const std::filesystem::path& path, char value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.put(value);
    output.close();
    return static_cast<bool>(output);
}

void testPathLockSurvivesAtomicReplacement() {
    using namespace cppfm;
    TemporaryDirectory directory;
    const auto primary = directory.path() / "r.0.0.mca";
    const auto staged = directory.path() / "r.0.0.mca.new";
    check(writeByte(primary, 'a') && writeByte(staged, 'b'),
          "region replacement fixtures are created");
    const auto keysBefore = RegionFile::transactionLockKeys(primary.string());
    const auto stagedIdentity = RegionFile::transactionKey(staged.string());

    std::error_code replaceError;
    check(persistence_detail::replaceFile(staged, primary, replaceError),
          "fixture replacement succeeds");
    const auto keysAfter = RegionFile::transactionLockKeys(primary.string());
    check(keysBefore[0] == keysAfter[0],
          "canonical path lock key is stable across atomic replacement");
    check(keysBefore[1] != keysAfter[1] && keysAfter[1] == stagedIdentity,
          "test replacement changes the native file identity");
}

void testNestedDirectoryCreation() {
    using namespace cppfm;
    TemporaryDirectory directory;
    const auto nested = directory.path() / "world" / "DIM-1" / "region";
    std::error_code createError;
    const bool created = storage_file_ops::createDirectoriesDurably(
        nested, createError);
    check(created && !createError && std::filesystem::is_directory(nested),
          "nested persistence directory hierarchy is created");
}

void testTransactionRemainsLockedAcrossAtomicReplacement() {
    using namespace cppfm;
    TemporaryDirectory directory;
    const auto primary = directory.path() / "r.1.0.mca";
    const auto staged = directory.path() / "r.1.0.mca.new";
    check(writeByte(primary, 'a') && writeByte(staged, 'b'),
          "concurrency fixtures are created");

    bool lockRemainsUnavailableAcrossReplacement = false;
    {
        auto transaction = Persistence::lockRegionFileTransaction(
            primary.string());
        const auto beforeReplacement =
            Persistence::tryLockRegionFileTransaction(primary.string());
        const bool blockedBeforeReplacement = !beforeReplacement;

        std::error_code replaceError;
        check(persistence_detail::replaceFile(staged, primary, replaceError),
              "primary is atomically replaced while its transaction is held");
        const auto afterReplacement =
            Persistence::tryLockRegionFileTransaction(primary.string());
        lockRemainsUnavailableAcrossReplacement =
            blockedBeforeReplacement && !afterReplacement;
    }
    const auto releasedTransaction =
        Persistence::tryLockRegionFileTransaction(primary.string());

    check(lockRemainsUnavailableAcrossReplacement,
          "nonblocking acquisition proves replacement cannot bypass the active path transaction");
    check(static_cast<bool>(releasedTransaction),
          "region transaction can be acquired after the earlier save releases");
}

void testHardLinkAliasesShareTransactionIdentity() {
    using namespace cppfm;
    TemporaryDirectory directory;
    const auto primary = directory.path() / "r.2.0.mca";
    const auto alias = directory.path() / "alias.mca";
    check(writeByte(primary, 'a'), "hard-link primary fixture is created");
    std::error_code linkError;
    std::filesystem::create_hard_link(primary, alias, linkError);
    if (linkError) {
        std::printf("SKIP: hard-link transaction test unavailable: %s\n",
                    linkError.message().c_str());
        return;
    }

    bool blockedAcrossAliases = false;
    {
        auto transaction = Persistence::lockRegionFileTransaction(
            primary.string());
        blockedAcrossAliases =
            !Persistence::tryLockRegionFileTransaction(alias.string());
    }
    auto aliasTransaction =
        Persistence::tryLockRegionFileTransaction(alias.string());

    check(blockedAcrossAliases && aliasTransaction.has_value(),
          "nonblocking acquisition proves hard-link aliases serialize full persistence transactions");
}

} // namespace

int main() {
    testNestedDirectoryCreation();
    testPathLockSurvivesAtomicReplacement();
    testTransactionRemainsLockedAcrossAtomicReplacement();
    testHardLinkAliasesShareTransactionIdentity();
    std::printf("region_transaction_lock: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
