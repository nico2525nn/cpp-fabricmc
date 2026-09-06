package net.minecraft.village;

import java.util.Optional;
import net.minecraft.item.ItemStack;
import net.minecraft.network.RegistryByteBuf;

/** Lightweight, ABI-compatible trade offer value object. */
public class TradeOffer {
    public static final Object CODEC = new Object();
    public static final Object PACKET_CODEC = new Object();
    private final TradedItem firstBuyItem;
    private final Optional<ItemStack> secondBuyItem;
    private final ItemStack sellItem;
    private int uses;
    private int maxUses;
    private final boolean rewardingPlayerExperience;
    private int specialPrice;
    private int demandBonus;
    private final float priceMultiplier;
    private final int merchantExperience;

    public TradeOffer(TradedItem buyItem, ItemStack sellItem, int maxUses, int merchantExperience, float priceMultiplier) {
        this(buyItem, Optional.empty(), sellItem, 0, maxUses, true, 0, 0, priceMultiplier, merchantExperience);
    }

    public TradeOffer(TradedItem firstBuyItem, Optional<ItemStack> secondBuyItem, ItemStack sellItem,
                      int maxUses, int merchantExperience, float priceMultiplier) {
        this(firstBuyItem, secondBuyItem, sellItem, 0, maxUses, true, 0, 0, priceMultiplier, merchantExperience);
    }

    public TradeOffer(TradedItem firstBuyItem, Optional<ItemStack> secondBuyItem, ItemStack sellItem,
                      int maxUses, int merchantExperience, float priceMultiplier, int demandBonus) {
        this(firstBuyItem, secondBuyItem, sellItem, 0, maxUses, true, 0, demandBonus, priceMultiplier, merchantExperience);
    }

    public TradeOffer(TradedItem firstBuyItem, Optional<ItemStack> secondBuyItem, ItemStack sellItem,
                      int uses, int maxUses, boolean rewardingPlayerExperience, int specialPrice,
                      int demandBonus, float priceMultiplier, int merchantExperience) {
        this.firstBuyItem = firstBuyItem == null ? new TradedItem(net.minecraft.item.Items.AIR) : firstBuyItem;
        this.secondBuyItem = secondBuyItem == null ? Optional.empty() : secondBuyItem.map(ItemStack::copy);
        this.sellItem = sellItem == null ? ItemStack.EMPTY : sellItem.copy();
        this.uses = Math.max(0, uses);
        this.maxUses = Math.max(0, maxUses);
        this.rewardingPlayerExperience = rewardingPlayerExperience;
        this.specialPrice = specialPrice;
        this.demandBonus = demandBonus;
        this.priceMultiplier = priceMultiplier;
        this.merchantExperience = merchantExperience;
    }

    public TradeOffer(TradeOffer offer) {
        this(offer == null ? null : offer.firstBuyItem,
             offer == null ? Optional.empty() : offer.secondBuyItem,
             offer == null ? ItemStack.EMPTY : offer.sellItem,
             offer == null ? 0 : offer.uses,
             offer == null ? 0 : offer.maxUses,
             offer == null || offer.rewardingPlayerExperience,
             offer == null ? 0 : offer.specialPrice,
             offer == null ? 0 : offer.demandBonus,
             offer == null ? 0.0f : offer.priceMultiplier,
             offer == null ? 0 : offer.merchantExperience);
    }

    public TradedItem getFirstBuyItem() { return firstBuyItem; }
    public ItemStack getOriginalFirstBuyItem() { return firstBuyItem.itemStack(); }
    public int getFirstBuyItemCount(TradedItem item) { return item == null ? 0 : item.count(); }
    public Optional<ItemStack> getSecondBuyItem() { return secondBuyItem.map(ItemStack::copy); }
    public ItemStack getSellItem() { return sellItem.copy(); }
    public ItemStack copySellItem() { return sellItem.copy(); }
    public ItemStack getDisplayedFirstBuyItem() { return firstBuyItem.itemStack(); }
    public ItemStack getDisplayedSecondBuyItem() { return secondBuyItem.map(ItemStack::copy).orElse(ItemStack.EMPTY); }
    public int getUses() { return uses; }
    public int getMaxUses() { return maxUses; }
    public int getMerchantExperience() { return merchantExperience; }
    public int getDemandBonus() { return demandBonus; }
    public int getSpecialPrice() { return specialPrice; }
    public float getPriceMultiplier() { return priceMultiplier; }
    public boolean shouldRewardPlayerExperience() { return rewardingPlayerExperience; }
    public boolean isDisabled() { return uses >= maxUses; }
    public boolean hasBeenUsed() { return uses > 0; }
    public void use() { uses++; }
    public void disable() { uses = maxUses; }
    public void resetUses() { uses = 0; }
    public void setSpecialPrice(int value) { specialPrice = value; }
    public void increaseSpecialPrice(int value) { specialPrice += value; }
    public void clearSpecialPrice() { specialPrice = 0; }
    public void updateDemandBonus() { demandBonus = Math.max(0, demandBonus); }
    public boolean matchesBuyItems(ItemStack first, ItemStack second) {
        return firstBuyItem.matches(first) && (secondBuyItem.isEmpty() || secondBuyItem.get().isEmpty() ||
            (second != null && ItemStack.canCombine(secondBuyItem.get(), second) && second.getCount() >= secondBuyItem.get().getCount()));
    }
    public boolean depleteBuyItems(ItemStack first, ItemStack second) {
        if (!matchesBuyItems(first, second)) return false;
        first.decrement(firstBuyItem.count());
        secondBuyItem.ifPresent(item -> { if (second != null) second.decrement(item.getCount()); });
        return true;
    }
    public TradeOffer copy() { return new TradeOffer(this); }
    public static TradeOffer read(RegistryByteBuf buf) { return null; }
    public static void write(RegistryByteBuf buf, TradeOffer offer) { }
}
