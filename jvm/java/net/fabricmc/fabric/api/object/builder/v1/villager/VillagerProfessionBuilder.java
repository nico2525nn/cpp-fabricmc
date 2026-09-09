package net.fabricmc.fabric.api.object.builder.v1.villager;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.block.Block;
import net.minecraft.item.Item;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;
import net.minecraft.village.VillagerProfession;
import net.minecraft.world.poi.PointOfInterestType;

public final class VillagerProfessionBuilder {
    private Identifier id;
    private Predicate<RegistryEntry<PointOfInterestType>> workstation = value -> false;
    private Predicate<RegistryEntry<PointOfInterestType>> jobSite = value -> false;
    private final Set<Item> harvestable = new LinkedHashSet<>();
    private final Set<Block> secondary = new LinkedHashSet<>();
    private SoundEvent workSound;
    private VillagerProfessionBuilder() { }
    public static VillagerProfessionBuilder create() { return new VillagerProfessionBuilder(); }
    public VillagerProfessionBuilder id(Identifier value) { id = value; return this; }
    public VillagerProfessionBuilder workstation(RegistryKey<PointOfInterestType> key) { workstation = value -> value != null && key != null && key.equals(value.registryKey()); return this; }
    public VillagerProfessionBuilder workstation(Predicate<RegistryEntry<PointOfInterestType>> value) { workstation = value == null ? entry -> false : value; return this; }
    public VillagerProfessionBuilder jobSite(Predicate<RegistryEntry<PointOfInterestType>> value) { jobSite = value == null ? entry -> false : value; return this; }
    public VillagerProfessionBuilder harvestableItems(Item... values) { if (values != null) for (Item value : values) if (value != null) harvestable.add(value); return this; }
    public VillagerProfessionBuilder harvestableItems(Iterable<Item> values) { if (values != null) for (Item value : values) if (value != null) harvestable.add(value); return this; }
    public VillagerProfessionBuilder secondaryJobSites(Block... values) { if (values != null) for (Block value : values) if (value != null) secondary.add(value); return this; }
    public VillagerProfessionBuilder secondaryJobSites(Iterable<Block> values) { if (values != null) for (Block value : values) if (value != null) secondary.add(value); return this; }
    public VillagerProfessionBuilder workSound(SoundEvent value) { workSound = value; return this; }
    public VillagerProfession build() { return new VillagerProfession(id == null ? "cppfm:profession" : id.toString(), jobSite, workstation, harvestable, secondary, workSound); }
}
