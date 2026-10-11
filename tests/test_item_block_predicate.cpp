#include "game/AdventureBlockBreak.hpp"
#include "game/BlockEntities.hpp"

#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

cppfm::nbt::Value blockPredicate(
    std::string_view block,
    std::initializer_list<std::pair<std::string_view, std::string_view>> props = {}) {
    auto result = cppfm::nbt::Value::makeCompound();
    result.set("blocks", cppfm::nbt::Value::makeString(std::string(block)));
    if (!props.size()) return result;
    auto state = cppfm::nbt::Value::makeCompound();
    for (const auto& [name, value] : props)
        state.set(std::string(name), cppfm::nbt::Value::makeString(
            std::string(value)));
    result.set("state", std::move(state));
    return result;
}

cppfm::nbt::Value componentValue(cppfm::nbt::Value predicate,
                                 bool showTooltip = true) {
    auto predicates = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    predicates.list.push_back(std::move(predicate));
    auto value = cppfm::nbt::Value::makeCompound();
    value.set("predicates", std::move(predicates));
    value.set("show_in_tooltip", cppfm::nbt::Value::makeByte(
        showTooltip ? 1 : 0));
    return value;
}

std::uint16_t blockState(
    std::string_view block,
    const std::vector<std::pair<std::string_view, std::string_view>>& props = {}) {
    const auto* definition = cppfm::gen::blockByName(block);
    if (!definition) return 0;
    return static_cast<std::uint16_t>(cppfm::gen::stateWithProps(
        *definition, props));
}

cppfm::ItemStack stackWithComponent(std::string_view name,
                                    const cppfm::nbt::Value& saved) {
    auto component = cppfm::item_component_nbt::protocolComponentFromNbt(
        name, saved);
    if (!component) return cppfm::ItemStack::air();
    auto stack = cppfm::ItemStack::ofName("minecraft:diamond_pickaxe");
    stack.components.emplace_back(component->id, std::move(component->payload));
    return stack;
}

void testCanBreakComponentWireAndPersistence() {
    const auto saved = componentValue(blockPredicate("minecraft:dirt"), false);
    const auto encoded = cppfm::item_component_nbt::protocolComponentFromNbt(
        "minecraft:can_break", saved);
    check(encoded && encoded->id == cppfm::ItemStack::kCanBreakComponentId,
          "can_break uses the protocol-769 component ID 12");
    check(encoded && cppfm::item_component_nbt::isValidProtocolComponentPayload(
                         encoded->id, encoded->payload),
          "can_break payload matches the 1.21.4 component codec");

    auto stack = cppfm::ItemStack::ofName("minecraft:diamond_pickaxe");
    if (encoded)
        stack.components.emplace_back(encoded->id, encoded->payload);
    cppfm::WriteBuffer wire;
    stack.write(wire);
    cppfm::ReadBuffer input(wire.data);
    const auto decodedStack = cppfm::ItemStack::read(input);
    check(input.remaining() == 0 && decodedStack.sameItemData(stack),
          "can_break component survives item-slot protocol round trip");

    const auto persisted = stack.toNbt(-1);
    const auto restored = cppfm::ItemStack::fromNbt(persisted);
    check(restored && restored->sameItemData(stack),
          "can_break component survives saved-item NBT round trip");
}

void testAdventureBreakBlockAndProperties() {
    cppfm::TagManager tags;
    auto exactBlock = stackWithComponent(
        "minecraft:can_break",
        componentValue(blockPredicate("minecraft:dirt")));
    check(cppfm::itemCanBreakBlockInAdventure(
              exactBlock, blockState("minecraft:dirt"), tags),
          "Adventure can_break permits the listed dirt block");
    check(!cppfm::itemCanBreakBlockInAdventure(
              exactBlock, blockState("minecraft:stone"), tags),
          "Adventure can_break rejects an unlisted block");
    check(!cppfm::canPlayerBreakBlockInAdventure(
              3, exactBlock, blockState("minecraft:dirt"), tags),
          "spectator mode cannot break even when the held item allows it");

    auto axisPredicate = blockPredicate("minecraft:oak_log", {
        {"axis", "x"}});
    auto axisTool = stackWithComponent("minecraft:can_break",
                                      componentValue(std::move(axisPredicate)));
    check(cppfm::itemCanBreakBlockInAdventure(axisTool,
              blockState("minecraft:oak_log", {{"axis", "x"}}), tags),
          "Adventure can_break applies exact block-state property predicates");
    check(!cppfm::itemCanBreakBlockInAdventure(axisTool,
              blockState("minecraft:oak_log", {{"axis", "y"}}), tags),
          "Adventure can_break rejects a nonmatching block-state property");

    auto multiPropertyNonmatch = blockPredicate("minecraft:oak_slab", {
        {"type", "top"}, {"waterlogged", "false"}});
    auto multiPropertyMatch = blockPredicate("minecraft:oak_slab", {
        {"type", "bottom"}, {"waterlogged", "false"}});
    auto predicates = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    predicates.list.push_back(std::move(multiPropertyNonmatch));
    predicates.list.push_back(std::move(multiPropertyMatch));
    auto multiplePredicateValue = cppfm::nbt::Value::makeCompound();
    multiplePredicateValue.set("predicates", std::move(predicates));
    multiplePredicateValue.set("show_in_tooltip",
        cppfm::nbt::Value::makeByte(1));
    auto multiplePredicateTool = stackWithComponent(
        "minecraft:can_break", multiplePredicateValue);
    check(cppfm::itemCanBreakBlockInAdventure(
              multiplePredicateTool,
              blockState("minecraft:oak_slab", {
                  {"type", "bottom"}, {"waterlogged", "false"}}), tags),
          "a nonmatching multi-property predicate does not desynchronize a later OR predicate");
}

void testAdventureBlockTagsAndFailClosedNbt() {
    cppfm::TagManager tags;
    const auto* stone = cppfm::gen::blockByName("minecraft:stone");
    if (stone)
        tags.blockTags["minecraft:mineable/pickaxe"].insert(
            stone->defaultState);
    auto tagTool = stackWithComponent("minecraft:can_break",
        componentValue(blockPredicate("#mineable/pickaxe")));
    check(cppfm::itemCanBreakBlockInAdventure(
              tagTool, blockState("minecraft:stone"), tags),
          "Adventure can_break resolves a named block tag");

    auto nbtPredicate = blockPredicate("minecraft:chest");
    nbtPredicate.set("nbt", cppfm::nbt::Value::makeCompound());
    auto nbtTool = stackWithComponent("minecraft:can_break",
        componentValue(std::move(nbtPredicate)));
    check(!cppfm::itemCanBreakBlockInAdventure(
              nbtTool, blockState("minecraft:chest"), tags),
          "block-entity-NBT predicates fail closed without an NBT snapshot");

    auto entityPattern = cppfm::nbt::Value::makeCompound();
    entityPattern.set("id", cppfm::nbt::Value::makeString("minecraft:chest"));
    entityPattern.set("Lock", cppfm::nbt::Value::makeString("vault"));
    auto identifiedPredicate = blockPredicate("minecraft:chest");
    identifiedPredicate.set("nbt", std::move(entityPattern));
    auto identifiedTool = stackWithComponent("minecraft:can_break",
        componentValue(std::move(identifiedPredicate)));

    auto root = cppfm::nbt::Value::makeCompound();
    auto blockEntities = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto chest = cppfm::nbt::Value::makeCompound();
    chest.set("id", cppfm::nbt::Value::makeString("minecraft:chest"));
    chest.set("x", cppfm::nbt::Value::makeInt(0));
    chest.set("y", cppfm::nbt::Value::makeInt(64));
    chest.set("z", cppfm::nbt::Value::makeInt(0));
    chest.set("Lock", cppfm::nbt::Value::makeString("vault"));
    chest.set("Items", cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    blockEntities.list.push_back(std::move(chest));
    root.set("block_entities", std::move(blockEntities));

    cppfm::BlockEntityStore store;
    store.readChunkNbt(root);
    const auto actualChest = store.snapshotNbtAt(0, 64, 0);
    const auto mismatchedChest = store.snapshotNbtAt(
        0, 64, 0, cppfm::BlockEntity::Kind::Barrel);
    const auto matchingChest = store.snapshotNbtAt(
        0, 64, 0, cppfm::BlockEntity::Kind::Chest);
    const auto provider = [&]() -> const cppfm::nbt::Value* {
        return actualChest ? &*actualChest : nullptr;
    };
    check(actualChest && matchingChest && !mismatchedChest &&
              !actualChest->get("keepPacked") &&
              cppfm::itemCanBreakBlockInAdventure(
                  identifiedTool, blockState("minecraft:chest"), tags,
                  provider),
          "Adventure can_break matches an NBT subset from the live block-entity snapshot");

    auto opaqueRoot = cppfm::nbt::Value::makeCompound();
    auto opaqueEntities = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto opaqueChest = cppfm::nbt::Value::makeCompound();
    opaqueChest.set("id", cppfm::nbt::Value::makeString("mod:unknown_chest"));
    opaqueChest.set("x", cppfm::nbt::Value::makeInt(0));
    opaqueChest.set("y", cppfm::nbt::Value::makeInt(64));
    opaqueChest.set("z", cppfm::nbt::Value::makeInt(0));
    opaqueChest.set("Lock", cppfm::nbt::Value::makeString("vault"));
    opaqueEntities.list.push_back(std::move(opaqueChest));
    opaqueRoot.set("block_entities", std::move(opaqueEntities));
    cppfm::BlockEntityStore opaqueStore;
    opaqueStore.readChunkNbt(opaqueRoot);
    const auto opaqueSnapshot = opaqueStore.snapshotNbtAt(0, 64, 0);
    const auto opaqueProvider = [&]() -> const cppfm::nbt::Value* {
        return opaqueSnapshot ? &*opaqueSnapshot : nullptr;
    };
    check(!opaqueSnapshot && !cppfm::itemCanBreakBlockInAdventure(
              identifiedTool, blockState("minecraft:chest"), tags,
              opaqueProvider),
          "Adventure can_break does not authorize against an opaque, non-live block-entity record");

    auto wrongLock = cppfm::nbt::Value::makeCompound();
    wrongLock.set("Lock", cppfm::nbt::Value::makeString("other"));
    auto mismatchPredicate = blockPredicate("minecraft:chest");
    mismatchPredicate.set("nbt", std::move(wrongLock));
    auto mismatchTool = stackWithComponent("minecraft:can_break",
        componentValue(std::move(mismatchPredicate)));
    check(!cppfm::itemCanBreakBlockInAdventure(
              mismatchTool, blockState("minecraft:chest"), tags, provider),
          "Adventure can_break rejects a nonmatching block-entity NBT subset");

    std::size_t snapshotCalls = 0;
    auto alternatives = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto nonmatch = blockPredicate("minecraft:chest");
    auto wrongAlternativeNbt = cppfm::nbt::Value::makeCompound();
    wrongAlternativeNbt.set("Lock",
        cppfm::nbt::Value::makeString("other"));
    nonmatch.set("nbt", std::move(wrongAlternativeNbt));
    auto match = blockPredicate("minecraft:chest");
    auto matchingNbt = cppfm::nbt::Value::makeCompound();
    matchingNbt.set("Lock", cppfm::nbt::Value::makeString("vault"));
    match.set("nbt", std::move(matchingNbt));
    alternatives.list.push_back(std::move(nonmatch));
    alternatives.list.push_back(std::move(match));
    auto alternativeValue = cppfm::nbt::Value::makeCompound();
    alternativeValue.set("predicates", std::move(alternatives));
    alternativeValue.set("show_in_tooltip", cppfm::nbt::Value::makeByte(1));
    const auto alternativeTool = stackWithComponent(
        "minecraft:can_break", alternativeValue);
    const auto countingProvider = [&]() -> const cppfm::nbt::Value* {
        ++snapshotCalls;
        return provider();
    };
    check(cppfm::itemCanBreakBlockInAdventure(
              alternativeTool, blockState("minecraft:chest"), tags,
              countingProvider) && snapshotCalls == 1,
          "NBT snapshots are lazy and memoized across OR predicates");

    auto expectedNested = cppfm::nbt::Value::makeCompound();
    auto expectedNestedValue = cppfm::nbt::Value::makeCompound();
    auto expectedList = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto expectedListEntry = cppfm::nbt::Value::makeCompound();
    expectedListEntry.set("value", cppfm::nbt::Value::makeInt(7));
    expectedList.list.push_back(std::move(expectedListEntry));
    expectedNestedValue.set("entries", std::move(expectedList));
    expectedNested.set("data", std::move(expectedNestedValue));
    auto actualNested = cppfm::nbt::Value::makeCompound();
    auto actualNestedValue = cppfm::nbt::Value::makeCompound();
    actualNestedValue.set("extra", cppfm::nbt::Value::makeString("ignored"));
    auto actualList = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto actualListExtra = cppfm::nbt::Value::makeCompound();
    actualListExtra.set("value", cppfm::nbt::Value::makeInt(8));
    actualList.list.push_back(std::move(actualListExtra));
    auto actualListEntry = cppfm::nbt::Value::makeCompound();
    actualListEntry.set("value", cppfm::nbt::Value::makeInt(7));
    actualList.list.push_back(std::move(actualListEntry));
    actualNestedValue.set("entries", std::move(actualList));
    actualNested.set("data", std::move(actualNestedValue));
    check(cppfm::nbt::matchesSubset(expectedNested, actualNested, true) &&
              !cppfm::nbt::matchesSubset(expectedNested, actualNested, false),
          "NBT subset matching recurses through compounds and ignores list order when requested");

    auto expectedExactList = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto expectedExactEntry = cppfm::nbt::Value::makeCompound();
    expectedExactEntry.set("value", cppfm::nbt::Value::makeInt(7));
    expectedExactList.list.push_back(expectedExactEntry);
    auto actualExactList = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto actualExactEntry = cppfm::nbt::Value::makeCompound();
    actualExactEntry.set("value", cppfm::nbt::Value::makeInt(7));
    actualExactEntry.set("extra", cppfm::nbt::Value::makeByte(1));
    actualExactList.list.push_back(std::move(actualExactEntry));
    check(cppfm::nbt::matchesSubset(expectedExactList, expectedExactList, false) &&
              cppfm::nbt::matchesSubset(expectedExactList, actualExactList, true) &&
              !cppfm::nbt::matchesSubset(expectedExactList, actualExactList, false),
          "ordered NBT list matching uses exact element equality while subset mode remains recursive");

    const auto emptyCompoundList =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    const auto emptyByteList = cppfm::nbt::Value::makeList(cppfm::nbt::Byte);
    check(!cppfm::nbt::matchesSubset(
              emptyCompoundList, emptyByteList, false),
          "exact NBT list matching compares element types even for empty lists");

    auto noComponent = cppfm::ItemStack::ofName("minecraft:diamond_pickaxe");
    check(!cppfm::itemCanBreakBlockInAdventure(
              noComponent, blockState("minecraft:dirt"), tags),
          "Adventure without can_break cannot break a block");
}

void testCanPlaceOnComponentDecode() {
    const auto saved = componentValue(blockPredicate("minecraft:stone"));
    const auto encoded = cppfm::item_component_nbt::protocolComponentFromNbt(
        "minecraft:can_place_on", saved);
    check(encoded && encoded->id == cppfm::ItemStack::kCanPlaceOnComponentId &&
          cppfm::item_component_nbt::isValidProtocolComponentPayload(
              encoded->id, encoded->payload),
          "can_place_on is accepted by the matching protocol-769 component codec");
}

} // namespace

int main() {
    testCanBreakComponentWireAndPersistence();
    testAdventureBreakBlockAndProperties();
    testAdventureBlockTagsAndFailClosedNbt();
    testCanPlaceOnComponentDecode();
    if (failures != 0) {
        std::fprintf(stderr, "%d/%d assertions failed\n", failures, checks);
        return 1;
    }
    std::printf("PASS: %d item block-predicate assertions\n", checks);
    return 0;
}
