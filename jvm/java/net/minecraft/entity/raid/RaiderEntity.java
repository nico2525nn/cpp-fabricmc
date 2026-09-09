package net.minecraft.entity.raid;

import net.minecraft.entity.ItemEntity;
import net.minecraft.entity.mob.Monster;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.sound.SoundEvent;

/** 1.21.4 marker/API interface for entities participating in raids. */
public interface RaiderEntity extends Monster {
    int getOutOfRaidCounter();
    void setOutOfRaidCounter(int counter);
    boolean isCelebrating();
    void setCelebrating(boolean celebrating);
    boolean hasRaid();
    boolean hasActiveRaid();
    Raid getRaid();
    int getWave();
    void setWave(int wave);
    boolean isCaptain();
    boolean canJoinRaid();
    void setAbleToJoinRaid(boolean ableToJoinRaid);
    void setRaid(Raid raid);
    SoundEvent getCelebratingSound();
    void addBonusForWave(ServerWorld world, int wave, boolean unused);

    default boolean method_16483(ItemEntity itemEntity) { return false; }
}
