#include "core/RuntimeLayout.hpp"
#include <EmbeddedResources.hpp>
#include "platform/FileLock.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
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

bool readFile(const std::filesystem::path& path, std::string& value) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    value.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return !input.bad();
}

std::map<std::string, std::string> activeProperties(std::string_view contents) {
    std::map<std::string, std::string> properties;
    std::size_t lineStart = 0;
    while (lineStart < contents.size()) {
        const auto lineEnd = contents.find('\n', lineStart);
        auto line = contents.substr(lineStart,
            lineEnd == std::string_view::npos ? contents.size() - lineStart
                                               : lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#' && line.front() != '!') {
            const auto equals = line.find('=');
            if (equals != std::string_view::npos)
                properties[std::string(line.substr(0, equals))] =
                    std::string(line.substr(equals + 1));
        }
        if (lineEnd == std::string_view::npos) break;
        lineStart = lineEnd + 1;
    }
    return properties;
}

bool hasExpectedFirstRunProperties(const std::filesystem::path& path) {
    std::string contents;
    if (!readFile(path, contents)) return false;
    const auto properties = activeProperties(contents);
    const std::map<std::string, std::string> expected = {
        {"server-port", "25565"}, {"max-players", "20"},
        {"view-distance", "10"}, {"simulation-distance", "10"},
        {"level-type", "minecraft:normal"}, {"difficulty", "easy"},
        {"motd", "A Minecraft Server"}, {"level-seed", ""},
        {"spawn-protection", "16"}, {"online-mode", "true"},
        {"white-list", "false"},
        {"pvp", "true"}, {"allow-flight", "false"},
        {"hardcore", "false"}, {"enforce-secure-profile", "true"},
        {"network-compression-threshold", "256"}, {"enable-rcon", "false"},
        {"rcon.port", "25575"}, {"rcon.password", ""},
        {"resource-pack", ""}, {"resource-pack-sha1", ""},
        {"require-resource-pack", "false"}, {"jvm", "true"},
        {"jvm-strict", "false"},
    };
    // Exact equality also rejects unsupported/unknown active vanilla keys.
    return properties == expected;
}

} // namespace

int main() {
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
    pass = pass && hasExpectedFirstRunProperties(root / "server.properties");
    if (cppfm::embedded::kHasPack) {
        std::string exampleContents;
        std::string copiedContents;
        pass = pass && pathExists(root / "server.properties.example") &&
               readFile(root / "server.properties.example", exampleContents) &&
               readFile(root / "server.properties", copiedContents) &&
               copiedContents == exampleContents &&
               hasExpectedFirstRunProperties(root / "server.properties.example");
    } else {
        // No-pack builds cannot extract the example and must exercise the
        // compiled-in first-run fallback instead of skipping this contract.
        pass = pass && !pathExists(root / "server.properties.example");
    }

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
                  << (error.empty() ? secondError : error)
                  << " (including first-run properties template/default checks)\n";
        std::filesystem::remove_all(root, ec);
        return 1;
    }
    std::filesystem::remove_all(root, ec);
    std::cout << "runtime layout: PASS"
              << (cppfm::embedded::kHasPack ? " (embedded template)\n"
                                            : " (no-pack fallback)\n");
    return 0;
}
