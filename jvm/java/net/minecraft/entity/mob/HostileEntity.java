package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnReason;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.ServerWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldAccess;

/** 1.21.4 hostile-mob ABI used by Carpet's spawn and despawn mixins. */
public class HostileEntity extends MobEntity {
    protected HostileEntity(EntityType<?> type, World world) { super(type, world); }
    protected HostileEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected HostileEntity() { super((EntityType<?>) null, (World) null); }

    public static boolean canSpawnInDark(EntityType<?> type, ServerWorldAccess world,
                                         SpawnReason reason, BlockPos pos, Random random) {
        return true;
    }
    public static boolean canSpawnIgnoreLightLevel(EntityType<?> type, WorldAccess world,
                                                   SpawnReason reason, BlockPos pos, Random random) {
        return true;
    }
    protected static boolean isSpawnDark(ServerWorldAccess world, BlockPos pos, Random random) {
        return true;
    }
    public static DefaultAttributeContainer.Builder createHostileAttributes() {
        return DefaultAttributeContainer.builder();
    }
    public boolean isAngryAt(ServerWorld world, PlayerEntity player) { return false; }
    public void updateDespawnCounter() { }
}
