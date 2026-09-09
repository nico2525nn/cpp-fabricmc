package net.minecraft.entity.player;

import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.Entity;
import net.minecraft.item.ItemStack;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.text.Text;
import net.minecraft.util.Hand;
import net.minecraft.util.Arm;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.TypedActionResult;
import net.minecraft.util.ActionResult;
import net.minecraft.world.World;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;

public class PlayerEntity extends LivingEntity {
    /** Reasons exposed by the 1.21.4 sleep API. */
    public enum SleepFailureReason {
        NOT_POSSIBLE_HERE("not_possible_here"),
        NOT_POSSIBLE_NOW("not_possible_now"),
        TOO_FAR_AWAY("too_far_away"),
        OBSTRUCTED("obstructed"),
        NOT_SAFE("not_safe");

        private final net.minecraft.text.Text message;
        SleepFailureReason(String key) { message = net.minecraft.text.Text.literal(key); }
        public net.minecraft.text.Text getMessage() { return message; }
    }

    protected final PlayerInventory inventory;
    protected final PlayerAbilities abilities = new PlayerAbilities();
    protected final HungerManager hungerManager = new HungerManager();
    protected PlayerEntity(long nativeHandle) { super(nativeHandle); inventory = new PlayerInventory(nativeHandle); }
    protected PlayerEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); inventory = new PlayerInventory(nativeHandle); }
    protected PlayerEntity(World world) { this(0L, world, null); }
    public boolean isSneaking() { return NativeAccess.playerSneaking(nativeHandle); }
    public boolean isCreative() { return NativeAccess.playerGameMode(nativeHandle) == 1 || abilities.creativeMode; }
    public boolean isSpectator() { return NativeAccess.playerGameMode(nativeHandle) == 3; }
    public PlayerInventory getInventory() { return inventory; }
    public PlayerAbilities getAbilities() { return abilities; }
    public HungerManager getHungerManager() { return hungerManager; }
    public ItemStack getMainHandStack() { return getInventory().getMainHandStack(); }
    public ItemStack getOffHandStack() { return getInventory().getOffHandStack(); }
    public World getEntityWorld() { return getWorld(); }
    @Override public ItemStack getStackInHand(Hand hand) { return hand == Hand.OFF_HAND ? getOffHandStack() : getMainHandStack(); }
    @Override public void swingHand(Hand hand) { }
    public void sendMessage(Text message) { sendMessage(message, false); }
    public void sendMessage(Text message, boolean overlay) { if (message != null) NativeAccess.sendMessage(nativeHandle, message.getString(), overlay); }
    public boolean giveItemStack(ItemStack stack) {
        if (stack == null || stack.isEmpty()) return false;
        for (int slot = 0; slot < inventory.size(); slot++) {
            if (inventory.getStack(slot).isEmpty()) { inventory.setStack(slot, stack.copy()); return true; }
        }
        return false;
    }
    public boolean dropItem(ItemStack stack, boolean throwRandomly, boolean retainOwnership) { return stack != null && !stack.isEmpty(); }
    public boolean dropItem(ItemStack stack, boolean throwRandomly) { return dropItem(stack, throwRandomly, false); }
    public ActionResult interact(Entity entity, Hand hand) { return ActionResult.PASS; }
    /** Server attack hook retained for combat mixins and mapped callers. */
    public void attack(Entity target) { }
    public TypedActionResult<ItemStack> useItem() { return TypedActionResult.pass(getMainHandStack()); }
    public ItemStack getEquippedStack(net.minecraft.entity.EquipmentSlot slot) {
        if (slot == null) return ItemStack.EMPTY;
        return switch (slot) {
            case MAINHAND -> getMainHandStack(); case OFFHAND -> getOffHandStack();
            case FEET -> inventory.getStack(36); case LEGS -> inventory.getStack(37);
            case CHEST -> inventory.getStack(38); case HEAD -> inventory.getStack(39);
        };
    }
    @Override public void equipStack(net.minecraft.entity.EquipmentSlot slot, ItemStack stack) {
        if (slot == null) return;
        ItemStack previous = getEquippedStack(slot);
        int index = switch (slot) { case MAINHAND -> inventory.selectedSlot; case OFFHAND -> 40; case FEET -> 36; case LEGS -> 37; case CHEST -> 38; case HEAD -> 39; };
        ItemStack current = stack == null ? ItemStack.EMPTY : stack;
        inventory.setStack(index, current);
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.EQUIPMENT_CHANGE.invoker()
            .onChange(this, slot, previous, current);
    }
    public Arm getMainArm() { return Arm.RIGHT; }
    public BlockPos getSpawnPointPosition() { return getBlockPos(); }
    /** 1.21.4 player sleep entrypoint used by Fabric's sleep events. */
    @SuppressWarnings({"rawtypes", "unchecked"})
    public com.mojang.datafixers.util.Either trySleep(BlockPos position) {
        if (position == null || !isAlive())
            return com.mojang.datafixers.util.Either.left("not_possible_here");
        World currentWorld = getWorld();
        if (currentWorld != null) currentWorld.getBlockState(position);
        sleep(position);
        return com.mojang.datafixers.util.Either.right(com.mojang.datafixers.util.Unit.INSTANCE);
    }
    /** Vanilla sleep rule hook used when calculating the day-night reset. */
    public boolean canResetTimeBySleeping() { return true; }
    public float getBlockBreakingSpeed(net.minecraft.block.BlockState state) { return isCreative() ? 1.0f : 1.0f; }
    public float getLuck() { return 0.0f; }
    /** Server-world overload present in the 1.21.4 Yarn ABI. */
    public boolean damage(net.minecraft.server.world.ServerWorld world,
                          net.minecraft.entity.damage.DamageSource source, float amount) {
        return damage(source, amount);
    }
    /** Server-specific damage phase retained as a distinct Mixin target. */
    @Override
    protected void applyDamage(net.minecraft.server.world.ServerWorld world,
                               net.minecraft.entity.damage.DamageSource source, float amount) {
        super.applyDamage(world, source, amount);
    }
    public boolean checkGliding() { return false; }
    public boolean canInteractWithBlockAt(BlockPos pos, double distance) {
        return pos != null && getPos().squaredDistanceTo(Vec3d.ofCenter(pos)) <= distance * distance;
    }
    public boolean canInteractWithEntityIn(net.minecraft.util.math.Box box, double distance) {
        return box != null && getPos().squaredDistanceTo(box.getCenter()) <= distance * distance;
    }
    public void collideWithEntity(Entity entity) { }
    public int experienceLevel() { return 0; }
    public ServerCommandSource getCommandSource() {
        net.minecraft.server.MinecraftServer server = getServer();
        return server == null ? null : new ServerCommandSource(this instanceof net.minecraft.server.network.ServerPlayerEntity p ? p : null, server);
    }
    public Vec3d getRotationVec(float tickDelta) { return super.getRotationVec(tickDelta); }
    /** Yarn's 1.21.4 movement tick hook. */
    public void tickMovement() { }
    /** Mojang-mapped compatibility alias used by older server code. */
    public void aiStep() { tickMovement(); }
    /** Pose recomputation hook declared by the 1.21.4 player ABI. */
    public void updatePose() { }
    /** Common entity tick entrypoint declared on PlayerEntity for mixin ABI. */
    @Override public void tick() { super.tick(); }
}
