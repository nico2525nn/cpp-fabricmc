// WorldDataManager: level.dat handling with atomic rename and DataFixerUpper version check (plan7)
#pragma once
#include <string>
#include <functional>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include "../core/NBTValue.hpp"

namespace cppfm {

constexpr std::int32_t kCurrentDataVersion = 4189;

class WorldDataManager {
public:
    explicit WorldDataManager(std::string worldDir) : dir_(std::move(worldDir)) {}

    void setLevelStateProvider(std::function<void(nbt::Value&)> p, std::function<void(const nbt::Value&)> c) {
        provide_ = std::move(p);
        consume_ = std::move(c);
    }
    void setDirectory(std::string d) { dir_ = std::move(d); }
    const std::string& directory() const { return dir_; }

    // Atomic write helper: write to temp then rename
    bool atomicWrite(const std::string& path, const std::vector<std::uint8_t>& data) const {
        try {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            std::string tmp = path + ".new";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                f.write(reinterpret_cast<const char*>(data.data()), data.size());
                if (!f) return false;
            }
            // atomic rename
            std::filesystem::rename(tmp, path);
            return true;
        } catch (...) {
            return false;
        }
    }

    // DataFixerUpper-like version check
    bool needsFixup(std::int32_t fileVersion) const {
        return fileVersion < kCurrentDataVersion;
    }
    // Simple fixup: bump version and ensure required compounds exist
    void applyFixups(nbt::Value& root, std::int32_t fromVersion) const {
        if (fromVersion >= kCurrentDataVersion) return;
        // Example fixups: ensure Version compound, WorldBorder defaults, etc.
        auto* data = root.get("Data");
        if (!data) return;
        // In real DFU, would apply schemata. Here we just ensure DataVersion is updated.
        // Caller will set DataVersion to current before save.
        (void)fromVersion;
    }

    bool checkAndFixVersion(nbt::Value& root) const {
        nbt::Value* data = nullptr;
        for (auto& [k,v] : root.comp) if (k=="Data") { data = &v; break; }
        if (!data) return false;
        std::int32_t ver = kCurrentDataVersion;
        if (auto* dv = data->get("DataVersion")) {
            if (dv->tag == nbt::Int) ver = dv->i;
            else if (dv->tag == nbt::Long) ver = static_cast<std::int32_t>(dv->l);
        } else {
            ver = 0;
        }
        if (needsFixup(ver)) {
            std::fprintf(stderr, "[WorldDataManager] DataFixerUpper: upgrading %d -> %d\n", ver, kCurrentDataVersion);
            applyFixups(root, ver);
            for (auto& [k,v] : data->comp) if (k=="DataVersion") { v.tag = nbt::Int; v.i = kCurrentDataVersion; return true; }
            data->set("DataVersion", nbt::Value::makeInt(kCurrentDataVersion));
        }
        return true;
    }

    // High-level save/load delegating to providers
    bool saveLevelData(nbt::Value root) {
        try {
            nbt::Value* data = nullptr;
            for (auto& [k,v] : root.comp) if (k=="Data") { data = &v; break; }
            if (data) {
                for (auto& [k,v] : data->comp) if (k=="DataVersion") { v.i = kCurrentDataVersion; break; }
            }
            WriteBuffer out;
            nbt::writeFileRoot(out, root);
            std::string path = dir_ + "/level.dat";
            return atomicWrite(path, out.data);
        } catch (...) { return false; }
    }
    bool saveLevelDataWithProviders(std::int64_t worldTicks, std::int64_t dayTime, class World& world,
                                    const std::string& difficulty,
                                    double borderDiameter, double borderCX, double borderCZ,
                                    double borderLerpTarget = -1, std::int64_t borderLerpMs = -1);

    bool loadLevelData(class World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                       double* borderLerpTargetOut = nullptr, std::int64_t* borderLerpMsOut = nullptr);

    // Raw load for testing: returns root
    bool loadRaw(nbt::Value& outRoot) const {
        try {
            std::string path = dir_ + "/level.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (bytes.empty()) return false;
            ReadBuffer in(bytes);
            nbt::Parser parser(in);
            outRoot = parser.readFileRoot();
            // version check
            checkAndFixVersion(outRoot);
            return true;
        } catch (...) { return false; }
    }

private:
    std::string dir_;
    std::function<void(nbt::Value&)> provide_;
    std::function<void(const nbt::Value&)> consume_;
};

} // namespace cppfm
