package net.minecraft.entity.passive;

import java.util.UUID;
import java.util.function.DoubleSupplier;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.entity.data.TrackedData;
import net.minecraft.item.ItemStack;
import net.minecraft.util.ActionResult;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.World;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.inventory.Inventory;

/** Core horse hierarchy ABI used by server-side optimization mods. */
public abstract class AbstractHorseEntity extends AnimalEntity {
    protected final Inventory armorInventory = new net.minecraft.inventory.SimpleInventory(2);
    protected int temper;
    protected UUID ownerUuid;
    protected boolean inAir;
    protected boolean jumping;
    protected boolean tamed;
    protected boolean saddled;
    protected boolean bred;
    protected boolean eating;

    protected AbstractHorseEntity(EntityType<?> type, World world) { super(type, world); }
    protected AbstractHorseEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected AbstractHorseEntity() { super(); }

    public void setInAir(boolean value) { inAir = value; }
    public boolean isInAir() { return inAir; }
    public void setBred(boolean value) { bred = value; }
    public boolean isBred() { return bred; }
    public boolean isTame() { return tamed; }
    public void setTame(boolean value) { tamed = value; }
    public boolean isSaddled() { return saddled; }
    public void setSaddled(boolean value) { saddled = value; }
    public int getTemper() { return temper; }
    public void setTemper(int value) { temper = Math.max(0, value); }
    public int addTemper(int difference) { temper = Math.max(0, temper + difference); return temper; }
    public int getMaxTemper() { return 100; }
    public boolean isEatingGrass() { return eating; }
    public void setEatingGrass(boolean value) { eating = value; }
    public Inventory getArmorInventory() { return armorInventory; }
    public int getInventorySize() { return armorInventory.size(); }
    public int getInventoryColumns() { return 1; }
    public UUID getOwnerUuid() { return ownerUuid; }
    public void setOwnerUuid(UUID value) { ownerUuid = value; }
    public void jump(float strength, Vec3d movementInput) { jumping = true; }
    public void travel(Vec3d movementInput) { move(net.minecraft.entity.MovementType.SELF, movementInput); }
    public void initAttributes(Random random) { }
    public void initCustomGoals() { }
    public void onChestedStatusChanged() { }
    public void equipHorseArmor(PlayerEntity player, ItemStack stack) { }
    public void putPlayerOnBack(PlayerEntity player) { }
    public ActionResult interactHorse(PlayerEntity player, ItemStack stack) { return ActionResult.PASS; }
    public boolean canBreed() { return isAlive() && isTame(); }
    public boolean canUseItem(ItemStack stack) { return stack != null && !stack.isEmpty(); }
    public static DefaultAttributeContainer.Builder createBaseHorseAttributes() {
        return DefaultAttributeContainer.builder();
    }
    public static double calculateAttributeBaseValue(double parentBase, double otherParentBase,
                                                      double min, double max, Random random) {
        double value = (parentBase + otherParentBase + 1.0) / 2.0;
        return Math.max(min, Math.min(max, value));
    }
    public double getChildMovementSpeedBonus(DoubleSupplier randomDoubleGetter) {
        return randomDoubleGetter == null ? 0.0 : randomDoubleGetter.getAsDouble();
    }
    public double getChildJumpStrengthBonus(DoubleSupplier randomDoubleGetter) {
        return randomDoubleGetter == null ? 0.0 : randomDoubleGetter.getAsDouble();
    }
    public void setChildAttributes(AbstractHorseEntity child, AbstractHorseEntity other) { }
    public void setChildAttribute(AbstractHorseEntity child, Object block, Object attribute,
                                   double min, double max) { }
    public void walkToParent(ServerWorld world) { }
    public void playEatingAnimation() { }
    public void playAngrySound() { }
    public void wagTail() { }
    public void playJumpSound() { }
    public void updateAnger() { }
    public void spawnPlayerReactionParticles(boolean positive) { }
    public boolean eatsGrass() { return true; }
    public int getMinAmbientStandDelay() { return 40; }
    public boolean shouldAmbientStand() { return true; }
    public void onTrackedDataSet(TrackedData<?> data) { }
}
