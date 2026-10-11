#include "game/BlockEntityNbtCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int assertions = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

cppfm::nbt::Value makeItem(std::string id, std::int32_t count,
                           std::int8_t slot) {
    using namespace cppfm;
    auto item = nbt::Value::makeCompound();
    item.set("id", nbt::Value::makeString(std::move(id)));
    item.set("count", nbt::Value::makeInt(count));
    item.set("Slot", nbt::Value::makeByte(slot));
    return item;
}

cppfm::nbt::Value makeRecord(std::string id, std::int32_t x = 0,
                             std::int32_t y = 64, std::int32_t z = 0) {
    using namespace cppfm;
    auto record = nbt::Value::makeCompound();
    record.set("id", nbt::Value::makeString(std::move(id)));
    record.set("x", nbt::Value::makeInt(x));
    record.set("y", nbt::Value::makeInt(y));
    record.set("z", nbt::Value::makeInt(z));
    return record;
}

cppfm::nbt::Value makeItems(std::initializer_list<cppfm::nbt::Value> items) {
    auto list = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    list.list.insert(list.list.end(), items.begin(), items.end());
    return list;
}

bool throwsRuntimeError(const auto& operation) {
    try {
        operation();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

const cppfm::nbt::Value* listAt(const cppfm::nbt::Value& list,
                                std::size_t index) {
    return list.tag == cppfm::nbt::List && index < list.list.size()
        ? &list.list[index] : nullptr;
}

void testInventorySlotCountsAndValidation() {
    using cppfm::BlockEntityNbtCodec;
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:chest") == 27,
          "chest codec capacity is 27 slots");
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:trapped_chest") == 27,
          "trapped chest uses chest codec capacity");
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:hopper") == 5,
          "hopper codec capacity is five slots");
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:brewing_stand") == 5,
          "brewing stand codec capacity is five slots");
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:crafter") == 9,
          "crafter codec capacity is nine slots");
    check(BlockEntityNbtCodec::inventorySlotCount("minecraft:furnace") == 3,
          "furnace codec capacity is three slots");
    check(BlockEntityNbtCodec::inventorySlotCount("example:unknown") == 0,
          "unknown block entities have no typed inventory capacity");

    auto root = cppfm::nbt::Value::makeCompound();
    auto chest = makeRecord("minecraft:chest");
    chest.set("Items", makeItems({makeItem("minecraft:diamond", 1, 0),
                                    makeItem("minecraft:stone", 2, 26)}));
    auto records = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    records.list.push_back(std::move(chest));
    root.set("block_entities", std::move(records));
    check(!throwsRuntimeError([&] {
              BlockEntityNbtCodec::validateInventoryPayloads(root);
          }),
          "valid in-range inventory slots pass payload validation");

    auto duplicateRoot = cppfm::nbt::Value::makeCompound();
    auto duplicate = makeRecord("minecraft:hopper");
    duplicate.set("Items", makeItems({makeItem("minecraft:diamond", 1, 2),
                                       makeItem("minecraft:stone", 1, 2)}));
    auto duplicateRecords = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    duplicateRecords.list.push_back(std::move(duplicate));
    duplicateRoot.set("TileEntities", std::move(duplicateRecords));
    check(throwsRuntimeError([&] {
              BlockEntityNbtCodec::validateInventoryPayloads(duplicateRoot);
          }),
          "duplicate in-range inventory slots are rejected");

    auto wrongListRoot = cppfm::nbt::Value::makeCompound();
    auto wrongList = makeRecord("minecraft:chest");
    wrongList.set("Items", cppfm::nbt::Value::makeInt(1));
    auto wrongListRecords = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    wrongListRecords.list.push_back(std::move(wrongList));
    wrongListRoot.set("block_entities", std::move(wrongListRecords));
    check(throwsRuntimeError([&] {
              BlockEntityNbtCodec::validateInventoryPayloads(wrongListRoot);
          }),
          "known inventory with a non-list Items field is rejected");
}

void testRecordDecodeAndEncodeAcrossKinds() {
    using namespace cppfm;
    const std::vector<std::pair<std::string, BlockEntity::Kind>> records{
        {"minecraft:chest", BlockEntity::Kind::Chest},
        {"minecraft:trapped_chest", BlockEntity::Kind::Chest},
        {"minecraft:hopper", BlockEntity::Kind::Hopper},
        {"minecraft:dispenser", BlockEntity::Kind::Dispenser},
        {"minecraft:dropper", BlockEntity::Kind::Dropper},
        {"minecraft:barrel", BlockEntity::Kind::Barrel},
        {"minecraft:shulker_box", BlockEntity::Kind::ShulkerBox},
        {"minecraft:brewing_stand", BlockEntity::Kind::Brewing},
        {"minecraft:crafter", BlockEntity::Kind::Crafter},
        {"minecraft:furnace", BlockEntity::Kind::Furnace},
        {"minecraft:smoker", BlockEntity::Kind::Furnace},
        {"minecraft:blast_furnace", BlockEntity::Kind::Furnace},
        {"minecraft:sign", BlockEntity::Kind::Sign},
        {"minecraft:hanging_sign", BlockEntity::Kind::Sign},
    };
    for (const auto& [id, kind] : records) {
        auto input = makeRecord(id, -17, 72, 31);
        const auto decoded = BlockEntityNbtCodec::decodeRecord(input);
        check(decoded && decoded->first == posKey(-17, 72, 31) &&
                  decoded->second && decoded->second->kind == kind,
              "known block-entity ID decodes to the expected value kind");
        if (!decoded || !decoded->second) continue;

        auto encoded = nbt::Value::makeCompound();
        check(BlockEntityNbtCodec::encodeRecord(
                  encoded, *decoded->second, -17, 72, 31),
              "durable block-entity value encodes to one record");
        const auto* encodedId = encoded.get("id");
        const std::string expectedFallback =
            id == "minecraft:trapped_chest" ? "minecraft:chest"
            : id == "minecraft:smoker" || id == "minecraft:blast_furnace"
                ? "minecraft:furnace"
            : id == "minecraft:hanging_sign" ? "minecraft:sign" : id;
        check(encodedId && encodedId->tag == nbt::String &&
                  encodedId->str == expectedFallback,
              "encoding without a template selects the kind's fallback ID");
        if (expectedFallback != id) {
            auto aliasTemplate = input;
            check(BlockEntityNbtCodec::encodeRecord(
                      aliasTemplate, *decoded->second, -17, 72, 31) &&
                      aliasTemplate.get("id") &&
                      aliasTemplate.get("id")->str == id,
                  "encoding with the original template preserves a supported ID alias");
        }
        const auto roundTrip = BlockEntityNbtCodec::decodeRecord(encoded);
        check(roundTrip && roundTrip->first == posKey(-17, 72, 31) &&
                  roundTrip->second && roundTrip->second->kind == kind,
              "record encoding round-trips the position and value kind");
    }

    auto unknown = makeRecord("example:modded_block_entity");
    check(!BlockEntityNbtCodec::decodeRecord(unknown),
          "unknown block-entity type stays opaque to the typed codec");
    auto invalidPosition = makeRecord("minecraft:chest", 1 << 25, 64, 0);
    check(!BlockEntityNbtCodec::decodeRecord(invalidPosition),
          "record coordinates outside packed-position range are not decoded");

    BlockEntity moving;
    moving.kind = BlockEntity::Kind::MovingPiston;
    auto transient = nbt::Value::makeCompound();
    check(!BlockEntityNbtCodec::encodeRecord(transient, moving, 0, 64, 0),
          "moving pistons are omitted from durable record encoding");

    BlockEntity invalid;
    invalid.kind = static_cast<BlockEntity::Kind>(255);
    auto unchanged = makeRecord("minecraft:chest", 3, 70, -4);
    const bool invalidKindRejected = !BlockEntityNbtCodec::encodeRecord(
        unchanged, invalid, 0, 64, 0);
    const auto* unchangedId = unchanged.get("id");
    const auto* unchangedX = unchanged.get("x");
    check(invalidKindRejected && unchangedId &&
              unchangedId->tag == nbt::String &&
              unchangedId->str == "minecraft:chest" && unchangedX &&
              unchangedX->tag == nbt::Int && unchangedX->i == 3 &&
              !unchanged.get("keepPacked"),
          "unknown block-entity kind is rejected without mutating its template");
}

void testItemPayloadAndInventoryTemplateMerge() {
    using namespace cppfm;
    auto source = makeRecord("minecraft:trapped_chest", -1, 63, 2);
    source.set("modded_record_field", nbt::Value::makeString("keep-me"));
    auto item = makeItem("minecraft:diamond", 3, 0);
    source.set("Items", makeItems({std::move(item)}));

    const auto decoded = BlockEntityNbtCodec::decodeRecord(source);
    check(decoded && decoded->second &&
              decoded->second->chest.slots[0].count == 3,
          "known inventory item decodes into its block-entity slot");
    if (decoded && decoded->second) {
        auto editedTemplate = source;
        decoded->second->chest.slots[0].count = 4;
        check(BlockEntityNbtCodec::encodeRecord(
                  editedTemplate, *decoded->second, -1, 63, 2),
              "edited chest inventory re-encodes");
        const auto* preservedField = editedTemplate.get("modded_record_field");
        const auto* preservedId = editedTemplate.get("id");
        const auto* items = editedTemplate.get("Items");
        const auto* encodedItem = items ? listAt(*items, 0) : nullptr;
        const auto* encodedCount = encodedItem
            ? encodedItem->get("count") : nullptr;
        check(preservedField && preservedField->str == "keep-me" &&
                  preservedId && preservedId->str == "minecraft:trapped_chest",
              "record template preserves unknown fields and trapped-chest ID");
        check(items && encodedItem && encodedCount && encodedCount->i == 4,
              "edited stack count is serialized into its inventory record");
    }

    auto templateRecord = makeRecord("minecraft:chest");
    templateRecord.set("Items", makeItems({
        makeItem("minecraft:diamond", 1, 0),
        makeItem("example:modded_item", 2, 1),
        makeItem("minecraft:stone", 1, 1),
        makeItem("minecraft:stone", 1, 40),
    }));
    BlockEntity emptyChest;
    auto* originalItems = templateRecord.get("Items");
    originalItems->list.push_back(nbt::Value::makeCompound());
    check(BlockEntityNbtCodec::encodeRecord(
              templateRecord, emptyChest, 0, 64, 0),
          "empty typed chest can merge against its raw template");
    const auto* mergedItems = templateRecord.get("Items");
    check(mergedItems && mergedItems->list.size() == 4,
          "unknown-slot data and malformed duplicate entries survive encoding");
    const auto* retainedModItem = listAt(*mergedItems, 0);
    const auto* retainedDuplicate = listAt(*mergedItems, 1);
    const auto* retainedOutOfRange = listAt(*mergedItems, 2);
    const auto* retainedMalformed = listAt(*mergedItems, 3);
    const auto* modId = retainedModItem ? retainedModItem->get("id") : nullptr;
    const auto* duplicateSlot = retainedDuplicate
        ? retainedDuplicate->get("Slot") : nullptr;
    const auto* outOfRangeSlot = retainedOutOfRange
        ? retainedOutOfRange->get("Slot") : nullptr;
    check(modId && modId->str == "example:modded_item" &&
              duplicateSlot && duplicateSlot->b == 1 && outOfRangeSlot &&
              outOfRangeSlot->b == 40 && retainedMalformed &&
              retainedMalformed->tag == nbt::Compound,
          "unknown item, duplicate slot, out-of-range slot, and malformed record remain opaque");
}

void testFurnaceCrafterAndSignPayloads() {
    using namespace cppfm;

    auto furnaceRecord = makeRecord("minecraft:smoker");
    furnaceRecord.set("BurnTime", nbt::Value::makeShort(19));
    furnaceRecord.set("CookTime", nbt::Value::makeShort(7));
    furnaceRecord.set("CookTimeTotal", nbt::Value::makeShort(100));
    const auto furnace = BlockEntityNbtCodec::decodeRecord(furnaceRecord);
    check(furnace && furnace->second &&
              furnace->second->furnace.burnTicks == 19 &&
              furnace->second->furnace.cookProgress == 7 &&
              furnace->second->furnace.cookTotal == 100,
          "furnace timing fields decode from a compatible furnace alias");
    if (furnace && furnace->second) {
        auto encoded = furnaceRecord;
        furnace->second->furnace.cookProgress = 8;
        BlockEntityNbtCodec::encodeRecord(encoded, *furnace->second, 0, 64, 0);
        const auto* cook = encoded.get("CookTime");
        check(cook && cook->tag == nbt::Short && cook->s == 8,
              "furnace timing changes are encoded in their original NBT types");
    }

    auto malformedBurnTime = makeRecord("minecraft:furnace");
    malformedBurnTime.set("BurnTime", nbt::Value::makeInt(19));
    check(!BlockEntityNbtCodec::decodeRecord(malformedBurnTime),
          "furnace BurnTime with a non-short NBT tag stays opaque");
    auto malformedCookTime = makeRecord("minecraft:furnace");
    malformedCookTime.set("CookTime", nbt::Value::makeByte(7));
    check(!BlockEntityNbtCodec::decodeRecord(malformedCookTime),
          "furnace CookTime with a non-short NBT tag stays opaque");
    auto malformedTotal = makeRecord("minecraft:furnace");
    malformedTotal.set("CookTimeTotal", nbt::Value::makeInt(100));
    check(!BlockEntityNbtCodec::decodeRecord(malformedTotal),
          "furnace CookTimeTotal with a non-short NBT tag stays opaque");
    auto malformedBrewTime = makeRecord("minecraft:brewing_stand");
    malformedBrewTime.set("BrewTime", nbt::Value::makeInt(19));
    check(!BlockEntityNbtCodec::decodeRecord(malformedBrewTime),
          "brewing-stand BrewTime with a non-short NBT tag stays opaque");
    auto malformedFuel = makeRecord("minecraft:brewing_stand");
    malformedFuel.set("Fuel", nbt::Value::makeShort(19));
    check(!BlockEntityNbtCodec::decodeRecord(malformedFuel),
          "brewing-stand Fuel with a non-byte NBT tag stays opaque");

    auto crafterRecord = makeRecord("minecraft:crafter");
    auto disabled = nbt::Value{};
    disabled.tag = nbt::IntArray;
    disabled.intArray = {0, 8};
    crafterRecord.set("disabled_slots", std::move(disabled));
    crafterRecord.set("crafting_ticks_remaining", nbt::Value::makeInt(4));
    crafterRecord.set("triggered", nbt::Value::makeByte(1));
    const auto crafter = BlockEntityNbtCodec::decodeRecord(crafterRecord);
    check(crafter && crafter->second &&
              crafter->second->crafter.isSlotDisabled(0) &&
              crafter->second->crafter.isSlotDisabled(8) &&
              crafter->second->crafter.craftingTicksRemaining == 4 &&
              crafter->second->crafter.triggered,
          "crafter state decodes independently of inventory storage");
    if (crafter && crafter->second) {
        auto encoded = nbt::Value::makeCompound();
        BlockEntityNbtCodec::encodeRecord(encoded, *crafter->second, 0, 64, 0);
        const auto* encodedDisabled = encoded.get("disabled_slots");
        check(encodedDisabled && encodedDisabled->tag == nbt::IntArray &&
                  encodedDisabled->intArray.size() == 2,
              "crafter disabled slots encode as a vanilla integer array");
    }

    auto signRecord = makeRecord("minecraft:sign");
    auto front = nbt::Value::makeCompound();
    auto signMessages = nbt::Value::makeList(nbt::String);
    auto filteredMessages = nbt::Value::makeList(nbt::String);
    for (int line = 0; line < 4; ++line) {
        signMessages.list.push_back(
            nbt::Value::makeString("line" + std::to_string(line)));
        filteredMessages.list.push_back(
            nbt::Value::makeString("filtered" + std::to_string(line)));
    }
    front.set("messages", std::move(signMessages));
    front.set("filtered_messages", std::move(filteredMessages));
    front.set("color", nbt::Value::makeString("blue"));
    signRecord.set("front_text", std::move(front));
    const auto sign = BlockEntityNbtCodec::decodeRecord(signRecord);
    check(sign && sign->second && sign->second->sign.hasFront,
          "sign front-side presence is decoded from its messages compound");
    if (sign && sign->second) {
        sign->second->sign.front[0] = "edited";
        auto encoded = signRecord;
        BlockEntityNbtCodec::encodeRecord(encoded, *sign->second, 0, 64, 0);
        const auto* encodedFront = encoded.get("front_text");
        const auto* filtered = encodedFront
            ? encodedFront->get("filtered_messages") : nullptr;
        const auto* color = encodedFront ? encodedFront->get("color") : nullptr;
        const auto* messages = encodedFront
            ? encodedFront->get("messages") : nullptr;
        check(filtered && filtered->tag == nbt::List &&
                  filtered->list.size() == 4 &&
                  filtered->list[0].str == "edited" &&
                  filtered->list[1].str == "line1" && color &&
                  color->str == "blue" && messages &&
                  messages->list.size() == 4 &&
                  messages->list[0].str == "edited",
              "sign edits refresh stale filtered text while preserving side metadata");

        auto filteredOnlyEdit = encoded;
        auto* editedFront = filteredOnlyEdit.get("front_text");
        auto* editedFiltered = editedFront
            ? editedFront->get("filtered_messages") : nullptr;
        if (editedFiltered && editedFiltered->tag == nbt::List &&
            !editedFiltered->list.empty())
            editedFiltered->list[0] = nbt::Value::makeString("censored");
        const auto unedited = BlockEntityNbtCodec::decodeRecord(filteredOnlyEdit);
        auto preservedFilter = filteredOnlyEdit;
        if (unedited && unedited->second)
            BlockEntityNbtCodec::encodeRecord(
                preservedFilter, *unedited->second, 0, 64, 0);
        const auto* preservedFront = preservedFilter.get("front_text");
        const auto* preservedFiltered = preservedFront
            ? preservedFront->get("filtered_messages") : nullptr;
        check(preservedFiltered && preservedFiltered->tag == nbt::List &&
                  !preservedFiltered->list.empty() &&
                  preservedFiltered->list[0].str == "censored",
              "saving an unedited sign preserves distinct server-filtered text");
    }
}

} // namespace

int main() {
    testInventorySlotCountsAndValidation();
    testRecordDecodeAndEncodeAcrossKinds();
    testItemPayloadAndInventoryTemplateMerge();
    testFurnaceCrafterAndSignPayloads();
    std::printf("block_entity_nbt_codec: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
