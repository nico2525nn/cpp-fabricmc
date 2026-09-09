#include "core/RuntimeLayout.hpp"
#include <EmbeddedResources.hpp>
#include "platform/FileLock.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool pathExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool fileContains(const std::filesystem::path& path, const std::string& expected) {
    std::ifstream input(path, std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
    return input.good() || input.eof() ? value == expected : false;
}

} // namespace

int main() {
    if (!cppfm::embedded::kHasPack) {
        std::cout << "runtime layout: embedded pack unavailable; skipped\n";
        return 0;
    }

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("cppfm-runtime-layout-" + std::to_string(suffix));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "assets/registry", ec);
    if (ec) return 1;
    {
        std::ofstream sentinel(root / "assets/registry/tags.bin", std::ios::binary);
        sentinel << "user-owned";
    }

    // The runtime singleton is also the ownership boundary for the server
    // directory.  Concurrent first callers must converge on the same
    // prepared instance rather than racing on the directory and file lock.
    constexpr std::size_t callers = 8;
    std::array<bool, callers> concurrentResults{};
    std::array<std::string, callers> concurrentErrors;
    std::atomic<bool> start{false};
    std::vector<std::thread> concurrent;
    concurrent.reserve(callers);
    for (std::size_t index = 0; index < callers; ++index) {
        concurrent.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::filesystem::path concurrentRoot = root;
            concurrentResults[index] = cppfm::RuntimeLayout::prepare(
                concurrentRoot, &concurrentErrors[index]);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& caller : concurrent) caller.join();

    std::filesystem::path selected = root;
    std::string error;
    bool prepared = true;
    for (std::size_t index = 0; index < callers; ++index) {
        prepared = prepared && concurrentResults[index];
        if (!concurrentResults[index] && error.empty())
            error = concurrentErrors[index];
    }
    const std::vector<std::filesystem::path> required = {
        "world", "world/region", "mods", "config", "libraries", "logs",
        "crash-reports", "resourcepacks", ".cppfm/jvm/classes",
        "server.properties", "assets/registry/tags.bin"};
    bool pass = prepared;
    for (const auto& relative : required) pass = pass && pathExists(root / relative);
    pass = pass && fileContains(root / "assets/registry/tags.bin", "user-owned");

    selected = root;
    std::string secondError;
    pass = pass && cppfm::RuntimeLayout::prepare(selected, &secondError);
    pass = pass && fileContains(root / "assets/registry/tags.bin", "user-owned");

    std::string competingError;
    cppfm::platform::FileLock competingLock(
        root / ".cppfm" / "server.lock", competingError);
    pass = pass && !competingLock.locked();

    if (!pass) {
        std::cerr << "runtime layout test failed: "
                  << (error.empty() ? secondError : error) << '\n';
        std::filesystem::remove_all(root, ec);
        return 1;
    }
    std::filesystem::remove_all(root, ec);
    std::cout << "runtime layout: PASS\n";
    return 0;
}
