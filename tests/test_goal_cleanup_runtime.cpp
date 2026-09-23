#include "../src/brigadier/Arguments.hpp"
#include "../src/jvm/JavaObjectCache.hpp"
#include "../src/jvm/NativeHandleTable.hpp"
#include "../src/worldgen/StructurePlacer.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testNestedArguments() {
    cppfm::brigadier::ParseCtx context;
    auto item = cppfm::brigadier::args::itemPredicateArg();
    cppfm::brigadier::StringReader itemReader(
        "minecraft:stone[foo{bar:[1,2]}]{tag:{nested:true}}");
    require(item.parse(itemReader, context).asStr() == "minecraft:stone" &&
                itemReader.remainingLength() == 0,
            "item predicate did not consume nested data");

    auto nbt = cppfm::brigadier::args::nbtArg();
    cppfm::brigadier::StringReader nbtReader(" {outer:{inner:[1,2,{x:3}]}}");
    require(nbt.parse(nbtReader, context).asStr() ==
                "{outer:{inner:[1,2,{x:3}]}}",
            "NBT parser lost nested data");

    bool rejected = false;
    try {
        cppfm::brigadier::StringReader malformed("{outer:{inner:1}");
        (void)nbt.parse(malformed, context);
    } catch (const cppfm::brigadier::StringReader::ParseError&) {
        rejected = true;
    }
    require(rejected, "unterminated NBT was accepted");
}

void testResourceLocations() {
    using namespace cppfm::brigadier;
    ParseCtx context;
    const auto argument = args::resourceLocation();
    const auto parse = [&](const std::string& text) {
        StringReader reader(text);
        const auto value = argument.parse(reader, context).asStr();
        require(reader.remainingLength() == 0,
                "resource location parser left trailing identifier characters");
        return value;
    };
    require(parse("stone") == "minecraft:stone",
            "resource location defaults an omitted namespace");
    require(parse("mod_id.v2:path/segment-name_0") ==
                "mod_id.v2:path/segment-name_0",
            "resource location accepts the vanilla namespace/path character sets");
    // Identifier validation is character-based: path separators and dots are
    // legal identifier characters. Filesystem path safety is enforced by the
    // file resolver, not by changing command-parser identifier semantics.
    require(parse("mod:../assets//entry") == "mod:../assets//entry",
            "resource location does not impose filesystem segment rules");

    for (const std::string invalid : {"Mod:path", "mod:Upper", "mod:bad+path",
                                      "bad\\namespace:path", "mod:bad\\path",
                                      "mod:", ":path", "mod:path:extra"}) {
        bool rejected = false;
        try {
            StringReader reader(invalid);
            (void)argument.parse(reader, context);
            rejected = reader.remainingLength() != 0;
        } catch (const StringReader::ParseError&) {
            rejected = true;
        }
        require(rejected, "invalid resource location was accepted");
    }

    const std::vector<std::pair<ArgumentType, std::string>> specialized{
        {args::itemStackArg(), "minecraft:Stone"},
        {args::blockStateArg(), "minecraft:Stone"},
        {args::blockPredicateArg(), "#minecraft:Stone"},
        {args::itemPredicateArg(), "#minecraft:Stone"},
        {args::dimensionArg(), "minecraft:Overworld"},
        {args::lootTableArg(), "minecraft:Stone"},
    };
    for (const auto& [argumentType, text] : specialized) {
        bool rejected = false;
        try {
            StringReader reader(text);
            (void)argumentType.parse(reader, context);
        } catch (const StringReader::ParseError&) {
            rejected = true;
        }
        require(rejected,
                "specialized identifier argument accepted a non-vanilla identifier");
    }
}

void testStructureOrigins() {
    cppfm::worldgen::StructurePlacer placer(0x12345678ULL);
    cppfm::worldgen::PlacedFeature feature;
    feature.spacing = 32;
    feature.separation = 8;
    feature.salt = 0x55AAULL;
    feature.frequency = 1.0;

    bool checked = false;
    for (std::int32_t cx = 0; cx < 320 && !checked; ++cx) {
        for (std::int32_t cz = 0; cz < 320 && !checked; ++cz) {
            if (!placer.shouldPlaceAt(feature, cx, cz)) continue;
            std::int32_t originX = 0;
            std::int32_t originZ = 0;
            require(placer.findOrigin(feature, cx, cz, originX, originZ),
                    "placed origin was not discoverable");
            require(originX == cx && originZ == cz,
                    "origin lookup disagreed with placement check");
            checked = true;
        }
    }
    require(checked, "no deterministic placement candidate found");
}

void testJvmTables() {
    cppfm::jvm::NativeHandleTable handles;
    int object = 0;
    const auto handle = handles.registerObject(&object, cppfm::jvm::HandleKind::Entity);
    require(handle != 0 && handles.valid(handle, cppfm::jvm::HandleKind::Entity),
            "native handle was not registered");
    require(handles.invalidateHandle(handle) && !handles.valid(handle),
            "native handle invalidation failed");

    cppfm::jvm::JavaObjectCache cache;
    cache.put(handle, &object);
    cache.put(nullptr, handle + 1, "Example", &object);
    require(cache.size() == 2, "JVM object cache insertion failed");
    cache.erase(handle);
    require(cache.size() == 1, "JVM object cache erase lost the wrong entry");
    cache.erase(nullptr, handle + 1);
    require(cache.size() == 0, "JVM object cache did not drain all typed entries");
}

} // namespace

int main() {
    try {
        testNestedArguments();
        testResourceLocations();
        testStructureOrigins();
        testJvmTables();
        std::cout << "goal-cleanup-runtime: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "goal-cleanup-runtime: FAIL: " << error.what() << '\n';
        return 1;
    }
}
