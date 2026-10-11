#include "game/Items.hpp"
#include "game/BundleContents.hpp"
#include "game/DeathProtection.hpp"
#include "game/ItemComponentNbtAdditionalFood.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << description << '\n';
    if (!condition) ++failures;
}

cppfm::nbt::Value itemNbt(std::string id, std::int32_t count);

void checkComponentRoundTrip(std::string itemName, std::string componentName,
                             cppfm::nbt::Value saved,
                             const char* description) {
    using namespace cppfm;
    using namespace cppfm::item_component_nbt;

    const auto encoded = protocolComponentFromNbt(componentName, saved);
    check(encoded && isValidProtocolComponentPayload(
                          encoded->id, encoded->payload), description);
    if (!encoded) return;

    auto item = itemNbt(std::move(itemName), 1);
    if (!applyProtocolComponentToNbt(item, encoded->id, &encoded->payload)) {
        check(false, "additional component decodes into saved item NBT");
        return;
    }
    const auto* components = item.get("components");
    const auto* decoded = components ? components->get(componentName) : nullptr;
    const auto restored = decoded
        ? protocolComponentFromNbt(componentName, *decoded) : std::nullopt;
    check(restored && restored->payload == encoded->payload,
          "additional component round-trips between NBT and protocol");

    auto stack = ItemStack::fromNbt(item);
    if (!stack) {
        check(false, "additional component survives ItemStack persistence decode");
        return;
    }
    WriteBuffer wire;
    stack->write(wire);
    ReadBuffer input(wire.data);
    const auto received = ItemStack::read(input);
    check(input.remaining() == 0 && received.sameItemData(*stack),
          "additional component survives full ItemStack wire encoding");
}

cppfm::nbt::Value itemNbt(std::string id, std::int32_t count = 1) {
    auto item = cppfm::nbt::Value::makeCompound();
    item.set("id", cppfm::nbt::Value::makeString(std::move(id)));
    item.set("count", cppfm::nbt::Value::makeInt(count));
    return item;
}

cppfm::nbt::Value attributeModifier(
    std::string type, std::string id, double amount,
    std::string operation, std::string slot = {}) {
    auto modifier = cppfm::nbt::Value::makeCompound();
    modifier.set("type", cppfm::nbt::Value::makeString(std::move(type)));
    modifier.set("id", cppfm::nbt::Value::makeString(std::move(id)));
    modifier.set("amount", cppfm::nbt::Value::makeDouble(amount));
    modifier.set("operation",
                 cppfm::nbt::Value::makeString(std::move(operation)));
    if (!slot.empty())
        modifier.set("slot", cppfm::nbt::Value::makeString(std::move(slot)));
    return modifier;
}

cppfm::nbt::Value attributeComponent(cppfm::nbt::Value modifier,
                                     bool showTooltip = true) {
    auto modifiers = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    modifiers.list.push_back(std::move(modifier));
    auto component = cppfm::nbt::Value::makeCompound();
    component.set("modifiers", std::move(modifiers));
    component.set("show_in_tooltip",
                  cppfm::nbt::Value::makeByte(showTooltip ? 1 : 0));
    return component;
}

cppfm::nbt::Value bundleComponent(cppfm::nbt::Value item) {
    auto contents = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    contents.list.push_back(std::move(item));
    return contents;
}

void testStatusEffectBoundsInfiniteDurationAndFallbacks() {
    using namespace cppfm;
    namespace food = item_component_nbt::additional_food::detail;

    const auto speedRegistryId = food::registryId(
        food::RegistryKind::StatusEffect, "minecraft:speed");
    check(speedRegistryId && *speedRegistryId > 0,
          "status-effect regression resolves the captured vanilla registry mapping");
    if (!speedRegistryId) return;

    auto effect = nbt::Value::makeCompound();
    effect.set("id", nbt::Value::makeString("minecraft:speed"));
    effect.set("amplifier", nbt::Value::makeInt(kMaxEffectAmplifier));
    effect.set("duration", nbt::Value::makeInt(-1));
    auto hidden = nbt::Value::makeCompound();
    hidden.set("amplifier", nbt::Value::makeInt(1));
    hidden.set("duration", nbt::Value::makeInt(-1));
    effect.set("hidden_effect", std::move(hidden));

    WriteBuffer encodedEffect;
    const bool encodedInfinite = food::writePotionEffect(effect, encodedEffect);
    ReadBuffer effectInput(encodedEffect.data);
    const auto decodedEffect = food::readPotionEffect(effectInput, 0);
    const auto* decodedDuration = decodedEffect
        ? decodedEffect->get("duration") : nullptr;
    const auto* decodedHidden = decodedEffect
        ? decodedEffect->get("hidden_effect") : nullptr;
    const auto* decodedHiddenDuration = decodedHidden
        ? decodedHidden->get("duration") : nullptr;
    check(encodedInfinite && decodedEffect && decodedDuration &&
              decodedDuration->tag == nbt::Int && decodedDuration->i == -1 &&
              decodedEffect->get("amplifier") &&
              decodedEffect->get("amplifier")->i == kMaxEffectAmplifier &&
              decodedHiddenDuration && decodedHiddenDuration->tag == nbt::Int &&
              decodedHiddenDuration->i == -1 && effectInput.remaining() == 0,
          "food status-effect codec round-trips amplifier 255 and infinite durations");

    auto overAmplified = effect;
    overAmplified.set("amplifier",
                      nbt::Value::makeInt(kMaxEffectAmplifier + 1));
    WriteBuffer rejectedEncoding;
    check(!food::writePotionEffect(overAmplified, rejectedEncoding) &&
              rejectedEncoding.data.empty(),
          "food status-effect encoding rejects amplifier 256 before writing");
    auto overAmplifiedHidden = nbt::Value::makeCompound();
    overAmplifiedHidden.set("id",
                            nbt::Value::makeString("minecraft:speed"));
    overAmplifiedHidden.set("amplifier", nbt::Value::makeInt(0));
    overAmplifiedHidden.set("duration", nbt::Value::makeInt(20));
    auto invalidHiddenAmplifier = nbt::Value::makeCompound();
    invalidHiddenAmplifier.set(
        "amplifier", nbt::Value::makeInt(kMaxEffectAmplifier + 1));
    invalidHiddenAmplifier.set("duration", nbt::Value::makeInt(40));
    overAmplifiedHidden.set("hidden_effect",
                            std::move(invalidHiddenAmplifier));
    WriteBuffer rejectedHiddenEncoding;
    check(!food::writePotionEffect(overAmplifiedHidden,
                                   rejectedHiddenEncoding) &&
              rejectedHiddenEncoding.data.empty(),
          "food status-effect encoding rejects hidden amplifier 256");
    auto invalidNbtDuration = effect;
    invalidNbtDuration.set("duration", nbt::Value::makeInt(-2));
    WriteBuffer rejectedDurationEncoding;
    check(!food::writePotionEffect(invalidNbtDuration,
                                   rejectedDurationEncoding) &&
              rejectedDurationEncoding.data.empty(),
          "food status-effect encoding rejects durations below -1 before writing");

    const auto effectWire = [&](const std::int32_t amplifier,
                                const std::int32_t duration) {
        WriteBuffer wire;
        wire.varint(*speedRegistryId);
        wire.varint(amplifier);
        wire.varint(duration);
        wire.boolean(false);
        wire.boolean(true);
        wire.boolean(true);
        wire.boolean(false);
        return wire.data;
    };
    auto overAmplifiedWire = effectWire(kMaxEffectAmplifier + 1, -1);
    ReadBuffer overAmplifiedInput(overAmplifiedWire);
    check(!food::readPotionEffect(overAmplifiedInput, 0),
          "food status-effect decoding rejects amplifier 256");
    WriteBuffer overAmplifiedHiddenWire;
    overAmplifiedHiddenWire.varint(*speedRegistryId);
    overAmplifiedHiddenWire.varint(0);
    overAmplifiedHiddenWire.varint(20);
    overAmplifiedHiddenWire.boolean(false);
    overAmplifiedHiddenWire.boolean(true);
    overAmplifiedHiddenWire.boolean(true);
    overAmplifiedHiddenWire.boolean(true);
    overAmplifiedHiddenWire.varint(kMaxEffectAmplifier + 1);
    overAmplifiedHiddenWire.varint(40);
    overAmplifiedHiddenWire.boolean(false);
    overAmplifiedHiddenWire.boolean(true);
    overAmplifiedHiddenWire.boolean(true);
    overAmplifiedHiddenWire.boolean(false);
    ReadBuffer overAmplifiedHiddenInput(overAmplifiedHiddenWire.data);
    check(!food::readPotionEffect(overAmplifiedHiddenInput, 0),
          "food status-effect decoding rejects hidden amplifier 256");
    auto invalidDurationWire = effectWire(0, -2);
    ReadBuffer invalidDurationInput(invalidDurationWire);
    check(!food::readPotionEffect(invalidDurationInput, 0),
          "food status-effect decoding rejects durations below -1");

    const auto parsedDeathEffect = death_protection::effectFromNbt(effect);
    check(parsedDeathEffect && parsedDeathEffect->infiniteDuration &&
              parsedDeathEffect->durationTicks == -1 &&
              parsedDeathEffect->hiddenEffects.size() == 1 &&
              parsedDeathEffect->hiddenEffects.front().durationTicks == -1,
          "death-protection NBT accepts infinite active and hidden effects");
    auto invalidDeathAmplifier = effect;
    invalidDeathAmplifier.set("amplifier",
                              nbt::Value::makeInt(kMaxEffectAmplifier + 1));
    check(!death_protection::effectFromNbt(invalidDeathAmplifier),
          "death-protection NBT rejects amplifier 256");
    auto invalidDeathDuration = effect;
    invalidDeathDuration.set("duration", nbt::Value::makeInt(-2));
    check(!death_protection::effectFromNbt(invalidDeathDuration),
          "death-protection NBT rejects durations below -1");

    std::vector<EffectInstance> active;
    const EffectInstance infinite(effects::Speed, 0, -1);
    check(applyStatusEffect(active, infinite) && active.size() == 1 &&
              active.front().infiniteDuration && !active.front().expired(),
          "runtime effect application accepts infinite duration");
    tickHiddenEffects(active.front());
    --active.front().durationTicks; // Mirrors the runtime's direct countdown.
    const auto persistent = persistentEntityEffectUpdates(
        {effects::Speed}, active);
    check(!active.front().expired() && persistent.size() == 1 &&
              persistent.front().durationTicks == -1,
          "runtime countdown keeps infinite effects alive and serializes them as -1");

    std::vector<EffectInstance> rejectedRuntime;
    check(!applyStatusEffect(
              rejectedRuntime,
              EffectInstance(effects::Speed, kMaxEffectAmplifier + 1, 20)) &&
              rejectedRuntime.empty(),
          "runtime effect application rejects amplifier 256");
    std::vector<EffectInstance> maximumAmplifierRuntime;
    check(applyStatusEffect(
              maximumAmplifierRuntime,
              EffectInstance(effects::Speed, kMaxEffectAmplifier, 20)) &&
              maximumAmplifierRuntime.size() == 1 &&
              maximumAmplifierRuntime.front().amplifier ==
                  kMaxEffectAmplifier,
          "runtime effect application accepts amplifier 255");
    EffectInstance invalidHidden(effects::Speed, 0, 20);
    invalidHidden.hiddenEffects.push_back(
        {kMaxEffectAmplifier + 1, 40, false, true, true});
    check(!applyStatusEffect(rejectedRuntime, invalidHidden) &&
              rejectedRuntime.empty(),
          "runtime effect application rejects hidden amplifier 256");
    check(!applyStatusEffect(rejectedRuntime,
                             EffectInstance(effects::Speed, 0, -2)) &&
              rejectedRuntime.empty(),
          "runtime effect application rejects durations below -1");

    EffectInstance finiteZero(effects::Speed, 0, 0);
    std::vector<EffectInstance> zeroDurationEffects;
    applyStatusEffect(zeroDurationEffects, finiteZero);
    tickHiddenEffects(zeroDurationEffects.front());
    --zeroDurationEffects.front().durationTicks;
    check(zeroDurationEffects.front().expired(),
          "finite zero duration does not become infinite after runtime countdown");

    EffectInstance previous(effects::Speed, 2, 100);
    previous.hiddenEffects.push_back({1, 600, false, true, true});
    std::vector<EffectInstance> fallbackEffects{previous};
    check(applyStatusEffect(fallbackEffects,
                            EffectInstance(effects::Speed, 3, 250)) &&
              fallbackEffects.front().amplifier == 3 &&
              fallbackEffects.front().durationTicks == 250 &&
              fallbackEffects.front().hiddenEffects.size() == 1 &&
              fallbackEffects.front().hiddenEffects.front().amplifier == 1 &&
              fallbackEffects.front().hiddenEffects.front().durationTicks == 600,
          "stronger longer effect preserves an old hidden fallback that outlasts it");
}

void testAttributeModifierWireAndPersistence() {
    using namespace cppfm;
    using namespace cppfm::item_component_nbt;

    const auto saved = attributeComponent(attributeModifier(
        "minecraft:attack_damage", "example:bonus", 2.0, "add_value",
        "mainhand"));
    const auto encoded = protocolComponentFromNbt(
        "minecraft:attribute_modifiers", saved);
    const std::vector<std::uint8_t> golden{
        0x01, 0x02, 0x0d, 'e', 'x', 'a', 'm', 'p', 'l', 'e', ':', 'b',
        'o', 'n', 'u', 's', 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x01};
    check(encoded && encoded->id == 13 && encoded->payload == golden,
          "attribute_modifiers matches the official protocol field order and payload bytes");
    check(encoded && isValidProtocolComponentPayload(13, encoded->payload),
          "attribute_modifiers payload passes structural validation");

    if (encoded) {
        nbt::Value item = itemNbt("minecraft:diamond_sword");
        const bool applied = applyProtocolComponentToNbt(
            item, encoded->id, &encoded->payload);
        const auto* components = item.get("components");
        const auto* decoded = components
            ? components->get("minecraft:attribute_modifiers") : nullptr;
        const auto restored = decoded
            ? protocolComponentFromNbt("minecraft:attribute_modifiers",
                                       *decoded)
            : std::nullopt;
        check(applied && restored && restored->payload == golden,
              "attribute_modifiers round-trips through saved item NBT");

        ItemStack stack = ItemStack::ofName("minecraft:diamond_sword");
        stack.components.emplace_back(encoded->id, encoded->payload);
        WriteBuffer wire;
        stack.write(wire);
        ReadBuffer input(wire.data);
        const auto networkStack = ItemStack::read(input);
        const auto savedStack = ItemStack::fromNbt(stack.toNbt(-1));
        check(input.remaining() == 0 && networkStack.sameItemData(stack) &&
                  savedStack && savedStack->sameItemData(stack),
              "attribute_modifiers survives full ItemStack wire and NBT round trips");
    }

    auto directList = nbt::Value::makeList(nbt::Compound);
    directList.list.push_back(attributeModifier(
        "attack_damage", "test:default_slot", 1.5, "add_value"));
    const auto defaulted = protocolComponentFromNbt(
        "minecraft:attribute_modifiers", directList);
    check(defaulted && defaulted->payload.size() >= 2 &&
              defaulted->payload[defaulted->payload.size() - 2] == 0 &&
              defaulted->payload.back() == 1,
          "attribute NBT list form defaults slot to any and tooltip to true");

    auto everyEntry = nbt::Value::makeList(nbt::Compound);
    for (const auto attribute : kAttributeNames) {
        for (const auto operation : kAttributeModifierOperations) {
            for (const auto slot : kAttributeModifierSlots) {
                everyEntry.list.push_back(attributeModifier(
                    std::string(attribute), "test:modifier", 0.25,
                    std::string(operation), std::string(slot)));
            }
        }
    }
    const auto fullRegistry = protocolComponentFromNbt(
        "minecraft:attribute_modifiers", everyEntry);
    check(fullRegistry && fullRegistry->payload.size() > 9000 &&
              isValidProtocolComponentPayload(13, fullRegistry->payload),
          "all 32 vanilla attributes, three operations, and ten slots encode and validate");

    const auto unknownAttribute = protocolComponentFromNbt(
        "minecraft:attribute_modifiers",
        attributeComponent(attributeModifier(
            "example:modded_attribute", "test:modifier", 1.0,
            "add_value")));
    check(!unknownAttribute,
          "unknown modded attribute names are not misencoded as vanilla IDs");

    WriteBuffer invalidAttributeId;
    invalidAttributeId.varint(1);
    invalidAttributeId.varint(32);
    invalidAttributeId.string("test:modifier");
    invalidAttributeId.f64(1.0);
    invalidAttributeId.varint(0);
    invalidAttributeId.varint(0);
    invalidAttributeId.boolean(true);
    check(!isValidProtocolComponentPayload(13, invalidAttributeId.data),
          "attribute payload rejects a registry ID outside vanilla 1.21.4");
}

void testBundleContentsWireAndPersistence() {
    using namespace cppfm;
    using namespace cppfm::item_component_nbt;

    const auto saved = bundleComponent(itemNbt("minecraft:stone"));
    const auto encoded = protocolComponentFromNbt(
        "minecraft:bundle_contents", saved);
    const std::vector<std::uint8_t> golden{0x01, 0x01, 0x01, 0x00, 0x00};
    check(encoded && encoded->id == 40 && encoded->payload == golden,
          "bundle_contents encodes an ordered nested ItemStack list without slots");
    check(encoded && isValidProtocolComponentPayload(40, encoded->payload),
          "bundle_contents payload passes recursive ItemStack validation");

    if (encoded) {
        nbt::Value item = itemNbt("minecraft:bundle");
        const bool applied = applyProtocolComponentToNbt(
            item, encoded->id, &encoded->payload);
        const auto* components = item.get("components");
        const auto* decoded = components
            ? components->get("minecraft:bundle_contents") : nullptr;
        const auto restored = decoded
            ? protocolComponentFromNbt("minecraft:bundle_contents", *decoded)
            : std::nullopt;
        check(applied && restored && restored->payload == golden,
              "bundle_contents round-trips through item NBT");

        ItemStack stack = ItemStack::ofName("minecraft:bundle");
        stack.components.emplace_back(encoded->id, encoded->payload);
        WriteBuffer wire;
        stack.write(wire);
        ReadBuffer input(wire.data);
        const auto networkStack = ItemStack::read(input);
        const auto savedStack = ItemStack::fromNbt(stack.toNbt(-1));
        check(input.remaining() == 0 && networkStack.sameItemData(stack) &&
                  savedStack && savedStack->sameItemData(stack),
              "bundle contents survive full ItemStack wire and NBT round trips");
    }

    auto nestedBundle = itemNbt("minecraft:bundle");
    auto nestedContents = bundleComponent(itemNbt("minecraft:stone"));
    auto nestedComponents = nbt::Value::makeCompound();
    nestedComponents.set("minecraft:bundle_contents", std::move(nestedContents));
    nestedBundle.set("components", std::move(nestedComponents));
    const auto nested = protocolComponentFromNbt(
        "minecraft:bundle_contents", bundleComponent(std::move(nestedBundle)));
    check(nested && isValidProtocolComponentPayload(40, nested->payload),
          "bundle_contents recursively handles a bundle nested inside a bundle");

    auto emptyItem = itemNbt("minecraft:stone", 0);
    check(!protocolComponentFromNbt("minecraft:bundle_contents",
                                    bundleComponent(std::move(emptyItem))),
          "bundle_contents rejects an empty ItemStack entry");

    const std::vector<std::uint8_t> emptyStackPayload{
        0x01, 0x00};
    check(!isValidProtocolComponentPayload(40, emptyStackPayload),
          "bundle_contents rejects a wire-level empty ItemStack");
    const std::vector<std::uint8_t> excessiveCount{
        0x81, 0x08}; // 1025 entries, over the local defensive bound
    check(!isValidProtocolComponentPayload(40, excessiveCount),
          "bundle_contents applies its documented local element safety cap");
}

void testBundleSelectionAndExtraction() {
    using namespace cppfm;
    using namespace cppfm::item_component_nbt;

    auto contents = nbt::Value::makeList(nbt::Compound);
    contents.list.push_back(itemNbt("minecraft:stone"));
    contents.list.push_back(itemNbt("minecraft:dirt"));
    const auto encoded = protocolComponentFromNbt(
        "minecraft:bundle_contents", contents);
    check(encoded.has_value(), "two-stack bundle fixture encodes");
    if (!encoded) return;

    ItemStack bundle = ItemStack::ofName("minecraft:bundle");
    bundle.components.emplace_back(encoded->id, encoded->payload);
    bundle.selectedBundleStackIndex = 1;

    WriteBuffer wire;
    bundle.write(wire);
    ReadBuffer input(wire.data);
    auto networkCopy = ItemStack::read(input);
    const auto savedCopy = ItemStack::fromNbt(bundle.toNbt(-1));
    check(input.remaining() == 0 &&
              networkCopy.selectedBundleStackIndex == -1 &&
              savedCopy && savedCopy->selectedBundleStackIndex == -1 &&
              networkCopy.sameItemData(bundle) &&
              savedCopy->sameItemData(bundle),
          "selected bundle index is transient and excluded from stack identity");

    auto selected = bundle_contents::popSelected(bundle);
    auto remaining = bundle_contents::decode(bundle);
    check(selected && selected->name() == "minecraft:dirt" && remaining &&
              remaining->size() == 1 &&
              remaining->front().name() == "minecraft:stone" &&
              bundle.selectedBundleStackIndex == -1,
          "bundle extraction removes the selected stack and resets selection");

    auto persisted = ItemStack::fromNbt(savedCopy->toNbt(-1));
    check(persisted.has_value(), "persisted bundle fixture restores");
    if (persisted) {
        (void)bundle_contents::popSelected(*persisted);
        (void)bundle_contents::popSelected(*persisted);
        const auto savedEmpty = persisted->toNbt(-1);
        const auto* components = savedEmpty.get("components");
        check(!components ||
                  !components->get("minecraft:bundle_contents"),
              "removing the final stack removes bundle_contents from saved NBT");
    }
}

void testAdditionalVanillaComponentRoundTrips() {
    using namespace cppfm;

    const auto vanillaEatSound = nbt::Value::makeString(
        "minecraft:entity.generic.eat");
    WriteBuffer soundHolder;
    const bool encodedEatSound =
        item_component_nbt::additional_food::detail::writeSoundHolder(
            &vanillaEatSound, soundHolder);
    check(encodedEatSound &&
              soundHolder.data == std::vector<std::uint8_t>{0xDA, 0x04},
          "vanilla sound holder uses its exact registered protocol ID");
    ReadBuffer soundHolderInput(soundHolder.data);
    const auto decodedEatSound =
        item_component_nbt::additional_food::detail::readSoundHolder(
            soundHolderInput, 0);
    check(decodedEatSound &&
              decodedEatSound->id == "minecraft:entity.generic.eat" &&
              decodedEatSound->registryReference &&
              !decodedEatSound->fixedRange &&
              soundHolderInput.remaining() == 0,
          "registered sound holder ID decodes to its vanilla sound value");
    const auto savedEatSound = decodedEatSound
        ? item_component_nbt::additional_food::detail::soundToNbt(
              *decodedEatSound)
        : nbt::Value{};
    check(savedEatSound.tag == nbt::String &&
              savedEatSound.str == "minecraft:entity.generic.eat",
          "registered sound reference keeps its canonical NBT form");

    auto directSound = nbt::Value::makeCompound();
    directSound.set("sound_id", nbt::Value::makeString(
                                     "minecraft:entity.generic.eat"));
    WriteBuffer directSoundHolder;
    const bool encodedDirectSound =
        item_component_nbt::additional_food::detail::writeSoundHolder(
            &directSound, directSoundHolder);
    ReadBuffer directSoundInput(directSoundHolder.data);
    const auto decodedDirectSound =
        item_component_nbt::additional_food::detail::readSoundHolder(
            directSoundInput, 0);
    check(encodedDirectSound && !directSoundHolder.data.empty() &&
              directSoundHolder.data.front() == 0 && decodedDirectSound &&
              !decodedDirectSound->registryReference &&
              item_component_nbt::additional_food::detail::soundToNbt(
                  *decodedDirectSound).tag == nbt::Compound &&
              directSoundInput.remaining() == 0,
          "direct sound holder remains distinct from its registry reference");

    auto food = nbt::Value::makeCompound();
    food.set("nutrition", nbt::Value::makeInt(4));
    food.set("saturation", nbt::Value::makeFloat(2.4F));
    food.set("can_always_eat", nbt::Value::makeByte(0));
    checkComponentRoundTrip("minecraft:apple", "minecraft:food",
                            std::move(food),
                            "food component encodes valid vanilla fields");

    auto consumable = nbt::Value::makeCompound();
    consumable.set("consume_seconds", nbt::Value::makeFloat(1.6F));
    consumable.set("animation", nbt::Value::makeString("eat"));
    consumable.set("has_consume_particles", nbt::Value::makeByte(1));
    consumable.set("on_consume_effects", nbt::Value::makeList(nbt::Compound));
    checkComponentRoundTrip("minecraft:apple", "minecraft:consumable",
                            std::move(consumable),
                            "consumable component encodes a vanilla apple use");

    checkComponentRoundTrip("minecraft:honey_bottle",
                            "minecraft:use_remainder",
                            itemNbt("minecraft:glass_bottle"),
                            "use_remainder recursively encodes its ItemStack");

    auto cooldown = nbt::Value::makeCompound();
    cooldown.set("seconds", nbt::Value::makeFloat(0.5F));
    checkComponentRoundTrip("minecraft:ender_pearl", "minecraft:use_cooldown",
                            std::move(cooldown),
                            "use_cooldown encodes finite duration");

    auto tool = nbt::Value::makeCompound();
    tool.set("rules", nbt::Value::makeList(nbt::Compound));
    tool.set("default_mining_speed", nbt::Value::makeFloat(1.0F));
    tool.set("damage_per_block", nbt::Value::makeInt(1));
    checkComponentRoundTrip("minecraft:iron_pickaxe", "minecraft:tool",
                            std::move(tool),
                            "tool component encodes its default mining rule");

    auto equippable = nbt::Value::makeCompound();
    equippable.set("slot", nbt::Value::makeString("head"));
    checkComponentRoundTrip("minecraft:iron_helmet", "minecraft:equippable",
                            std::move(equippable),
                            "equippable component encodes a head slot");

    auto repairable = nbt::Value::makeCompound();
    auto repairItems = nbt::Value::makeList(nbt::String);
    repairItems.list.push_back(
        nbt::Value::makeString("minecraft:iron_ingot"));
    repairable.set("items", std::move(repairItems));
    checkComponentRoundTrip("minecraft:iron_sword", "minecraft:repairable",
                            std::move(repairable),
                            "repairable component encodes a concrete item set");

    auto deathProtection = nbt::Value::makeCompound();
    auto deathEffects = nbt::Value::makeList(nbt::Compound);
    auto clearEffects = nbt::Value::makeCompound();
    clearEffects.set("type", nbt::Value::makeString(
                                  "minecraft:clear_all_effects"));
    deathEffects.list.push_back(std::move(clearEffects));
    deathProtection.set("death_effects", std::move(deathEffects));
    checkComponentRoundTrip("minecraft:totem_of_undying",
                            "minecraft:death_protection",
                            std::move(deathProtection),
                            "death_protection encodes a supported consume effect");

    auto bareConsumeEffect = nbt::Value::makeCompound();
    bareConsumeEffect.set("type", nbt::Value::makeString(
                                      "clear_all_effects"));
    WriteBuffer bareConsumePayload;
    const bool bareConsumeEncoded =
        item_component_nbt::additional_food::detail::writeConsumeEffect(
            bareConsumeEffect, bareConsumePayload, 0);
    ReadBuffer bareConsumeInput(bareConsumePayload.data);
    const auto decodedBareConsume =
        item_component_nbt::additional_food::detail::readConsumeEffect(
            bareConsumeInput, 0);
    const auto* decodedBareType = decodedBareConsume
        ? decodedBareConsume->get("type") : nullptr;
    check(bareConsumeEncoded && decodedBareType &&
              decodedBareType->tag == nbt::String &&
              decodedBareType->str == "minecraft:clear_all_effects" &&
              bareConsumeInput.remaining() == 0,
          "bare vanilla consume-effect identifiers canonicalize to minecraft");

    auto foreignConsumeEffect = nbt::Value::makeCompound();
    foreignConsumeEffect.set("type", nbt::Value::makeString(
                                         "example:clear_all_effects"));
    WriteBuffer foreignConsumePayload;
    check(!item_component_nbt::additional_food::detail::writeConsumeEffect(
              foreignConsumeEffect, foreignConsumePayload, 0),
          "foreign namespace cannot alias a vanilla consume-effect operation");

    auto charged = nbt::Value::makeList(nbt::Compound);
    charged.list.push_back(itemNbt("minecraft:arrow"));
    checkComponentRoundTrip("minecraft:crossbow",
                            "minecraft:charged_projectiles",
                            std::move(charged),
                            "charged_projectiles encodes a nested ItemStack");

    checkComponentRoundTrip("minecraft:goat_horn", "minecraft:instrument",
                            nbt::Value::makeString(
                                "minecraft:ponder_goat_horn"),
                            "instrument resolves the captured vanilla registry ID");

    auto jukebox = nbt::Value::makeCompound();
    jukebox.set("song", nbt::Value::makeString("minecraft:cat"));
    jukebox.set("show_in_tooltip", nbt::Value::makeByte(1));
    checkComponentRoundTrip("minecraft:music_disc_cat",
                            "minecraft:jukebox_playable", std::move(jukebox),
                            "jukebox playable resolves captured vanilla song ID");

    auto lodestone = nbt::Value::makeCompound();
    auto target = nbt::Value::makeCompound();
    target.set("dimension", nbt::Value::makeString("minecraft:overworld"));
    nbt::Value position;
    position.tag = nbt::IntArray;
    position.intArray = {1, 64, -1};
    target.set("pos", std::move(position));
    lodestone.set("target", std::move(target));
    lodestone.set("tracked", nbt::Value::makeByte(1));
    checkComponentRoundTrip("minecraft:compass", "minecraft:lodestone_tracker",
                            std::move(lodestone),
                            "lodestone tracker packs dimension and block position");

    auto explosion = nbt::Value::makeCompound();
    explosion.set("shape", nbt::Value::makeString("small_ball"));
    nbt::Value colors;
    colors.tag = nbt::IntArray;
    colors.intArray = {0xFF0000};
    explosion.set("colors", std::move(colors));
    nbt::Value fadeColors;
    fadeColors.tag = nbt::IntArray;
    fadeColors.intArray = {0x0000FF};
    explosion.set("fade_colors", std::move(fadeColors));
    explosion.set("has_trail", nbt::Value::makeByte(1));
    explosion.set("has_twinkle", nbt::Value::makeByte(0));
    checkComponentRoundTrip("minecraft:firework_star",
                            "minecraft:firework_explosion",
                            std::move(explosion),
                            "firework explosion round-trips all color and flag fields");

    auto fireworks = nbt::Value::makeCompound();
    fireworks.set("flight_duration", nbt::Value::makeInt(2));
    fireworks.set("explosions", nbt::Value::makeList(nbt::Compound));
    checkComponentRoundTrip("minecraft:firework_rocket",
                            "minecraft:fireworks", std::move(fireworks),
                            "fireworks component round-trips an empty explosion list");

    auto patterns = nbt::Value::makeCompound();
    auto layers = nbt::Value::makeList(nbt::Compound);
    auto layer = nbt::Value::makeCompound();
    layer.set("pattern", nbt::Value::makeString("minecraft:stripe_center"));
    layer.set("color", nbt::Value::makeString("red"));
    layers.list.push_back(std::move(layer));
    patterns.set("layers", std::move(layers));
    checkComponentRoundTrip("minecraft:white_banner",
                            "minecraft:banner_patterns", std::move(patterns),
                            "banner patterns encode a captured pattern and dye color");

    auto profile = nbt::Value::makeCompound();
    profile.set("name", nbt::Value::makeString("Example"));
    checkComponentRoundTrip("minecraft:player_head",
                            "minecraft:profile", std::move(profile),
                            "profile component encodes a bounded optional name");

    auto sherds = nbt::Value::makeList(nbt::String);
    for (const auto* name : {"minecraft:angler_pottery_sherd",
                             "minecraft:archer_pottery_sherd",
                             "minecraft:arms_up_pottery_sherd",
                             "minecraft:blade_pottery_sherd"})
        sherds.list.push_back(nbt::Value::makeString(name));
    checkComponentRoundTrip("minecraft:decorated_pot",
                            "minecraft:pot_decorations", std::move(sherds),
                            "pot decorations encode four item registry IDs");
    WriteBuffer invalidPotSherds;
    invalidPotSherds.varint(999999);
    invalidPotSherds.varint(0);
    invalidPotSherds.varint(0);
    invalidPotSherds.varint(0);
    check(!item_component_nbt::isValidProtocolComponentPayload(
              61, invalidPotSherds.data),
          "pot decorations reject item IDs outside the captured registry");

    auto blockState = nbt::Value::makeCompound();
    blockState.set("axis", nbt::Value::makeString("x"));
    checkComponentRoundTrip("minecraft:oak_log", "minecraft:block_state",
                            std::move(blockState),
                            "block_state component round-trips string properties");

    auto beeData = nbt::Value::makeList(nbt::Compound);
    auto bee = nbt::Value::makeCompound();
    auto entityData = nbt::Value::makeCompound();
    entityData.set("id", nbt::Value::makeString("minecraft:bee"));
    bee.set("entity_data", std::move(entityData));
    bee.set("ticks_in_hive", nbt::Value::makeInt(100));
    bee.set("min_ticks_in_hive", nbt::Value::makeInt(600));
    beeData.list.push_back(std::move(bee));
    checkComponentRoundTrip("minecraft:bee_nest", "minecraft:bees",
                            std::move(beeData),
                            "bees component encodes bounded entity NBT");
}

void testItemStackComponentStreamAndDepth() {
    using namespace cppfm;

    auto fireworks = nbt::Value::makeCompound();
    fireworks.set("flight_duration", nbt::Value::makeInt(1));
    fireworks.set("explosions", nbt::Value::makeList(nbt::Compound));
    auto cooldown = nbt::Value::makeCompound();
    cooldown.set("seconds", nbt::Value::makeFloat(1.0F));
    const auto first = item_component_nbt::protocolComponentFromNbt(
        "minecraft:fireworks", fireworks);
    const auto second = item_component_nbt::protocolComponentFromNbt(
        "minecraft:use_cooldown", cooldown);
    check(first && second, "multi-component ItemStack fixtures encode");
    if (first && second) {
        auto stack = ItemStack::ofName("minecraft:firework_rocket");
        stack.components.emplace_back(first->id, first->payload);
        stack.components.emplace_back(second->id, second->payload);
        WriteBuffer wire;
        stack.write(wire);
        ReadBuffer input(wire.data);
        const auto decoded = ItemStack::read(input);
        check(input.remaining() == 0 && decoded.sameItemData(stack),
              "component parser leaves following component bytes for ItemStack");
    }

    const auto apple = ItemStack::ofName("minecraft:apple");
    std::vector<std::uint8_t> nestedStack;
    const auto maximumDepth = item_component_nbt::kMaxComponentDepth;
    for (std::size_t layer = 0; layer <= maximumDepth + 1; ++layer) {
        WriteBuffer encoded;
        encoded.varint(1);
        encoded.varint(static_cast<std::int32_t>(apple.itemId));
        if (layer == 0) {
            encoded.varint(0);
            encoded.varint(0);
        } else {
            encoded.varint(1);
            encoded.varint(0);
            encoded.varint(23); // minecraft:use_remainder
            encoded.raw(nestedStack.data(), nestedStack.size());
        }
        nestedStack = std::move(encoded.data);
    }

    bool rejectedDeepStack = false;
    try {
        ReadBuffer input(nestedStack);
        (void)ItemStack::read(input);
    } catch (const std::exception&) {
        rejectedDeepStack = true;
    }
    check(rejectedDeepStack,
          "nested ItemStack decoding enforces one shared component depth");
}

void testAnonymousNbtAllocationBudget() {
    using namespace cppfm;
    using namespace cppfm::item_component_nbt;

    const auto expandedCount = kMaxProtocolComponentAllocationBytes /
                                   sizeof(nbt::Value) +
                               1;
    WriteBuffer expandedList;
    expandedList.u8(nbt::List);
    expandedList.u8(nbt::Byte);
    expandedList.i32(static_cast<std::int32_t>(expandedCount));
    expandedList.data.resize(expandedList.data.size() + expandedCount, 0);
    check(!readAnonymousNbt(expandedList.data),
          "anonymous item-component NBT bounds list expansion before allocation");

    constexpr auto maximumNameLength =
        std::numeric_limits<std::uint16_t>::max();
    WriteBuffer largeNames;
    largeNames.u8(nbt::Compound);
    for (unsigned index = 0; index < 8; ++index) {
        largeNames.u8(nbt::Byte);
        largeNames.u16(maximumNameLength);
        std::string name(maximumNameLength, 'n');
        name[0] = static_cast<char>('A' + index);
        largeNames.raw(name.data(), name.size());
        largeNames.i8(static_cast<std::int8_t>(index));
    }
    largeNames.u8(nbt::End);
    const auto decodedNames = readAnonymousNbt(largeNames.data);
    check(decodedNames && decodedNames->comp.size() == 8 &&
              decodedNames->comp.front().first.size() == maximumNameLength,
          "anonymous item-component NBT accepts long valid names below the wire cap");
}

} // namespace

int main() {
    testStatusEffectBoundsInfiniteDurationAndFallbacks();
    testAttributeModifierWireAndPersistence();
    testBundleContentsWireAndPersistence();
    testBundleSelectionAndExtraction();
    testAdditionalVanillaComponentRoundTrips();
    testItemStackComponentStreamAndDepth();
    testAnonymousNbtAllocationBudget();
    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES") << ": "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
