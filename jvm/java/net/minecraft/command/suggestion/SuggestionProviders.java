package net.minecraft.command.suggestion;

import com.mojang.brigadier.Message;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.suggestion.SuggestionProvider;
import com.mojang.brigadier.suggestion.Suggestions;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.command.CommandSource;
import net.minecraft.entity.EntityType;
import net.minecraft.util.Identifier;

/** Vanilla suggestion-provider registry and the provider used by summon commands. */
public final class SuggestionProviders {
    private SuggestionProviders() { }

    private static final Map<Identifier, SuggestionProvider<?>> REGISTRY = new ConcurrentHashMap<>();

    public static final Identifier ASK_SERVER_NAME = Identifier.ofVanilla("ask_server");
    public static final SuggestionProvider<CommandSource> ASK_SERVER = empty();
    public static final SuggestionProvider<CommandSource> AVAILABLE_SOUNDS = empty();
    public static final SuggestionProvider<CommandSource> SUMMONABLE_ENTITIES =
        (context, builder) -> {
            if (context != null && context.getSource() != null) {
                builder.suggest("minecraft:pig");
                builder.suggest("minecraft:zombie");
            }
            return CompletableFuture.completedFuture(builder.build());
        };

    static {
        register(Identifier.ofVanilla("ask_server"), ASK_SERVER);
        register(Identifier.ofVanilla("available_sounds"), AVAILABLE_SOUNDS);
        register(Identifier.ofVanilla("summonable_entities"), SUMMONABLE_ENTITIES);
    }

    private static <S> SuggestionProvider<S> empty() {
        return (context, builder) -> CompletableFuture.completedFuture(builder.build());
    }

    public static <S> SuggestionProvider<S> byId(Identifier id) {
        @SuppressWarnings("unchecked") SuggestionProvider<S> value = (SuggestionProvider<S>) REGISTRY.get(id);
        return value;
    }

    public static <S> SuggestionProvider<S> register(Identifier id, SuggestionProvider<S> provider) {
        if (id == null || provider == null) throw new NullPointerException("id/provider");
        REGISTRY.put(id, provider);
        return provider;
    }

    public static <S> SuggestionProvider<S> getLocalProvider(SuggestionProvider<S> provider) { return provider; }
    public static Identifier computeId(SuggestionProvider<?> provider) {
        if (provider == null) return null;
        for (Map.Entry<Identifier, SuggestionProvider<?>> entry : REGISTRY.entrySet())
            if (entry.getValue() == provider) return entry.getKey();
        return null;
    }

    public static Message method_10023(EntityType<?> entityType) {
        String value = entityType == null ? "" : entityType.toString();
        return () -> value;
    }

    public static boolean method_45916(CommandContext<?> context, EntityType<?> entityType) { return true; }

    private static <S> CompletableFuture<Suggestions> method_10028(CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }

    private static <S> CompletableFuture<Suggestions> method_10029(CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }
}
