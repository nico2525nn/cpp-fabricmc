#include "GameServer.hpp"
#include "PlayerDataRecovery.hpp"
#include "BlockEntityNbtProjection.hpp"
#include "../core/Json.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../core/NBTValue.hpp"
#include "RegionFile.hpp"
#include "platform/StorageFileOps.hpp"
#include "ChunkCodec.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <system_error>
#include <string_view>
#include <vector>

namespace cppfm {
using namespace proto;
namespace {
constexpr std::size_t kMaxPlayerDataBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxAdminFileBytes = 8u * 1024u * 1024u;
std::mutex adminFileMutex;

enum class AdminJsonReadStatus { Loaded, Missing, Failed };

AdminJsonReadStatus readAdminJsonFile(const std::string& path,
                                      json::Value& value,
                                      const char* label) {
    std::vector<std::uint8_t> bytes;
    std::error_code readError;
    const auto readStatus = storage_file_ops::readRegularFileBounded(
        path, kMaxAdminFileBytes, bytes, readError);
    if (readStatus == storage_file_ops::BoundedReadStatus::Missing)
        return AdminJsonReadStatus::Missing;
    if (readStatus != storage_file_ops::BoundedReadStatus::Read) {
        std::fprintf(stderr,
                     "[cppfm] could not safely read %s (%s): %s\n",
                     path.c_str(), label, readError.message().c_str());
        return AdminJsonReadStatus::Failed;
    }
    const std::string text(bytes.begin(), bytes.end());
    try {
        value = json::Value::parse(text);
        return AdminJsonReadStatus::Loaded;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] %s is malformed: %s\n", path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] %s is malformed\n", path.c_str());
    }
    return AdminJsonReadStatus::Failed;
}

bool writeAdminJsonFile(const std::string& path, const json::Value& value,
                        const char* label) {
    std::string text;
    try {
        text = value.dump();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] could not serialize %s: %s\n", label, e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "[cppfm] could not serialize %s\n", label);
        return false;
    }
    if (text.size() > kMaxAdminFileBytes) {
        std::fprintf(stderr, "[cppfm] refusing oversized %s\n", label);
        return false;
    }

    std::lock_guard lock(adminFileMutex);
    const std::filesystem::path temporaryBase = path + ".new";
    std::filesystem::path temporary;
    bool temporaryOwned = false;
    try {
        const std::filesystem::path destination(path);
        const auto parent = destination.parent_path();
        if (!parent.empty()) {
            std::error_code directoryError;
            if (!storage_file_ops::createDirectoriesDurably(
                    parent, directoryError))
                throw std::filesystem::filesystem_error(
                    "create durable admin-data directory", parent,
                    directoryError);
        }
        text.push_back('\n');
        std::error_code writeError;
        if (!storage_file_ops::writeFileToNewSibling(
                temporaryBase, text.data(), text.size(), temporary,
                writeError, &temporaryOwned, 0666))
            throw std::system_error(writeError,
                                    "create temporary admin-data file");
        std::error_code ec;
        if (!storage_file_ops::replaceFile(temporary, destination, ec))
            throw std::system_error(ec, "replace admin-data file");
        temporaryOwned = false;
        if (!storage_file_ops::syncDirectory(parent))
            throw std::runtime_error("could not sync admin-data directory");
        return true;
    } catch (const std::exception& e) {
        std::error_code cleanupError;
        if (temporaryOwned)
            std::filesystem::remove(temporary, cleanupError);
        std::fprintf(stderr, "[cppfm] could not save %s: %s\n", path.c_str(), e.what());
    } catch (...) {
        std::error_code cleanupError;
        if (temporaryOwned)
            std::filesystem::remove(temporary, cleanupError);
        std::fprintf(stderr, "[cppfm] could not save %s\n", path.c_str());
    }
    return false;
}

template <typename Set>
void loadNameSet(const json::Value& root, Set& names, const char* field,
                 const char* label) {
    auto add = [&](const json::Value& entry) {
        if (entry.isStr()) {
            if (!entry.str.empty()) names.insert(entry.str);
            return;
        }
        if (!entry.isObj()) return;
        const auto* value = entry.find(field);
        if (value && value->isStr() && !value->str.empty())
            names.insert(value->str);
    };
    if (root.isArr()) {
        for (const auto& entry : root.arr) add(entry);
    } else if (root.isObj()) {
        // Keep accepting the old map-shaped files, but never interpret a
        // scalar/object field as a name by accident.
        for (const auto& [key, _] : root.obj)
            if (!key.empty()) names.insert(key);
    } else {
        std::fprintf(stderr, "[cppfm] ignoring non-array %s\n", label);
    }
}

void writeItemList(nbt::Value& root, const char* key, const ItemStack* slots,
                   int slotCount) {
    using namespace nbt;
    Value items = Value::makeList(Compound);
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
        const auto& slot = slots[slotIndex];
        if (slot.empty()) continue;
        items.list.push_back(slot.toNbt(slotIndex));
    }
    root.set(key, std::move(items));
}

int playerNbtInventorySlot(int internalSlot) {
    // Player::inv stores armor in reverse equip order at [5, 8] and keeps the
    // offhand at 45. Vanilla's serialized aliases are feet..head (100..103)
    // and -106 respectively; ordinary in-range inventory slots retain the
    // existing cppfm layout for save compatibility.
    if (internalSlot >= 5 && internalSlot <= 8) return internalSlot + 95;
    if (internalSlot == 45) return -106;
    return internalSlot;
}

void writePlayerInventory(nbt::Value& root, const ItemStack* slots,
                          int slotCount) {
    using namespace nbt;
    Value items = Value::makeList(Compound);
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
        const auto& slot = slots[slotIndex];
        if (slot.empty()) continue;
        items.list.push_back(slot.toNbt(playerNbtInventorySlot(slotIndex)));
    }
    root.set("Inventory", std::move(items));
}

bool loadPlayerRootForUpdate(const std::string& path, nbt::Value& root) {
    std::vector<std::uint8_t> bytes;
    std::error_code readError;
    const auto readStatus = storage_file_ops::readRegularFileBounded(
        path, kMaxPlayerDataBytes, bytes, readError);
    if (readStatus == storage_file_ops::BoundedReadStatus::Missing) {
        root = nbt::Value::makeCompound();
        return true;
    }
    if (readStatus != storage_file_ops::BoundedReadStatus::Read) {
        std::fprintf(stderr,
                     "[cppfm] refusing to overwrite unreadable, unsafe, or oversized player data %s: %s\n",
                     path.c_str(), readError.message().c_str());
        return false;
    }
    if (bytes.empty()) {
        std::fprintf(stderr,
                     "[cppfm] refusing to overwrite incomplete player data %s\n",
                     path.c_str());
        return false;
    }
    try {
        ReadBuffer input(bytes);
        nbt::Parser parser(input);
        auto existingRoot = parser.readFileRoot();
        if (existingRoot.tag != nbt::Compound) {
            std::fprintf(stderr,
                         "[cppfm] refusing to overwrite non-compound player data %s\n",
                         path.c_str());
            return false;
        }
        root = std::move(existingRoot);
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr,
                     "[cppfm] refusing to overwrite unparsable player data %s: %s\n",
                     path.c_str(), error.what());
    } catch (...) {
        std::fprintf(stderr,
                     "[cppfm] refusing to overwrite unparsable player data %s\n",
                     path.c_str());
    }
    return false;
}

void erasePlayerRootKey(nbt::Value& root, std::string_view key) {
    if (root.tag != nbt::Compound) return;
    auto& fields = root.comp;
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [key](const auto& field) {
                                    return field.first == key;
                                }),
                 fields.end());
}
}

static bool savePlayerNBT(const std::string& path, const Player& p) {
    std::lock_guard playerLock(p.stateMtx);
    using namespace nbt;
    Value root;
    if (!loadPlayerRootForUpdate(path, root)) return false;
    root.set("Health", Value::makeFloat(p.health));
    root.set("foodLevel", Value::makeInt(p.food));
    root.set("foodSaturation", Value::makeFloat(p.saturation));
    root.set("XpLevel", Value::makeInt(p.xp.level));
    root.set("XpTotal", Value::makeInt(p.xp.totalXp));
    root.set("XpP", Value::makeFloat(p.xp.progress));
    root.set("playerGameType", Value::makeInt(p.gamemode));
    root.set("previousPlayerGameType", Value::makeInt(p.previousGamemode));
    root.set("Dim", Value::makeInt(static_cast<std::int32_t>(p.dimension)));
    if (p.hasRespawnPoint) {
        root.set("SpawnX", Value::makeInt(p.respawnX));
        root.set("SpawnY", Value::makeInt(p.respawnY));
        root.set("SpawnZ", Value::makeInt(p.respawnZ));
        const char* dimension = p.respawnDimension == -1
                                    ? "minecraft:the_nether"
                                    : (p.respawnDimension == 1
                                           ? "minecraft:the_end"
                                           : "minecraft:overworld");
        root.set("SpawnDimension", Value::makeString(dimension));
        root.set("SpawnAngle", Value::makeFloat(p.respawnAngle));
        root.set("SpawnForced", Value::makeByte(0));
    } else {
        for (const std::string_view key : {
                 "SpawnX", "SpawnY", "SpawnZ", "SpawnDimension",
                 "SpawnAngle", "SpawnForced"})
            erasePlayerRootKey(root, key);
    }
    // Pos as List<Double> 3
    {
        Value pos = Value::makeList(Double);
        Value vx; vx.tag = Double; vx.d = p.x; pos.list.push_back(vx);
        Value vy; vy.tag = Double; vy.d = p.y; pos.list.push_back(vy);
        Value vz; vz.tag = Double; vz.d = p.z; pos.list.push_back(vz);
        root.set("Pos", std::move(pos));
    }
    // Keep the two vanilla inventories on one serializer so component and
    // removed-component handling cannot drift between them.
    writePlayerInventory(root, p.inv.data(), static_cast<int>(p.inv.size()));
    writeItemList(root, "EnderItems", p.enderItems.data(),
                  static_cast<int>(p.enderItems.size()));
    {
        std::vector<std::string> tags(p.scoreboardTags.begin(),
                                      p.scoreboardTags.end());
        std::sort(tags.begin(), tags.end());
        // Vanilla's NbtList forgets its element type when it becomes empty;
        // the on-disk marker for an empty list is TAG_End, not TAG_String.
        Value savedTags = Value::makeList(
            tags.empty() ? nbt::End : nbt::String);
        savedTags.list.reserve(tags.size());
        for (auto& tag : tags)
            savedTags.list.push_back(Value::makeString(std::move(tag)));
        root.set("Tags", std::move(savedTags));
    }
    WriteBuffer out;
    writeFileRoot(out, root);
    if (out.data.size() > kMaxPlayerDataBytes) {
        std::fprintf(stderr,
                     "[cppfm] refusing oversized player data save for %s (%zu bytes; max %zu)\n",
                     path.c_str(), out.data.size(), kMaxPlayerDataBytes);
        return false;
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        if (!storage_file_ops::createDirectoriesDurably(
                parent, directoryError))
            throw std::filesystem::filesystem_error(
                "create durable player-data directory", parent,
                directoryError);
    }
    if (out.data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::length_error("player data is too large");
    // Atomic: write to a sibling temporary file and replace the destination.
    const std::filesystem::path tmpBase = path + ".new";
    std::filesystem::path tmp;
    bool tmpOwned = false;
    std::error_code writeError;
    if (!storage_file_ops::writeFileToNewSibling(
            tmpBase, out.data.data(), out.data.size(), tmp, writeError,
            &tmpOwned, 0666)) {
        if (tmpOwned) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
        }
        std::fprintf(stderr, "[cppfm] player data save failed for %s: %s\n",
                     path.c_str(), writeError.message().c_str());
        return false;
    }
    std::error_code ec;
    if (!storage_file_ops::replaceFile(tmp, path, ec)) {
        if (tmpOwned) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
        }
        std::fprintf(stderr, "[cppfm] player data save failed for %s: %s\n",
                     path.c_str(), ec.message().c_str());
        return false;
    }
    tmpOwned = false;
    if (!storage_file_ops::syncDirectory(parent)) {
        std::fprintf(stderr, "[cppfm] player data save failed for %s: cannot sync player-data directory\n",
                     path.c_str());
        return false;
    }
    return true;
}

static bool loadPlayerNBT(const std::string& path, Player& p,
                          bool* unsupportedItemData = nullptr,
                          bool* preserveOriginalOnFailure = nullptr) {
    if (unsupportedItemData) *unsupportedItemData = false;
    if (preserveOriginalOnFailure) *preserveOriginalOnFailure = false;
    const auto preserveOriginal = [&] {
        if (preserveOriginalOnFailure)
            *preserveOriginalOnFailure = true;
    };
    std::lock_guard playerLock(p.stateMtx);
    // Parse into detached state first. A late malformed item used to fail
    // after position, health, or earlier inventory slots had already changed
    // on the live Player; quarantine then left that partial state in service.
    float health = p.health;
    std::int32_t food = p.food;
    float saturation = p.saturation;
    XpState xp = p.xp;
    std::uint8_t gameMode = p.gamemode;
    std::uint8_t previousGameMode = p.previousGamemode;
    std::int8_t dimension = p.dimension;
    bool hasRespawnPoint = p.hasRespawnPoint;
    std::int32_t respawnX = p.respawnX;
    std::int32_t respawnY = p.respawnY;
    std::int32_t respawnZ = p.respawnZ;
    std::int8_t respawnDimension = p.respawnDimension;
    float respawnAngle = p.respawnAngle;
    double x = p.x;
    double y = p.y;
    double z = p.z;
    double prevFeetY = p.prevFeetY;
    auto inventory = p.inv;
    auto enderItems = p.enderItems;
    // Tags are optional in older player saves. A missing field means the
    // player has no saved tags, not that this load should retain stale tags.
    std::unordered_set<std::string> scoreboardTags;
    std::vector<std::uint8_t> bytes;
    std::error_code readError;
    const auto readStatus = storage_file_ops::readRegularFileBounded(
        path, kMaxPlayerDataBytes, bytes, readError);
    if (readStatus != storage_file_ops::BoundedReadStatus::Read) {
        std::fprintf(stderr,
                     "[cppfm] refusing unsafe, unreadable, or oversized player data %s: %s\n",
                     path.c_str(), readError.message().c_str());
        preserveOriginal();
        return false;
    }
    if (bytes.empty()) return false;
    try {
        ReadBuffer r(bytes);
        nbt::Parser parser(r);
        nbt::Value root = parser.readFileRoot();
        if (const auto* v = root.get("Health")) {
            if (v->tag == nbt::Float) health = v->f;
            else if (v->tag == nbt::Double) health = static_cast<float>(v->d);
            else if (v->tag == nbt::Int) health = static_cast<float>(v->i);
        }
        if (const auto* v = root.get("foodLevel")) {
            if (v->tag == nbt::Int) food = v->i;
            else if (v->tag == nbt::Byte) food = v->b;
            else if (v->tag == nbt::Short) food = v->s;
        }
        if (const auto* v = root.get("foodSaturation")) {
            if (v->tag == nbt::Float) saturation = v->f;
            else if (v->tag == nbt::Double) saturation = static_cast<float>(v->d);
        }
        const auto readXpInteger = [](const nbt::Value* value,
                                      std::int32_t& result,
                                      const char* key) {
            if (!value) return;
            std::int64_t parsed = 0;
            switch (value->tag) {
            case nbt::Byte: parsed = value->b; break;
            case nbt::Short: parsed = value->s; break;
            case nbt::Int: parsed = value->i; break;
            case nbt::Long: parsed = value->l; break;
            default:
                throw std::runtime_error(std::string("invalid player ") +
                                         key + " tag");
            }
            if (parsed < 0 || parsed >
                    std::numeric_limits<std::int32_t>::max())
                throw std::runtime_error(std::string("invalid player ") +
                                         key + " value");
            result = static_cast<std::int32_t>(parsed);
        };
        readXpInteger(root.get("XpLevel"), xp.level, "XpLevel");
        readXpInteger(root.get("XpTotal"), xp.totalXp, "XpTotal");
        if (const auto* v = root.get("XpP")) {
            if (v->tag == nbt::Float) xp.progress = v->f;
            else if (v->tag == nbt::Double) xp.progress = static_cast<float>(v->d);
            else throw std::runtime_error("invalid player XpP tag");
            if (!std::isfinite(xp.progress) || xp.progress < 0.0f ||
                xp.progress >= 1.0f)
                throw std::runtime_error("invalid player XpP value");
        }
        auto readGameMode = [](const nbt::Value* value, std::uint8_t& out,
                               const char* key) {
            if (!value) return;
            std::int32_t mode = -1;
            if (value->tag == nbt::Byte) mode = value->b;
            else if (value->tag == nbt::Short) mode = value->s;
            else if (value->tag == nbt::Int) mode = value->i;
            if (mode < 0 || mode > 3)
                throw std::runtime_error(std::string("invalid saved player ") + key);
            out = static_cast<std::uint8_t>(mode);
        };
        readGameMode(root.get("playerGameType"), gameMode, "playerGameType");
        readGameMode(root.get("previousPlayerGameType"), previousGameMode,
                     "previousPlayerGameType");
        if (const auto* v = root.get("Dim")) {
            if (v->tag == nbt::Int) dimension = static_cast<std::int8_t>(v->i);
            else if (v->tag == nbt::Byte) dimension = static_cast<std::int8_t>(v->b);
        }
        if (const auto* sx = root.get("SpawnX")) {
            const auto* sy = root.get("SpawnY");
            const auto* sz = root.get("SpawnZ");
            auto readCoordinate = [](const nbt::Value* value,
                                     std::int32_t& out) {
                if (!value) return false;
                switch (value->tag) {
                case nbt::Byte: out = value->b; return true;
                case nbt::Short: out = value->s; return true;
                case nbt::Int: out = value->i; return true;
                case nbt::Long: out = static_cast<std::int32_t>(value->l); return true;
                default: return false;
                }
            };
            if (readCoordinate(sx, respawnX) &&
                readCoordinate(sy, respawnY) &&
                readCoordinate(sz, respawnZ)) {
                hasRespawnPoint = true;
                if (const auto* dimension = root.get("SpawnDimension")) {
                    if (dimension->tag == nbt::String) {
                        if (dimension->str == "minecraft:the_nether")
                            respawnDimension = -1;
                        else if (dimension->str == "minecraft:the_end")
                            respawnDimension = 1;
                        else
                            respawnDimension = 0;
                    } else if (dimension->tag == nbt::Int) {
                        respawnDimension = static_cast<std::int8_t>(dimension->i);
                    } else if (dimension->tag == nbt::Byte) {
                        respawnDimension = static_cast<std::int8_t>(dimension->b);
                    }
                } else {
                    respawnDimension = 0;
                }
                if (const auto* angle = root.get("SpawnAngle")) {
                    if (angle->tag == nbt::Float) respawnAngle = angle->f;
                    else if (angle->tag == nbt::Double)
                        respawnAngle = static_cast<float>(angle->d);
                }
            }
        }
        if (const auto* v = root.get("Pos")) {
            if (v->tag != nbt::List || v->list.size() != 3 ||
                (v->listElement != nbt::Double &&
                 v->listElement != nbt::Float))
                throw std::runtime_error("invalid saved player Pos list");
            for (const auto& coordinate : v->list)
                if (coordinate.tag != v->listElement)
                    throw std::runtime_error(
                        "mixed numeric tags in saved player Pos list");
            const auto coordinateValue = [](const nbt::Value& value) {
                return value.tag == nbt::Double ? value.d
                                                : static_cast<double>(value.f);
            };
            x = coordinateValue(v->list[0]);
            y = coordinateValue(v->list[1]);
            z = coordinateValue(v->list[2]);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                throw std::runtime_error("non-finite saved player position");
            prevFeetY = y;
        }
        // helper to load Inventory or EnderItems
        auto loadItems = [&](const char* key, ItemStack* dst, int dstSize){
            const auto* lst = root.get(key);
            if (!lst) return;
            if (lst->tag != nbt::List ||
                (!lst->list.empty() && lst->listElement != nbt::Compound))
                throw std::runtime_error(std::string("invalid saved player ") +
                                         key + " list");
            std::vector<bool> seen(static_cast<std::size_t>(dstSize), false);
            for (const auto& item : lst->list) {
                if (item.tag != nbt::Compound)
                    throw std::runtime_error(std::string("invalid saved player ") +
                                             key + " entry");
                const auto* sv = item.get("Slot");
                if (!sv)
                    throw std::runtime_error("saved player item has no Slot");
                int slot = 0;
                if (sv->tag == nbt::Byte) slot = sv->b;
                else if (sv->tag == nbt::Int) slot = sv->i;
                else if (sv->tag == nbt::Short) slot = sv->s;
                else throw std::runtime_error(
                    "saved player item Slot has a non-integral tag");
                if (slot < 0 || slot >= dstSize) {
                    // Accept vanilla's special player-equipment slots while
                    // rejecting arbitrary indices that would otherwise be
                    // silently discarded. The in-range 0..45 layout remains
                    // the format emitted by this server.
                    if (std::string_view(key) == "Inventory" &&
                        slot >= 100 && slot <= 103 && dstSize > 8)
                        slot -= 95; // vanilla feet..head => internal [5, 8]
                    else if (std::string_view(key) == "Inventory" &&
                             slot == -106 && dstSize > 45)
                        slot = 45; // offhand
                    else
                        throw std::runtime_error(
                            "saved player item Slot is outside the supported inventory");
                }
                if (seen[static_cast<std::size_t>(slot)])
                    throw std::runtime_error(
                        "duplicate saved player item slot");
                seen[static_cast<std::size_t>(slot)] = true;
                const auto* savedId = item.get("id");
                if (savedId && savedId->tag == nbt::String &&
                    !gen::itemIdByName().contains(savedId->str)) {
                    if (unsupportedItemData) *unsupportedItemData = true;
                    preserveOriginal();
                    throw std::runtime_error(
                        "saved player item ID is not registered by this server");
                }
                if (auto stack = ItemStack::fromNbt(item))
                    dst[slot] = std::move(*stack);
                else
                    throw std::runtime_error(
                        "unsupported or malformed saved player item");
            }
        };
        // clear before load (important for rejoin)
        // Do not clear player position/health already set; inventory is overwritten per slot, but clear empty slots remain air – we keep existing air for slots not in file
        // To ensure round-trip, we should clear inventory before loading? We'll clear only if list exists
        if (root.get("Inventory")) {
            for (auto &s : inventory) s = ItemStack::air();
        }
        if (root.get("EnderItems")) {
            for (auto &s : enderItems) s = ItemStack::air();
        }
        loadItems("Inventory", inventory.data(),
                  static_cast<int>(kPlayerInventorySlotCount));
        loadItems("EnderItems", enderItems.data(), 27);
        if (const auto* tags = root.get("Tags")) {
            const bool validTagsList = tags->tag == nbt::List &&
                (tags->list.empty()
                     ? tags->listElement == nbt::End ||
                           tags->listElement == nbt::String
                     : tags->listElement == nbt::String);
            if (!validTagsList)
                throw std::runtime_error("invalid saved player Tags list");
            scoreboardTags.clear();
            for (const auto& tag : tags->list) {
                if (tag.tag != nbt::String)
                    throw std::runtime_error(
                        "saved player Tags list contains a non-string value");
                if (scoreboardTags.size() < Player::kMaxScoreboardTags)
                    scoreboardTags.insert(tag.str);
            }
        }
        p.health = health;
        p.food = food;
        p.saturation = saturation;
        p.xp = xp;
        p.gamemode = gameMode;
        p.previousGamemode = previousGameMode;
        p.dimension = dimension;
        p.hasRespawnPoint = hasRespawnPoint;
        p.respawnX = respawnX;
        p.respawnY = respawnY;
        p.respawnZ = respawnZ;
        p.respawnDimension = respawnDimension;
        p.respawnAngle = respawnAngle;
        p.x = x;
        p.y = y;
        p.z = z;
        p.prevFeetY = prevFeetY;
        p.inv = std::move(inventory);
        p.enderItems = std::move(enderItems);
        p.scoreboardTags = std::move(scoreboardTags);
        return true;
    } catch (const std::bad_alloc&) {
        preserveOriginal();
        return false;
    } catch (...) { return false; }
}

void GameServer::saveLevelData() {
    persistence().saveLevelData(tickNo_, dayTime());
}
void GameServer::loadLevelData() {
    persistence().loadLevelData();
}
bool GameServer::savePlayerData(const std::string& uuidHex, Player& p) {
    const std::string path = cfg_.worldDir + "/playerdata/" + uuidHex + ".dat";
    try {
        if (!savePlayerNBT(path, p)) {
            std::fprintf(stderr, "[cppfm] player data save failed for %s\n", path.c_str());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] player data save failed for %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] player data save failed for %s\n", path.c_str());
    }
    return false;
}
bool GameServer::loadPlayerData(const std::string& uuidHex, Player& p,
                                bool* dataFilePresent,
                                bool* unsupportedItemData,
                                bool* unsafeExistingData) {
    // the player starts fresh; neighbours and startup are unaffected.
    const std::string path =
        cfg_.worldDir + "/playerdata/" + uuidHex + ".dat";
    std::error_code existenceError;
    const bool fileExists = std::filesystem::exists(path, existenceError);
    if (dataFilePresent)
        *dataFilePresent = fileExists || static_cast<bool>(existenceError);
    bool unsupported = false;
    bool preserveOriginal = false;
    bool unsafe = false;
    const bool loaded = loadPlayerDataIsolated(
        path,
        [&](const std::string& loadPath) {
            try {
                return loadPlayerNBT(loadPath, p, &unsupported,
                                     &preserveOriginal);
            } catch (const std::bad_alloc&) {
                preserveOriginal = true;
                return false;
            } catch (const std::system_error&) {
                preserveOriginal = true;
                return false;
            }
        }, nullptr, &preserveOriginal, &unsafe);
    if (unsupportedItemData) *unsupportedItemData = unsupported;
    if (unsafeExistingData) *unsafeExistingData = unsafe;
    return loaded;
}
void GameServer::invalidateRespawnPointsAt(std::int8_t dimension,
                                            std::int32_t x, std::int32_t y,
                                            std::int32_t z) {
    const auto targetDimension = canonicalDimension(dimension);
    for (auto& player : playersSnapshot()) {
        if (!player) continue;
        bool invalidated = false;
        std::string uuid;
        std::string name;
        {
            std::lock_guard playerLock(player->stateMtx);
            if (!player->hasRespawnPoint ||
                canonicalDimension(player->respawnDimension) != targetDimension ||
                player->respawnY != y) {
                continue;
            }
            const int dx = std::abs(player->respawnX - x);
            const int dz = std::abs(player->respawnZ - z);
            // A bed occupies two horizontal blocks.  The caller only invokes
            // this for bed states, so matching either half is sufficient.
            if (dx + dz > 1) continue;
            player->hasRespawnPoint = false;
            uuid = uuidToHex(player->uuid);
            name = player->name;
            invalidated = true;
        }
        if (!invalidated) {
            continue;
        }
        // Persistence takes the same player snapshot lock internally; do not
        // hold it across filesystem I/O or the log callback.
        savePlayerData(uuid, *player);
        std::fprintf(stderr,
                     "[cppfm] cleared respawn point for %s after bed break\n",
                     name.c_str());
    }
}
void GameServer::loadOps() {
    ops_.clear();
    json::Value root;
    if (readAdminJsonFile("ops.json", root, "operators") ==
        AdminJsonReadStatus::Loaded)
        loadNameSet(root, ops_, "name", "ops.json");
    // also allow ops.txt one name per line fallback
    std::vector<std::uint8_t> bytes;
    std::error_code readError;
    const auto readStatus = storage_file_ops::readRegularFileBounded(
        "ops.txt", kMaxAdminFileBytes, bytes, readError);
    if (readStatus == storage_file_ops::BoundedReadStatus::Read) {
        std::istringstream lines{std::string(bytes.begin(), bytes.end())};
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (!line.empty()) ops_.insert(line);
        }
    } else if (readStatus != storage_file_ops::BoundedReadStatus::Missing) {
        std::fprintf(stderr, "[cppfm] could not safely read ops.txt: %s\n",
                     readError.message().c_str());
    }
}
void GameServer::saveOps() const {
    json::Value root = json::Value::array();
    for (const auto& name : ops_) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::ofString(name));
        entry.set("level", json::Value::ofNumber(4));
        entry.set("bypassesPlayerLimit", json::Value::ofBool(false));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("ops.json", root, "operators");
}
void GameServer::loadBans() {
    json::Value root;
    const auto status = readAdminJsonFile(
        "banned-players.json", root, "banned players");
    if (status == AdminJsonReadStatus::Missing) {
        bannedPlayers_.clear();
        return;
    }
    if (status != AdminJsonReadStatus::Loaded)
        throw std::runtime_error(
            "refusing to start with unreadable banned-players.json");

    std::unordered_set<std::string> loadedPlayers;
    if (root.isArr()) {
        for (const auto& entry : root.arr) {
            if (entry.isStr() && !entry.str.empty()) {
                loadedPlayers.insert(entry.str);
                continue;
            }
            const auto* name = entry.isObj() ? entry.find("name") : nullptr;
            if (!name || !name->isStr() || name->str.empty())
                throw std::runtime_error(
                    "refusing to start with invalid banned-players.json entry");
            loadedPlayers.insert(name->str);
        }
    } else if (root.isObj()) {
        for (const auto& [name, _] : root.obj) {
            if (name.empty())
                throw std::runtime_error(
                    "refusing to start with invalid banned-players.json key");
            loadedPlayers.insert(name);
        }
    } else {
        throw std::runtime_error(
            "refusing to start with invalid banned-players.json root");
    }
    bannedPlayers_.swap(loadedPlayers);
}
void GameServer::saveBans() const {
    json::Value root = json::Value::array();
    for (const auto& name : bannedPlayers_) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::ofString(name));
        entry.set("reason", json::Value::ofString("Banned by an operator."));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("banned-players.json", root, "banned players");
}
void GameServer::loadBannedIps() {
    json::Value root;
    const auto status = readAdminJsonFile(
        "banned-ips.json", root, "banned IPs");
    if (status == AdminJsonReadStatus::Missing) {
        bannedIps_.clear();
        return;
    }
    if (status != AdminJsonReadStatus::Loaded)
        throw std::runtime_error(
            "refusing to start with unreadable banned-ips.json");

    std::unordered_set<std::string> loadedIps;
    if (root.isArr()) {
        for (const auto& entry : root.arr) {
            if (entry.isStr() && !entry.str.empty()) {
                loadedIps.insert(entry.str);
                continue;
            }
            if (!entry.isObj())
                throw std::runtime_error(
                    "refusing to start with invalid banned-ips.json entry");
            const auto* ip = entry.find("ip");
            const auto* name = entry.find("name");
            if (ip && ip->isStr() && !ip->str.empty()) {
                loadedIps.insert(ip->str);
            } else if (name && name->isStr() && !name->str.empty()) {
                loadedIps.insert(name->str);
            } else {
                throw std::runtime_error(
                    "refusing to start with invalid banned-ips.json entry");
            }
        }
    } else if (root.isObj()) {
        for (const auto& [ip, _] : root.obj) {
            if (ip.empty())
                throw std::runtime_error(
                    "refusing to start with invalid banned-ips.json key");
            loadedIps.insert(ip);
        }
    } else {
        throw std::runtime_error(
            "refusing to start with invalid banned-ips.json root");
    }
    bannedIps_.swap(loadedIps);
}
void GameServer::saveBannedIps() const {
    json::Value root = json::Value::array();
    for (const auto& ip : bannedIps_) {
        json::Value entry = json::Value::object();
        entry.set("ip", json::Value::ofString(ip));
        entry.set("reason", json::Value::ofString("Banned by an operator."));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("banned-ips.json", root, "banned IPs");
}
void GameServer::saveWhitelist() const {
    if (!whitelist_.save("whitelist.json"))
        std::fprintf(stderr, "[cppfm] whitelist save failed\n");
}
void GameServer::kickPlayer(const std::string& name, const std::string& reason) {
    std::shared_ptr<Player> target;
    for (auto& p : playersSnapshot()) {
        if (!p) continue;
        std::lock_guard playerLock(p->stateMtx);
        if (p->name == name) {
            target = p;
            break;
        }
    }
    if (!target) return;
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard playerLock(target->stateMtx);
        connection = target->conn;
    }
    if (!connection) return;
    std::string txt = reason.empty() ? "Kicked by an operator." : reason;
    WriteBuffer b;
    nbt::writeTextComponent(b, txt);
    connection->trySendPacket(proto::pl::sc::Disconnect, b);
    connection->close();
}
void GameServer::sendWorldBorderTo(Player& p) const {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard playerLock(p.stateMtx);
        connection = p.conn;
    }
    if (!connection) return;
    // InitializeWorldBorder full packet — Yarn WorldBorder 59999968, lerp interpolation
    WriteBuffer i;
    i.f64(worldBorderCenterX_); i.f64(worldBorderCenterZ_);
    double oldSize = worldBorderDiameter_;
    double newSize = worldBorderDiameter_;
    std::int64_t lerpMs = 0;
    if (worldBorderLerpRemainingTicks_ > 0) {
        oldSize = worldBorderDiameter_;
        newSize = worldBorderLerpTo_;
        lerpMs = worldBorderLerpMs_;
    }
    i.f64(oldSize); i.f64(newSize);
    i.varlong(lerpMs);
    i.varint((int)constants::kWorldBorderDiameter); // max world border (portalTeleportBoundary)
    i.varint(5);  // warning blocks
    i.varint(15); // warning time
    connection->trySendPacket(proto::pl::sc::InitializeWorldBorder, i);
    // also send Center for spec compliance (separate packet)
    WriteBuffer c;
    c.f64(worldBorderCenterX_); c.f64(worldBorderCenterZ_);
    connection->trySendPacket(proto::pl::sc::WorldBorderCenter, c);
    // Lerp-specific separate packets if active
    if (worldBorderLerpRemainingTicks_ > 0) {
        WriteBuffer l;
        l.f64(oldSize); l.f64(newSize); l.varlong(lerpMs);
        connection->trySendPacket(proto::pl::sc::WorldBorderLerpSize, l);
    } else {
        WriteBuffer s;
        s.f64(newSize);
        connection->trySendPacket(proto::pl::sc::WorldBorderSize, s);
    }
}
void GameServer::broadcastWorldBorder() {
    for (auto& p : playersSnapshot()) {
        if (p) sendWorldBorderTo(*p);
    }
    if (auto* persistence = persistenceFor(0)) {
        if (worldBorderLerpRemainingTicks_ > 0) {
            double cur = worldBorderDiameter_;
            persistence->setWorldBorder(cur, worldBorderCenterX_, worldBorderCenterZ_);
            persistence->setWorldBorderLerp(cur, worldBorderLerpTo_, worldBorderLerpRemainingTicks_);
        } else {
            persistence->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
        }
        persistence->saveLevelData(tickNo_, dayTime());
    }
}
std::string GameServer::dispatchConsole(const std::string& line) {
    if (shutdownStarted_.load(std::memory_order_acquire))
        return "error: server is stopping";
    std::string command = line;
    std::string javaResponse;
    // RCON/console is another command ingress.  A Java-registered command is
    // consumed by the embedded bridge; rewritten/unknown commands continue
    // through the authoritative native dispatcher below.
    if (jvmRuntime_ && !jvmRuntime_->onCommand(nullptr, command, &javaResponse))
        return javaResponse.empty() ? "OK" : javaResponse;
    // Serialize console/RCON mutation with the authoritative tick and with
    // session command ingress. The recursive gates allow callbacks to re-enter.
    SimulationDispatchGuard simulationLock(simulationDispatchMtx_);
    std::lock_guard commandLock(commandDispatchMtx_);
    brigadier::CommandSource src;
    src.console = true;
    src.srcX = 0; src.srcY = -60; src.srcZ = 0;
    bindCommandSelector(src);
    std::string captured;
    consoleCapture_ = &captured;
    try {
        const auto res = commands_.execute(command, std::move(src));
        consoleCapture_ = nullptr;
        if (!captured.empty()) return captured;
        return res.ok ? "OK" : ("error: " + res.errorText);
    } catch (const std::exception& e) {
        consoleCapture_ = nullptr;
        return std::string("error: ") + e.what();
    } catch (...) {
        consoleCapture_ = nullptr;
        return "error: command failed";
    }
}

void GameServer::demandChunkAsync(std::int32_t cx, std::int32_t cz) {
    demandChunkAsyncFor(0, cx, cz);
}

void GameServer::demandChunkAsyncFor(std::int8_t dimension,
                                     std::int32_t cx, std::int32_t cz) {
    dimension = canonicalDimension(dimension);
    const auto k = dimensionChunkKey(dimension, cx, cz);
    if (chunkBodyCache_.contains(dimension, cx, cz)) return;
    if (worldFor(dimension).hasChunk(cx, cz)) return;
    Persistence* persistence = persistenceFor(dimension);
    {
        std::lock_guard lk(pendingLoadsMtx_);
        if (pendingLoads_.count(k)) return;
        const auto ticket = chunkSaveCoordinator_->beginLoad(k);
        if (!ticket) return; // another load or a write owns this chunk's I/O fence
        try {
            World* loadingWorld = &worldFor(dimension);
            auto future = ioPool_.submit([persistence, loadingWorld,
                                          dimension, cx, cz,
                                          stamp = ticket->stamp]{
                Persistence::ChunkBytesRead result;
                try {
                    std::lock_guard ioLock(stamp->ioMutex);
                    if (!persistence) {
                        result.status = Persistence::ChunkBytesRead::Status::Failed;
                        result.error = "persistence runtime is unavailable";
                    } else {
                        loadingWorld->markChunkLoadStarted();
                        result = persistence->loadChunkBytesRecovering(cx, cz);
                    }
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[GameServer] async chunk load dim=%d %d,%d failed: %s\n",
                                 static_cast<int>(dimension), cx, cz, e.what());
                    result.status = Persistence::ChunkBytesRead::Status::Failed;
                    result.error = e.what();
                } catch (...) {
                    std::fprintf(stderr, "[GameServer] async chunk load dim=%d %d,%d failed\n",
                                 static_cast<int>(dimension), cx, cz);
                    result.status = Persistence::ChunkBytesRead::Status::Failed;
                    result.error = "unknown async read failure";
                }
                return result;
            });
            pendingLoads_.emplace(
                k, PendingChunkLoad{ticket->stamp, ticket->generation,
                                    std::move(future)});
        } catch (const std::exception& e) {
            chunkSaveCoordinator_->finishLoad(k, ticket->stamp);
            std::fprintf(stderr, "[GameServer] could not queue chunk load dim=%d %d,%d: %s\n",
                         static_cast<int>(dimension), cx, cz, e.what());
        } catch (...) {
            chunkSaveCoordinator_->finishLoad(k, ticket->stamp);
            std::fprintf(stderr, "[GameServer] could not queue chunk load dim=%d %d,%d\n",
                         static_cast<int>(dimension), cx, cz);
        }
    }
}
void GameServer::saveChunkAsync(std::int32_t cx, std::int32_t cz) {
    saveChunkAsyncFor(0, cx, cz);
}

void GameServer::saveChunkAsyncFor(std::int8_t dimension,
                                   std::int32_t cx, std::int32_t cz) {
    dimension = canonicalDimension(dimension);
    try {
        Persistence* savePersistence = persistenceFor(dimension);
        if (!savePersistence) {
            std::fprintf(stderr,
                         "[GameServer] no persistence owner for chunk dim=%d %d,%d; save not scheduled\n",
                         static_cast<int>(dimension), cx, cz);
            return;
        }
        World& saveWorld = worldFor(dimension);
        if (const auto* structures = saveWorld.structureManager();
            structures && structures->hasPendingLootInChunk(cx, cz)) {
            // A failed/delayed structure-loot transfer must not let an Anvil
            // snapshot persist the chest without its pending LootTable. Leave
            // this dirty chunk resident; the tick queue will retry and the
            // next save attempt can proceed after the marker is in chunk NBT.
            return;
        }
        BlockEntityStore& saveBlockEntities = blockEntitiesFor(dimension);
        // Moving-piston entities are a two-tick transaction.  Complete that
        // transaction while the simulation gate is held, before snapshotting
        // the chunk; otherwise the moving block could outlive its transient BE
        // after restart.
        redstoneFor(dimension).flushPendingPistons(cx, cz);
        auto cacheSnapshot = beginChunkCacheSnapshot(dimension, cx, cz);
        const std::uint64_t dirtyVersion = savePersistence->dirtyVersion(cx, cz);
        std::unordered_map<std::uint16_t, std::string> idxToKey;
        { const auto& order = gameData_.order("minecraft:worldgen/biome");
          for (std::size_t i = 0; i < order.size(); ++i)
              idxToKey.emplace(static_cast<std::uint16_t>(i), order[i]); }
        std::vector<std::uint8_t> nbtBytes;
        std::vector<std::uint8_t> bodyBytes;
        std::uint64_t revision = 0;
        const std::uint32_t biomeIdx = data_.biomeIndex(saveWorld.biomeKey());
        nbt::Value root;
        nbt::Value blockEntityNbt = nbt::Value::makeList(nbt::Compound);
        saveBlockEntities.writeChunkNbt(cx, cz, blockEntityNbt);
        nbt::Value initialChunkBlockEntityNbt =
            nbt::Value::makeList(nbt::Compound);
        saveBlockEntities.writeChunkInitialNbt(
            cx, cz, initialChunkBlockEntityNbt);
        const bool has = saveWorld.withChunk(cx, cz, [&](const Chunk& c) {
            revision = c.revision;

            // Serialize directly from the read-locked chunk.  Copying a full
            // Chunk here used to allocate roughly 0.8 MiB for every dirty
            // unload, then allocate the NBT and cache payloads as well.
            root = chunkToNBT(cx, cz, c, saveWorld.biomeKey(), &idxToKey,
                              saveWorld.verticalGeometry());

            WriteBuffer body;
            serializeLevelChunkBody(body, cx, cz, c, biomeIdx,
                                    saveWorld.verticalGeometry(),
                                    &initialChunkBlockEntityNbt);
            bodyBytes = std::move(body.data);
        });
        if (!has) return;
        // Keep the world snapshot lock out of block-entity serialization. A
        // block-entity tick can hold its entity lock while changing World;
        // taking those locks in the reverse order on this save path could
        // deadlock the simulation thread.
        if (savePersistence)
            savePersistence->mergeOpaqueChunkRootNbt(cx, cz, root);
        if (!blockEntityNbt.list.empty())
            root.set("block_entities", std::move(blockEntityNbt));
        WriteBuffer out;
        nbt::writeFileRoot(out, root);
        nbtBytes = std::move(out.data);
        // Cache body update (tick thread) with the same revision/snapshot as
        // the durable NBT bytes; the I/O worker receives bytes only.
        auto body = std::make_shared<const std::vector<std::uint8_t>>(std::move(bodyBytes));
        (void)storeChunkForSnapshot(cacheSnapshot, revision,
                                    std::move(body));
        const auto key = dimensionChunkKey(dimension, cx, cz);
        const auto coordinator = chunkSaveCoordinator_;
        const auto saveTicket = coordinator->beginSave(key);
        const auto& stamp = saveTicket.stamp;
        try {
                    ioPool_.submit([dimension, cx, cz, key, stamp,
                            saveId = saveTicket.saveId,
                            dirtyVersion, savePersistence,
                            saveBlockEntities = &saveBlockEntities, coordinator,
                            nbtBytes = std::move(nbtBytes)]() mutable {
                bool persisted = false;
                try {
                    // Serialize I/O for one chunk, then hold the region-wide
                    // transaction lock across RegionFile header/data updates.
                    // A newer snapshot supersedes an older one even when both
                    // carry the same block revision (e.g. inventory edits).
                    std::lock_guard ioLock(stamp->ioMutex);
                    if (stamp->latestSaveId.load(std::memory_order_acquire) == saveId) {
                        // The generation check shares the region-file lock
                        // with background flushes, so an older snapshot cannot
                        // pass a pre-lock check and overwrite this save after
                        // waiting for I/O.
                        persisted = savePersistence->storeChunkBytesIfDirtyVersion(
                            cx, cz, dirtyVersion, nbtBytes);
                    }
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[GameServer] async chunk save dim=%d %d,%d failed: %s\n",
                                 static_cast<int>(dimension), cx, cz, e.what());
                } catch (...) {
                    std::fprintf(stderr, "[GameServer] async chunk save dim=%d %d,%d failed\n",
                                 static_cast<int>(dimension), cx, cz);
                }
                coordinator->finishSave(
                    key, stamp, saveId, persisted, [&] {
                        if (savePersistence && dirtyVersion != 0 &&
                            savePersistence->markCleanIfVersion(
                                cx, cz, dirtyVersion))
                            saveBlockEntities->clearRemovalTombstonesForChunk(
                                cx, cz);
                    });
            });
        } catch (...) {
            coordinator->finishSave(key, stamp, saveTicket.saveId, false,
                                     [] {});
            throw;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[GameServer] could not queue chunk save dim=%d %d,%d: %s\n",
                     static_cast<int>(dimension), cx, cz, e.what());
    } catch (...) {
        std::fprintf(stderr, "[GameServer] could not queue chunk save dim=%d %d,%d\n",
                     static_cast<int>(dimension), cx, cz);
    }
}
void GameServer::pollPendingLoads() {
    // Drain ready futures without holding the registry lock while publishing
    // chunks or mutating the world. Network threads can continue to queue.
    std::vector<std::pair<DimensionChunkKey, PendingChunkLoad>> ready;
    {
        std::lock_guard lk(pendingLoadsMtx_);
        for (auto it = pendingLoads_.begin(); it != pendingLoads_.end(); ) {
            if (it->second.future.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                ready.emplace_back(it->first, std::move(it->second));
                it = pendingLoads_.erase(it);
                continue;
            }
            ++it;
        }
    }
    for (auto& [key, load] : ready) {
        const std::int8_t dimension = canonicalDimension(key.dimension);
        const std::int32_t cx = key.cx;
        const std::int32_t cz = key.cz;
        Persistence::ChunkBytesRead readResult;
        try { readResult = load.future.get(); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[GameServer] async chunk future dim=%d %d,%d failed: %s\n",
                         static_cast<int>(dimension), cx, cz, e.what());
            readResult.status = Persistence::ChunkBytesRead::Status::Failed;
            readResult.error = e.what();
        } catch (...) {
            std::fprintf(stderr, "[GameServer] async chunk future dim=%d %d,%d failed\n",
                         static_cast<int>(dimension), cx, cz);
            readResult.status = Persistence::ChunkBytesRead::Status::Failed;
            readResult.error = "unknown async future failure";
        }
        World& loadedWorld = worldFor(dimension);
        Persistence* persistence = persistenceFor(dimension);
        std::unique_lock<std::mutex> loadFence;
        if (!chunkSaveCoordinator_->lockCurrentLoad(
                key, {load.stamp, load.generation}, loadFence)) {
            if (loadFence.owns_lock()) loadFence.unlock();
            chunkSaveCoordinator_->finishLoad(key, load.stamp);
            std::fprintf(stderr,
                         "[GameServer] discarded stale async chunk snapshot dim=%d %d,%d\n",
                         static_cast<int>(dimension), cx, cz);
            continue;
        }
        struct LoadCompletion final {
            std::shared_ptr<ChunkSaveCoordinator> coordinator;
            DimensionChunkKey key;
            std::shared_ptr<ChunkSaveCoordinator::Stamp> stamp;
            std::unique_lock<std::mutex>& fence;

            ~LoadCompletion() noexcept {
                if (fence.owns_lock()) fence.unlock();
                try {
                    coordinator->finishLoad(key, stamp);
                } catch (...) {
                    std::fprintf(stderr,
                                 "[GameServer] failed to release chunk load fence\n");
                }
            }
        } loadCompletion{chunkSaveCoordinator_, key, load.stamp, loadFence};
        if (readResult.status == Persistence::ChunkBytesRead::Status::Failed) {
            std::fprintf(stderr,
                         "[GameServer] refusing to regenerate chunk dim=%d %d,%d after read failure: %s\n",
                         static_cast<int>(dimension), cx, cz,
                         readResult.error.empty() ? "unknown read failure"
                                                  : readResult.error.c_str());
            continue;
        }
        if (readResult.status == Persistence::ChunkBytesRead::Status::Missing) {
            // No stored chunk -> generate via WorldGen (tick thread, seed-safe).
            // Keep the same-key fence until the synchronous fallback finishes
            // so a save cannot begin between the empty read and generation.
            try {
                loadedWorld.generateChunkIfMissing(cx, cz);
            } catch (const std::exception& e) {
                std::fprintf(stderr,
                             "[GameServer] missing chunk generation dim=%d %d,%d failed: %s\n",
                             static_cast<int>(dimension), cx, cz, e.what());
            } catch (...) {
                std::fprintf(stderr,
                             "[GameServer] missing chunk generation dim=%d %d,%d failed\n",
                             static_cast<int>(dimension), cx, cz);
            }
            continue;
        }
        auto bytes = std::move(readResult.bytes);
        try {
            Chunk chunk;
            bool decoded = false;
            std::function<void()> publishCallback;
            {
                ChunkBuildWriter writer(chunk);
                decoded = persistence &&
                          persistence->decodeChunkBytes(bytes, writer, cx, cz);
                if (decoded)
                    publishCallback = writer.takePublishCallback();
            }
            if (decoded) {
                loadedWorld.installLoadedChunk(
                    cx, cz, std::move(chunk), std::move(publishCallback));
                auto cacheSnapshot =
                    beginChunkCacheSnapshot(dimension, cx, cz);
                // Populate the cache with the encoded body.
                nbt::Value blockEntityNbt =
                    nbt::Value::makeList(nbt::Compound);
                blockEntitiesFor(dimension).writeChunkInitialNbt(
                    cx, cz, blockEntityNbt);
                auto body = std::make_shared<const std::vector<std::uint8_t>>([&]{
                    WriteBuffer wb;
                    const std::uint32_t biomeIdx =
                        data_.biomeIndex(loadedWorld.biomeKey());
                    loadedWorld.withChunk(cx, cz, [&](const Chunk& c) {
                        serializeLevelChunkBody(wb, cx, cz, c, biomeIdx,
                                                loadedWorld.verticalGeometry(),
                                                &blockEntityNbt);
                    });
                    return wb.data;
                }());
                const std::uint64_t rev = loadedWorld.revisionAt(cx, cz);
                (void)storeChunkForSnapshot(cacheSnapshot, rev,
                                            std::move(body));
            } else {
                std::fprintf(stderr,
                             "[GameServer] validated chunk bytes dim=%d %d,%d failed publication decode; preserving persisted data\n",
                             static_cast<int>(dimension), cx, cz);
            }
        } catch (const ChunkPublishError& e) {
            std::fprintf(stderr,
                         "[GameServer] chunk extras dim=%d %d,%d could not be published: %s; persisted data retained for retry\n",
                         static_cast<int>(dimension), cx, cz, e.what());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[GameServer] stored chunk dim=%d %d,%d rejected: %s; preserving persisted data\n",
                         static_cast<int>(dimension), cx, cz, e.what());
        } catch (...) {
            std::fprintf(stderr, "[GameServer] stored chunk dim=%d %d,%d rejected; preserving persisted data\n",
                         static_cast<int>(dimension), cx, cz);
        }
    }
}
bool GameServer::chunkFitsAnvilEntry(std::int8_t dimension,
                                    std::int32_t cx, std::int32_t cz) {
    dimension = canonicalDimension(dimension);
    auto* storage = persistenceFor(dimension);
    if (!storage) return false;
    const auto writeBlockEntities =
        [this, dimension](std::int32_t writeCx, std::int32_t writeCz,
                          nbt::Value& root) {
            auto entities = nbt::Value::makeList(nbt::Compound);
            if (!blockEntitiesFor(dimension).writeChunkNbt(
                    writeCx, writeCz, entities, {}, true))
                throw std::runtime_error(
                    "block-entity admission snapshot is contended");
            if (!entities.list.empty())
                root.set("block_entities", std::move(entities));
        };
    return storage->chunkFitsAnvilEntry(cx, cz, writeBlockEntities);
}

bool GameServer::blockEntityCandidatesFitAnvilEntry(
    std::int8_t dimension,
    const std::vector<BlockEntityStore::SnapshotOverride>& candidates) {
    if (candidates.empty()) return true;
    dimension = canonicalDimension(dimension);
    auto* storage = persistenceFor(dimension);
    if (!storage) return false;

    std::vector<std::pair<std::int32_t, std::int32_t>> checkedChunks;
    checkedChunks.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto x = posKeyUnpackX(candidate.key);
        const auto y = posKeyUnpackY(candidate.key);
        const auto z = posKeyUnpackZ(candidate.key);
        if (!worldFor(dimension).verticalGeometry().containsY(y)) return false;
        const auto cx = x >> 4;
        const auto cz = z >> 4;
        const auto chunk = std::pair{cx, cz};
        if (std::find(checkedChunks.begin(), checkedChunks.end(), chunk) !=
            checkedChunks.end())
            continue;
        checkedChunks.push_back(chunk);

        const auto writeBlockEntities =
            [this, dimension, &candidates](std::int32_t writeCx,
                                            std::int32_t writeCz,
                                            nbt::Value& root) {
                auto entities = nbt::Value::makeList(nbt::Compound);
                if (!blockEntitiesFor(dimension).writeChunkNbt(
                        writeCx, writeCz, entities, candidates, true))
                    throw std::runtime_error(
                        "block-entity admission snapshot is contended");
                if (!entities.list.empty())
                    root.set("block_entities", std::move(entities));
            };
        if (!storage->chunkFitsAnvilEntry(cx, cz, writeBlockEntities))
            return false;
    }
    return true;
}

bool GameServer::blockMutationCandidatesFitAnvilEntries(
    std::int8_t dimension,
    const std::vector<World::BlockUpdate>& blockUpdates,
    const std::vector<BlockEntityStore::NbtReplacement>& replacements) {
    dimension = canonicalDimension(dimension);
    auto* storage = persistenceFor(dimension);
    if (!storage) return false;
    const auto geometry = worldFor(dimension).verticalGeometry();

    std::vector<std::pair<std::int32_t, std::int32_t>> chunks;
    chunks.reserve(blockUpdates.size() + replacements.size());
    std::unordered_map<std::int64_t, std::vector<World::BlockUpdate>>
        blockUpdatesByChunk;
    blockUpdatesByChunk.reserve(blockUpdates.size());
    std::unordered_map<
        std::int64_t,
        std::vector<const BlockEntityStore::NbtReplacement*>>
        replacementsByChunk;
    replacementsByChunk.reserve(replacements.size());
    for (const auto& update : blockUpdates) {
        if (!geometry.containsY(update.y) ||
            !gen::blockByState(update.state))
            return false;
        const auto cx = update.x >> 4;
        const auto cz = update.z >> 4;
        chunks.emplace_back(cx, cz);
        blockUpdatesByChunk[chunkKey(cx, cz)].push_back(update);
    }
    for (const auto& replacement : replacements) {
        if (!geometry.containsY(replacement.y)) return false;
        const auto cx = replacement.x >> 4;
        const auto cz = replacement.z >> 4;
        chunks.emplace_back(cx, cz);
        replacementsByChunk[chunkKey(cx, cz)].push_back(&replacement);
    }
    std::sort(chunks.begin(), chunks.end());
    chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());

    for (const auto& [cx, cz] : chunks) {
        const auto key = chunkKey(cx, cz);
        const auto blockGroup = blockUpdatesByChunk.find(key);
        const auto replacementGroup = replacementsByChunk.find(key);
        auto chunkUpdates = blockGroup == blockUpdatesByChunk.end()
            ? std::vector<World::BlockUpdate>{}
            : blockGroup->second;
        auto chunkReplacements = replacementGroup == replacementsByChunk.end()
            ? std::vector<const BlockEntityStore::NbtReplacement*>{}
            : replacementGroup->second;

        const auto writeProjection =
            [this, dimension, geometry, cx, cz,
             chunkUpdates = std::move(chunkUpdates),
             chunkReplacements = std::move(chunkReplacements)](
                std::int32_t writeCx, std::int32_t writeCz,
                nbt::Value& root) {
                if (writeCx != cx || writeCz != cz)
                    throw std::runtime_error(
                        "block-mutation Anvil projection received the wrong chunk");

                if (!chunkUpdates.empty()) {
                    auto projected = worldFor(dimension).tryGetCopy(cx, cz);
                    if (!projected)
                        throw std::runtime_error(
                            "block-mutation Anvil projection chunk is unavailable");
                    projectBlockUpdatesToAnvilNbt(
                        *projected, root, cx, cz, chunkUpdates, geometry);
                }

                // /clone copies opaque source NBT losslessly; do not decode it
                // through the set of block entities understood by this build.
                auto projectedEntities = nbt::Value::makeList(nbt::Compound);
                if (!blockEntitiesFor(dimension).writeChunkNbt(
                        cx, cz, projectedEntities, {}, true))
                    throw std::runtime_error(
                        "block-entity projection is contended during block-mutation admission");
                projectBlockEntityNbtReplacements(projectedEntities,
                                                  chunkReplacements);
                root.set("block_entities", std::move(projectedEntities));
            };

        if (!storage->chunkFitsAnvilEntry(cx, cz, writeProjection))
            return false;
    }
    return true;
}

} // namespace cppfm
