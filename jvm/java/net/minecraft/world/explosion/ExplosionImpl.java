package net.minecraft.world.explosion;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.LinkedHashMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.item.ItemStack;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;

/** Server-side explosion implementation surface used by Carpet's event hooks. */
public class ExplosionImpl extends Explosion {
    private final ServerWorld world;
    /** Mojang-mapped field spelling used by Carpet's Accessor. */
    private final ServerWorld level;
    private final Entity entity;
    private final DamageSource damageSource;
    private final ExplosionBehavior behavior;
    private final Vec3d pos;
    private final float power;
    private final boolean createFire;
    private final DestructionType destructionType;
    private final Map<LivingPlayerKey, Vec3d> knockbackByPlayer = new LinkedHashMap<>();
    private final List<BlockPos> blocksToDestroy = new ArrayList<>();

    public ExplosionImpl(ServerWorld world, Entity entity, DamageSource damageSource,
                         ExplosionBehavior behavior, Vec3d pos, float power,
                         boolean createFire, DestructionType destructionType) {
        super(world, entity, pos);
        this.world = world; this.level = world; this.entity = entity; this.damageSource = damageSource;
        this.behavior = behavior == null ? new ExplosionBehavior() : behavior;
        this.pos = pos == null ? Vec3d.ZERO : pos; this.power = power;
        this.createFire = createFire;
        this.destructionType = destructionType == null ? DestructionType.DESTROY : destructionType;
    }
    public void explode() { }
    public List<BlockPos> getBlocksToDestroy() { return List.copyOf(blocksToDestroy); }
    public float calculateReceivedDamage(Vec3d position, Entity target) { return power; }
    public void damageEntities() { }
    public boolean isSmall() { return power < 2.0f; }
    public void createFire(List<BlockPos> positions) { }
    public void destroyBlocks(List<BlockPos> positions) { if (positions != null) blocksToDestroy.addAll(positions); }
    public Map<?, Vec3d> getKnockbackByPlayer() { return Map.copyOf(knockbackByPlayer); }
    public DamageSource getDamageSource() { return damageSource; }
    public void addDroppedItem(List<?> droppedItemsOut, ItemStack item, BlockPos pos) { }
    public ExplosionBehavior getBehavior() { return behavior; }
    public ServerWorld getWorld() { return world; }
    public Entity getEntity() { return entity; }
    public Vec3d getPos() { return pos; }
    public float getPower() { return power; }
    public boolean shouldDestroyBlocks() { return destructionType != DestructionType.KEEP; }

    private record LivingPlayerKey(long value) { }
}
