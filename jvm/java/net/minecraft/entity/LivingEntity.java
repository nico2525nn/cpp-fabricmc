package net.minecraft.entity;

import cppfm.bridge.WrapperCache;
import net.minecraft.item.ItemStack;
import net.minecraft.block.BlockState;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.Hand;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.TypedActionResult;
import net.minecraft.world.World;
import java.util.Map;
import java.util.Collections;

public class LivingEntity extends Entity {
    private float health = 20.0f;
    private float maxHealth = 20.0f;
    private boolean usingItem;
    /** Vanilla jump state exposed by Fabric's server-side access wideners. */
    private boolean jumping;
    /** Vanilla first-update flag retained as the FIELD injection anchor. */
    private boolean firstUpdate;
    private Hand activeHand = Hand.MAIN_HAND;
    private EntityPose pose = EntityPose.STANDING;

    protected LivingEntity(long nativeHandle) { super(nativeHandle); }
    protected LivingEntity(EntityType<?> type, World world) { super(type, world); }
    protected LivingEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    public static LivingEntity of(long handle) {
        return WrapperCache.get(LivingEntity.class, handle, LivingEntity::new);
    }
    public float getHealth() { return nativeHandle == 0 ? health : NativeAccess.entityHealth(nativeHandle); }
    public float getMaxHealth() { return maxHealth; }
    public void setHealth(float value) {
        health = Math.max(0.0f, Math.min(maxHealth, value));
        if (nativeHandle != 0) NativeAccess.setEntityHealth(nativeHandle, health);
    }
    public void setMaxHealth(float value) { maxHealth = Math.max(0.0f, value); setHealth(health); }
    public boolean damage(net.minecraft.entity.damage.DamageSource source, float amount) {
        if (!isAlive() || !Float.isFinite(amount) || amount <= 0.0f || isInvulnerableTo(source)) return false;
        net.minecraft.entity.damage.DamageSource damageSource = source == null
            ? new net.minecraft.entity.damage.DamageSource("generic") : source;
        if (!net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DAMAGE.invoker()
                .allowDamage(this, damageSource, amount)) return false;
        net.minecraft.entity.DamageSource legacySource = new net.minecraft.entity.DamageSource(
            damageSource.getName(), damageSource.getSource(), damageSource.getAttacker());
        if (!net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.ALLOW_DAMAGE.invoker()
                .allowDamage(this, legacySource, amount)) return false;
        float before = health;
        if (nativeHandle != 0 && !NativeAccess.entityDead(nativeHandle)) {
            float observed = getHealth();
            if (observed > 0.0f) before = observed;
        }
        float next = Math.max(0.0f, before - amount);
        setHealth(next);
        boolean died = next <= 0.0f;
        if (died) {
            boolean allowed = net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DEATH.invoker()
                .allowDeath(this, damageSource, amount);
            allowed &= net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.ALLOW_DEATH.invoker()
                .allowDeath(this, legacySource, amount);
            if (this instanceof net.minecraft.server.network.ServerPlayerEntity player)
                allowed &= net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents.ALLOW_DEATH.invoker()
                    .allowDeath(player, damageSource, amount);
            if (allowed) remove(RemovalReason.KILLED);
            else { died = false; setHealth(Math.max(1.0f, Math.min(maxHealth, before))); }
        }
        final boolean blocked = false;
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DAMAGE.invoker()
            .afterDamage(this, damageSource, amount, Math.max(0.0f, before - getHealth()), blocked);
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DAMAGE.invoker()
            .afterDamage(this, legacySource, amount, Math.max(0.0f, before - getHealth()), blocked);
        if (died) {
            net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DEATH.invoker()
                .afterDeath(this, damageSource);
            net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DEATH.invoker()
                .afterDeath(this, legacySource);
        }
        return true;
    }
    /** Server-world overload present in the 1.21.4 mapped LivingEntity ABI. */
    protected boolean damage(net.minecraft.server.world.ServerWorld world,
                             net.minecraft.entity.damage.DamageSource source,
                             float amount) {
        // Keep the two vanilla locals alive through the return boundary. The
        // Fabric AFTER_DAMAGE event captures these exact values with
        // LocalCapture.CAPTURE_FAILHARD; a delegation-only stub would make a
        // real, otherwise compatible event mixin fail during transformation.
        float dealt = 0.0f;
        boolean blocked = false;
        boolean result = damage(source, amount);
        if (result) dealt = Math.max(0.0f, amount);
        if (blocked) dealt = 0.0f;
        return result && dealt >= 0.0f;
    }
    /** Legacy package overload retained for source compatibility. */
    public boolean damage(DamageSource source, float amount) { return damage((net.minecraft.entity.damage.DamageSource) source, amount); }
    public boolean damage(Object source, float amount) {
        return damage(source instanceof net.minecraft.entity.damage.DamageSource d
            ? d : new net.minecraft.entity.damage.DamageSource("generic"), amount);
    }
    public void heal(float amount) { if (amount > 0.0f) setHealth(health + amount); }
    public boolean isDead() { return !isAlive() || (nativeHandle == 0 ? health <= 0.0f : NativeAccess.entityDead(nativeHandle)); }
    public boolean isInvulnerable() { return false; }
    public boolean isInvulnerableTo(net.minecraft.entity.damage.DamageSource source) { return isInvulnerable(); }
    public boolean isInvulnerableTo(DamageSource source) { return isInvulnerableTo((net.minecraft.entity.damage.DamageSource) source); }
    /** Canonical 1.21.4 death hook; mixins may override or inject here. */
    public void onDeath(net.minecraft.entity.damage.DamageSource source) { remove(RemovalReason.KILLED); }
    /** Damage application phase separated from event dispatch for mixin targets. */
    protected void applyDamage(net.minecraft.server.world.ServerWorld world,
                               net.minecraft.entity.damage.DamageSource source, float amount) {
        setHealth(Math.max(0.0f, getHealth() - Math.max(0.0f, amount)));
    }
    public void travelMidAir(net.minecraft.util.math.Vec3d movement) { }
    public boolean canUsePortals(boolean allowVehicles) { return true; }
    /** Riding lifecycle hook used by navigation and server mixins. */
    public void stopRiding() { }
    /** Landing-state lookup used by powder-snow optimizations. */
    public BlockState getLandingBlockState() {
        World currentWorld = getWorld();
        return currentWorld == null ? net.minecraft.block.Blocks.AIR.getDefaultState()
            : currentWorld.getBlockState(getBlockPos());
    }
    /** Attribute lookup boundary retained for the vanilla movement ABI. */
    public net.minecraft.entity.attribute.EntityAttributeInstance getAttributeInstance(
            RegistryEntry<?> attribute) { return null; }
    /** Powder-snow movement hook used by Lithium's fast check mixin. */
    public void addPowderSnowSlowIfNeeded() {
        BlockState state = getLandingBlockState();
        if (!state.isAir()) return;
        net.minecraft.entity.attribute.EntityAttributeInstance instance = getAttributeInstance(null);
        if (instance != null) instance.getValue();
    }
    public boolean isUsingItem() { return usingItem; }
    public Hand getActiveHand() { return activeHand; }
    public ItemStack getActiveItem() { return getStackInHand(activeHand); }
    public void setCurrentHand(Hand hand) { activeHand = hand == null ? Hand.MAIN_HAND : hand; usingItem = true; }
    public void clearActiveItem() { usingItem = false; }
    public ItemStack getStackInHand(Hand hand) { return ItemStack.EMPTY; }
    public void swingHand(Hand hand) { }
    public EntityPose getPose() { return pose; }
    public void setPose(EntityPose value) { pose = value == null ? EntityPose.STANDING : value; }
    public void setPose(net.minecraft.entity.Pose value) { pose = value == null ? EntityPose.STANDING : value.toEntityPose(); }
    public boolean isSleeping() { return pose == EntityPose.SLEEPING; }
    public void sleep(net.minecraft.util.math.BlockPos position) {
        // Keep the real 1.21.4 INVOKE_ASSIGN anchor used by Fabric's
        // EntitySleepEvents mixin.  The native entity remains authoritative;
        // this lookup is also useful for Java-side event listeners.
        World currentWorld = getWorld();
        BlockState state = currentWorld == null
            ? net.minecraft.block.Blocks.AIR.getDefaultState()
            : currentWorld.getBlockState(position);
        if (state != null) pose = EntityPose.SLEEPING;
    }
    public void wakeUp() { pose = EntityPose.STANDING; }
    /** Intermediary alias for the 1.21.4 wake-up method. */
    public void method_18404() { wakeUp(); }
    /** Intermediary overload used by the 1.21.4 sleep-event mixin target. */
    public void method_18404(net.minecraft.util.math.BlockPos position) {
        World currentWorld = getWorld();
        BlockState state = currentWorld == null
            ? net.minecraft.block.Blocks.AIR.getDefaultState()
            : currentWorld.getBlockState(position);
        if (state != null) wakeUp();
    }
    public Boolean method_18405(net.minecraft.util.math.BlockPos position) { return Boolean.FALSE; }
    public net.minecraft.util.math.Direction getSleepingDirection() {
        return net.minecraft.util.math.Direction.SOUTH;
    }
    public boolean isSleepingInBed() { return isSleeping(); }
    public java.util.Optional<net.minecraft.util.math.BlockPos> getSleepingPosition() {
        return java.util.Optional.empty();
    }
    public void setPositionInBed(net.minecraft.util.math.BlockPos position) { }
    public void clearSleepingPosition() { pose = EntityPose.STANDING; }
    public void setSleepingPosition(net.minecraft.util.math.BlockPos position) { pose = EntityPose.SLEEPING; }
    public float getArmor() { return 0.0f; }
    public float getArmorToughness() { return 0.0f; }
    protected void damageArmor(net.minecraft.entity.damage.DamageSource source, float amount) { }
    protected void damageHelmet(net.minecraft.entity.damage.DamageSource source, float amount) { }
    protected void damageShield(float amount) { }
    public boolean isJumping() { return jumping; }
    public void setJumping(boolean value) { jumping = value; }
    /** Vanilla movement attribute hook used by server-side mixins. */
    public float getMovementSpeed(float base) { return base; }
    /**
     * Vanilla's equipment-placement hook.  Keep the common armor naming
     * rules useful for server-side item/entity code while retaining a stable
     * method for Fabric mixins and access wideners.
     */
    public EquipmentSlot getPreferredEquipmentSlot(ItemStack stack) {
        if (stack == null || stack.isEmpty() || stack.getItem() == null) return EquipmentSlot.MAINHAND;
        String path = stack.getItem().getId().getPath();
        if (path.endsWith("_helmet") || path.equals("turtle_helmet")) return EquipmentSlot.HEAD;
        if (path.endsWith("_chestplate") || path.equals("elytra")) return EquipmentSlot.CHEST;
        if (path.endsWith("_leggings")) return EquipmentSlot.LEGS;
        if (path.endsWith("_boots")) return EquipmentSlot.FEET;
        return EquipmentSlot.MAINHAND;
    }
    /** Equipment-diff view exposed by Lithium's equipment tracking mixin. */
    public Map<EquipmentSlot, ItemStack> getEquipmentChanges() {
        return Collections.emptyMap();
    }
    /** Equipment comparison boundary used by Lithium's tracking mixin. */
    public void sendEquipmentChanges() {
        checkHandStackSwap(getEquipmentChanges());
    }
    public void checkHandStackSwap(Map<EquipmentSlot, ItemStack> changes) { }
    /** Elytra/gliding tick hook used by movement optimizations. */
    public void tickGliding() {
        canGlide();
    }
    public boolean canGlide() { return false; }
    /** Hand-swing tick hook used by the fast-hand-swing mixin. */
    public void tickHandSwing() { }
    public ItemStack getEquippedStack(EquipmentSlot slot) { return ItemStack.EMPTY; }
    public void equipStack(EquipmentSlot slot, ItemStack stack) {
        ItemStack previous = getEquippedStack(slot);
        boolean wasUsingItem = usingItem;
        usingItem = false;
        onEquipStack(slot, previous, stack == null ? ItemStack.EMPTY : stack);
        usingItem = wasUsingItem;
    }
    public void onEquipStack(EquipmentSlot slot, ItemStack oldStack, ItemStack newStack) {
        // Keep the vanilla first-update field access visible to FIELD injectors.
        // The actual equipment side effects are handled by the authoritative
        // native entity; this branch is the stable 1.21.4 bytecode anchor.
        if (firstUpdate) return;
    }
    public void pushEntities() { }
    /** Vanilla cramming hook; extensions may inject before/after this call. */
    public void tickCramming() { }
    public TypedActionResult<ItemStack> tryAttack(Entity target) { return TypedActionResult.success(ItemStack.EMPTY); }

    /**
     * LivingEntity owns this override in the 1.21.4 hierarchy.  Keeping the
     * server enchantment tick call in the Java shell gives MixinExtras
     * WrapWithCondition mixins the same invocation anchor as vanilla.
     */
    @Override public void baseTick() {
        super.baseTick();
        net.minecraft.world.World current = getWorld();
        net.minecraft.server.world.ServerWorld serverWorld = current instanceof net.minecraft.server.world.ServerWorld sw
            ? sw : null;
        net.minecraft.enchantment.EnchantmentHelper.onTick(serverWorld, this);
    }
}
