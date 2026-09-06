package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.data.TrackedData;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.particle.ParticleEffect;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.thread.ReentrantThreadExecutor;
import net.minecraft.village.TradeOffer;
import net.minecraft.village.TradeOfferList;
import net.minecraft.village.TradeOffers;
import net.minecraft.inventory.SimpleInventory;
import net.minecraft.world.World;

/** Server-side merchant ABI shared by villagers and wandering traders. */
public class MerchantEntity extends net.minecraft.entity.mob.MobEntity {
    public static final int INVENTORY_SIZE = 8;
    public static final TrackedData<Integer> HEAD_ROLLING_TIME_LEFT = new TrackedData<>();
    protected TradeOfferList offers = new TradeOfferList();
    protected final SimpleInventory inventory = new SimpleInventory(INVENTORY_SIZE);
    protected PlayerEntity customer;

    public MerchantEntity(EntityType<?> type, World world) { super(type, world); }
    protected MerchantEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }

    protected void produceParticles(ParticleEffect parameters) { }
    public void fillRecipes() { }
    public void setOffersFromServer(TradeOfferList offers) { this.offers = offers == null ? new TradeOfferList() : offers; }
    public TradeOfferList getOffers() { return offers; }
    public void resetCustomer() { customer = null; }
    public boolean hasCustomer() { return customer != null; }
    public PlayerEntity getCustomer() { return customer; }
    public void setCustomer(PlayerEntity value) { customer = value; }
    public SimpleInventory getInventory() { return inventory; }
    public void playCelebrateSound() { }
    public void afterUsing(TradeOffer offer) { }
    public int getHeadRollingTimeLeft() { return 0; }
    public void setHeadRollingTimeLeft(int ticks) { }
    protected void fillRecipesFromPool(TradeOfferList recipeList, TradeOffers.Factory[] pool, int count) { }
    protected SoundEvent getTradingSound(boolean sold) { return null; }
    public boolean canInteractWith(PlayerEntity player) { return player != null && player.isAlive(); }
    public void trade(TradeOffer offer) { }
    public void onSelling(ItemStack stack) { }
}
