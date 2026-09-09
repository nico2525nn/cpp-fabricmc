package net.minecraft.entity.raid;

import net.minecraft.nbt.NbtCompound;
import net.minecraft.server.world.ServerWorld;

/** Minimal state holder for the public RaiderEntity method signatures. */
public class Raid {
    private final int id;
    private final ServerWorld world;

    public Raid(ServerWorld world, NbtCompound nbt) {
        this.world = world;
        this.id = nbt == null ? 0 : nbt.getInt("Id");
    }

    public Raid(int id, ServerWorld world, Object center) {
        this.id = id;
        this.world = world;
    }

    public int getId() { return id; }
    public ServerWorld getWorld() { return world; }
}
