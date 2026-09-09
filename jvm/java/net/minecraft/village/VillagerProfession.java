package net.minecraft.village;

import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.block.Block;
import net.minecraft.item.Item;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.sound.SoundEvent;
import net.minecraft.world.poi.PointOfInterestType;

/** Value object describing a villager profession. */
public final class VillagerProfession {
    private final String id;
    private final Predicate<RegistryEntry<PointOfInterestType>> heldJobSite;
    private final Predicate<RegistryEntry<PointOfInterestType>> acquirableJobSite;
    private final Set<Item> gatherableItems;
    private final Set<Block> secondaryJobSites;
    private final SoundEvent workSound;
    public VillagerProfession(String id,
            Predicate<RegistryEntry<PointOfInterestType>> heldJobSite,
            Predicate<RegistryEntry<PointOfInterestType>> acquirableJobSite,
            Set<Item> gatherableItems, Set<Block> secondaryJobSites, SoundEvent workSound) {
        this.id = id == null ? "minecraft:none" : id;
        this.heldJobSite = heldJobSite; this.acquirableJobSite = acquirableJobSite;
        this.gatherableItems = Set.copyOf(gatherableItems == null ? Set.of() : gatherableItems);
        this.secondaryJobSites = Set.copyOf(secondaryJobSites == null ? Set.of() : secondaryJobSites);
        this.workSound = workSound;
    }
    public String id() { return id; }
    public Predicate<RegistryEntry<PointOfInterestType>> heldJobSite() { return heldJobSite; }
    public Predicate<RegistryEntry<PointOfInterestType>> acquirableJobSite() { return acquirableJobSite; }
    public Set<Item> gatherableItems() { return gatherableItems; }
    public Set<Block> secondaryJobSites() { return secondaryJobSites; }
    public SoundEvent workSound() { return workSound; }
}
