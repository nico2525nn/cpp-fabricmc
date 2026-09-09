package net.minecraft.text;

import java.util.stream.Stream;
import net.minecraft.command.EntitySelector;
import net.minecraft.server.command.ServerCommandSource;

/** Entity-selector NBT text source used by the 1.21.4 text ABI. */
public record EntityNbtDataSource(String rawSelector, EntitySelector selector) implements NbtDataSource {
    public static final Type TYPE = new Type("entity", null);
    public static final com.mojang.serialization.MapCodec<EntityNbtDataSource> CODEC = null;

    public EntityNbtDataSource(String rawSelector) {
        this(rawSelector, null);
    }
    public EntityNbtDataSource {
        rawSelector = rawSelector == null ? "" : rawSelector;
    }
    @Override public Stream<Text> get(ServerCommandSource source) { return Stream.empty(); }
    @Override public Type getType() { return TYPE; }
    public static EntitySelector parseSelector(String string) { return null; }
}
