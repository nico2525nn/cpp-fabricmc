#include "../src/brigadier/Arguments.hpp"
#include "../src/jvm/JavaObjectCache.hpp"
#include "../src/jvm/NativeHandleTable.hpp"
#include "../src/worldgen/StructurePlacer.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

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
        testStructureOrigins();
        testJvmTables();
        std::cout << "goal-cleanup-runtime: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "goal-cleanup-runtime: FAIL: " << error.what() << '\n';
        return 1;
    }
}
