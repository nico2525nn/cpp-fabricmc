package net.fabricmc.fabric.api.object.builder.v1.trade;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;
import net.minecraft.util.Identifier;
import net.minecraft.village.TradeOffers;
import net.minecraft.village.VillagerProfession;

/** In-process trade factory registry used by villager and wandering-trader mods. */
public final class TradeOfferHelper {
    private static final Map<VillagerProfession, Map<Integer, List<TradeOffers.Factory>>> VILLAGER = new ConcurrentHashMap<>();
    private static final Map<Integer, List<TradeOffers.Factory>> WANDERING = new ConcurrentHashMap<>();
    private TradeOfferHelper() { }
    public static void registerVillagerOffers(VillagerProfession profession, int level,
            Consumer<List<TradeOffers.Factory>> factories) {
        if (profession == null || factories == null) return;
        List<TradeOffers.Factory> values = new ArrayList<>(); factories.accept(values);
        VILLAGER.computeIfAbsent(profession, ignored -> new ConcurrentHashMap<>()).computeIfAbsent(level, ignored -> new ArrayList<>()).addAll(values);
    }
    public static void registerVillagerOffers(VillagerProfession profession, int level, VillagerOffersAdder adder) {
        if (profession == null || adder == null) return;
        List<TradeOffers.Factory> values = new ArrayList<>(); adder.onRegister(values, false);
        VILLAGER.computeIfAbsent(profession, ignored -> new ConcurrentHashMap<>()).computeIfAbsent(level, ignored -> new ArrayList<>()).addAll(values);
    }
    public static void registerWanderingTraderOffers(int level, Consumer<List<TradeOffers.Factory>> factory) {
        if (factory == null) return;
        List<TradeOffers.Factory> values = new ArrayList<>(); factory.accept(values);
        WANDERING.computeIfAbsent(level, ignored -> new ArrayList<>()).addAll(values);
    }
    public static synchronized void registerRebalancedWanderingTraderOffers(Consumer<WanderingTraderOffersBuilder> consumer) {
        if (consumer != null) consumer.accept(new WanderingBuilder());
    }
    public static void refreshOffers() { }
    @FunctionalInterface public interface VillagerOffersAdder { void onRegister(List<TradeOffers.Factory> factories, boolean rebalanced); }
    public interface WanderingTraderOffersBuilder {
        Identifier BUY_ITEMS_POOL = Identifier.of("minecraft", "buy_items");
        Identifier SELL_SPECIAL_ITEMS_POOL = Identifier.of("minecraft", "sell_special_items");
        Identifier SELL_COMMON_ITEMS_POOL = Identifier.of("minecraft", "sell_common_items");
        WanderingTraderOffersBuilder pool(Identifier pool, int level, TradeOffers.Factory... factories);
        default WanderingTraderOffersBuilder pool(Identifier pool, int level, Collection<? extends TradeOffers.Factory> factories) { return pool(pool, level, factories == null ? new TradeOffers.Factory[0] : factories.toArray(TradeOffers.Factory[]::new)); }
        default WanderingTraderOffersBuilder addAll(Identifier pool, Collection<? extends TradeOffers.Factory> factories) { return pool(pool, 1, factories); }
        default WanderingTraderOffersBuilder addAll(Identifier pool, TradeOffers.Factory... factories) { return pool(pool, 1, factories); }
        WanderingTraderOffersBuilder addOffersToPool(Identifier pool, TradeOffers.Factory... factories);
        default WanderingTraderOffersBuilder addOffersToPool(Identifier pool, Collection<TradeOffers.Factory> factories) { return addOffersToPool(pool, factories == null ? new TradeOffers.Factory[0] : factories.toArray(TradeOffers.Factory[]::new)); }
    }
    private static final class WanderingBuilder implements WanderingTraderOffersBuilder {
        @Override public WanderingTraderOffersBuilder pool(Identifier pool, int level, TradeOffers.Factory... factories) { return addOffersToPool(pool, factories); }
        @Override public WanderingTraderOffersBuilder addOffersToPool(Identifier pool, TradeOffers.Factory... factories) {
            if (factories != null) WANDERING.computeIfAbsent(1, ignored -> new ArrayList<>()).addAll(List.of(factories)); return this;
        }
    }
}
