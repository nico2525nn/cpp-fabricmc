package net.minecraft.entity.mob;

import java.util.HashMap;
import java.util.Map;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.SpawnReason;
import net.minecraft.entity.conversion.EntityConversionContext;
import net.minecraft.entity.ai.control.JumpControl;
import net.minecraft.entity.ai.control.LookControl;
import net.minecraft.entity.ai.control.MoveControl;
import net.minecraft.entity.ai.goal.Goal;
import net.minecraft.entity.ai.goal.GoalSelector;
import net.minecraft.entity.ai.pathing.EntityNavigation;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.util.collection.DefaultedList;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Hand;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.ServerWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldAccess;

/** 1.21.4 mob superclass ABI shared by hostile and passive entities. */
public class MobEntity extends LivingEntity {
    protected GoalSelector targetSelector = new GoalSelector();
    protected GoalSelector goalSelector = new GoalSelector();
    protected boolean persistent;
    protected boolean canPickUpLoot;
    protected boolean aiDisabled;
    protected boolean leftHanded;
    protected boolean attacking;
    protected BlockPos positionTarget = BlockPos.ORIGIN;
    protected float positionTargetRange = -1.0f;
    protected LivingEntity target;
    protected final Map<String, Goal> temporaryTasks = new HashMap<>();
    protected final Map<Object, Float> pathfindingPenalties = new HashMap<>();
    protected EntityNavigation navigation;
    protected MoveControl moveControl;
    protected LookControl lookControl;
    protected JumpControl jumpControl;
    private final DefaultedList<ItemStack> equipment = DefaultedList.ofSize(2, ItemStack.EMPTY);

    protected MobEntity(EntityType<?> type, World world) {
        super(type, world);
        initializeControls(world);
    }
    protected MobEntity(long nativeHandle, World world, EntityType<?> type) {
        super(nativeHandle, world, type);
        initializeControls(world);
    }
    protected MobEntity() { this(EntityType.UNKNOWN, null); }

    private void initializeControls(World world) {
        navigation = new EntityNavigation(this, world);
        moveControl = new MoveControl(this);
        lookControl = new LookControl(this);
        jumpControl = new JumpControl(this);
    }

    public GoalSelector getGoalSelector() { return goalSelector; }
    public GoalSelector getTargetSelector() { return targetSelector; }
    public GoalSelector getAI(boolean target) { return target ? targetSelector : goalSelector; }
    public Map<String, Goal> getTemporaryTasks() { return temporaryTasks; }
    public EntityNavigation getNavigation() { return navigation; }
    public MoveControl getMoveControl() { return moveControl; }
    public LookControl getLookControl() { return lookControl; }
    public JumpControl getJumpControl() { return jumpControl; }
    public LivingEntity getTarget() { return target; }
    public void setTarget(LivingEntity value) { target = value; }
    public boolean isPersistent() { return persistent; }
    public void setPersistent() { persistent = true; }
    public void setPersistence(boolean value) { persistent = value; }
    public boolean cannotDespawn() { return persistent; }
    public boolean isAiDisabled() { return aiDisabled; }
    public void setAiDisabled(boolean value) { aiDisabled = value; }
    public boolean isLeftHanded() { return leftHanded; }
    public void setLeftHanded(boolean value) { leftHanded = value; }
    public boolean isAttacking() { return attacking; }
    public void setAttacking(boolean value) { attacking = value; }
    public BlockPos getPositionTarget() { return positionTarget; }
    public void setPositionTarget(BlockPos value, int range) {
        positionTarget = value == null ? BlockPos.ORIGIN : value;
        positionTargetRange = range;
    }
    public void clearPositionTarget() { positionTarget = BlockPos.ORIGIN; positionTargetRange = -1.0f; }
    public boolean hasPositionTarget() { return positionTargetRange >= 0.0f; }
    public boolean isInWalkTargetRange() { return hasPositionTarget(); }
    public boolean isInWalkTargetRange(BlockPos pos) { return pos != null && hasPositionTarget(); }
    public float getPositionTargetRange() { return positionTargetRange; }

    public int getMaxLookYawChange() { return 10; }
    public int getMaxLookPitchChange() { return 40; }
    public int getMaxHeadRotation() { return 75; }
    public boolean canImmediatelyDespawn(double distanceSquared) { return !persistent && distanceSquared > 16384.0; }
    public boolean canPickupItem(ItemStack stack) { return canPickUpLoot && stack != null && !stack.isEmpty(); }
    public boolean canGather(ServerWorld world, ItemStack stack) { return canPickupItem(stack); }
    public void setCanPickUpLoot(boolean value) { canPickUpLoot = value; }
    public boolean isAffectedByDaylight() { return false; }
    public boolean movesIndependently() { return false; }
    public int getLimitPerChunk() { return 4; }
    public boolean canUseRangedWeapon(Item weapon) { return false; }
    public void mobTick(ServerWorld world) { }
    public void readCustomDataFromNbt(NbtCompound nbt) {
        // Vanilla reads two equipment slots through DefaultedList.set.  The
        // calls are also the stable target for Lithium's WrapOperation hook.
        equipment.set(0, ItemStack.EMPTY);
        equipment.set(1, ItemStack.EMPTY);
    }
    @Override public void baseTick() {
        super.baseTick();
        if (isAlive()) {
            // Keep MobEntity.isAlive() as an expression-injection anchor.
        }
    }
    public void initGoals() { }
    public void playAmbientSound() { }
    public void playAttackSound() { }
    public void playSpawnEffects() { }
    public void resetSoundDelay() { }
    public void updateGoalControls() { }
    public void clearGoals() { goalSelector.clear(); targetSelector.clear(); temporaryTasks.clear(); }
    public void clearGoalsAndTasks() { clearGoals(); }
    public void stopMovement() { if (navigation != null) navigation.stop(); }
    public void setForwardSpeed(float value) { }
    public void setSidewaysSpeed(float value) { }
    public void setUpwardSpeed(float value) { }
    public void lookAtEntity(net.minecraft.entity.Entity entity, float maxYawChange, float maxPitchChange) { }
    public void onEatingGrass() { }
    public void updateDespawnCounter() { }
    public boolean isDisallowedInPeaceful() { return false; }
    public void setPathfindingPenalty(Object nodeType, float penalty) { pathfindingPenalties.put(String.valueOf(nodeType), penalty); }
    public float getPathfindingPenalty(Object nodeType) { return pathfindingPenalties.getOrDefault(String.valueOf(nodeType), 0.0f); }
    public void setBaby(boolean baby) { }
    public boolean isBaby() { return false; }
    public EntityNavigation createNavigation(World world) { return new EntityNavigation(this, world); }
    public DefaultAttributeContainer.Builder createMobAttributes() { return DefaultAttributeContainer.builder(); }

    public static boolean canMobSpawn(EntityType<?> type, WorldAccess world, SpawnReason reason,
                                      BlockPos pos, Random random) { return true; }
    public boolean canSpawn(WorldAccess world) { return true; }
    public static boolean canSpawnIgnoreLightLevel(EntityType<?> type, WorldAccess world,
                                                   SpawnReason reason, BlockPos pos, Random random) { return true; }
    protected static boolean isSpawnDark(ServerWorldAccess world, BlockPos pos, Random random) { return true; }
    public boolean spawnsTooManyForEachTry(int count) { return count > getLimitPerChunk(); }
    public boolean interactMob(PlayerEntity player, Hand hand) { return false; }
    public net.minecraft.util.ActionResult interactWithItem(PlayerEntity player, Hand hand) {
        return net.minecraft.util.ActionResult.PASS;
    }
    /** Mob conversion boundary used by Fabric entity events. */
    public MobEntity convertTo(EntityType<?> type, EntityConversionContext context,
                               SpawnReason reason, EntityConversionContext.Finalizer finalizer) {
        if (finalizer != null) finalizer.finalizeConversion(this);
        return this;
    }
    /** Riding lifecycle entrypoint used by the mob mixin set. */
    public boolean startRiding(net.minecraft.entity.Entity entity, boolean force) {
        return entity != null;
    }
}
