package net.minecraft.village;

import java.util.Collection;
import java.util.List;
import java.util.ArrayList;
import net.minecraft.item.ItemStack;

/** Ordered collection of merchant offers. */
public class TradeOfferList extends ArrayList<TradeOffer> {
    public static final Object CODEC = new Object();
    public static final Object PACKET_CODEC = new Object();

    public TradeOfferList(int size) { super(Math.max(0, size)); }
    public TradeOfferList(Collection<? extends TradeOffer> offers) {
        super(offers == null ? List.of() : offers);
    }
    public TradeOfferList() { super(); }

    public TradeOfferList copy() {
        TradeOfferList copy = new TradeOfferList();
        for (TradeOffer offer : this) copy.add(offer == null ? null : offer.copy());
        return copy;
    }

    public TradeOffer getValidOffer(ItemStack firstBuyItem, ItemStack secondBuyItem, int index) {
        if (index >= 0 && index < size()) {
            TradeOffer offer = get(index);
            if (offer != null && offer.matchesBuyItems(firstBuyItem, secondBuyItem)) return offer;
        }
        for (TradeOffer offer : this)
            if (offer != null && !offer.isDisabled() && offer.matchesBuyItems(firstBuyItem, secondBuyItem)) return offer;
        return null;
    }
}
