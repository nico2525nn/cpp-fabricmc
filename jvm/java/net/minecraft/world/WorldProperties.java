package net.minecraft.world;

public interface WorldProperties {
    long getTime();
    long getTimeOfDay();
    boolean isRaining();
    boolean isThundering();
}
