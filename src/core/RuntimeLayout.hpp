// Runtime directory and embedded-resource bootstrap.
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace cppfm::platform {
class FileLock;
}

namespace cppfm {

class RuntimeLayout {
public:
    // When root is empty, selects the current directory unless
    // CPPFM_SERVER_DIR is set.  A non-empty root is useful for tests and
    // embedding callers and is never allowed to change the process cwd.
    // Creates the standard server layout and extracts embedded files.  The
    // environment-selected root is made the process working directory so the
    // existing relative data paths behave like a normal server.jar launch.
    static bool prepare(std::filesystem::path& root, std::string* error = nullptr);

    static std::filesystem::path embeddedClasses(
        const std::filesystem::path& root) {
        return root / ".cppfm" / "jvm" / "classes";
    }

private:
    RuntimeLayout() = default;
    ~RuntimeLayout();

    RuntimeLayout(const RuntimeLayout&) = delete;
    RuntimeLayout& operator=(const RuntimeLayout&) = delete;

    static RuntimeLayout& processInstance();
    bool prepareForProcess(std::filesystem::path& root, std::string* error);

    std::mutex prepareMutex_;
    std::filesystem::path root_;
    std::unique_ptr<platform::FileLock> serverLock_;
    bool prepared_ = false;
};

} // namespace cppfm
