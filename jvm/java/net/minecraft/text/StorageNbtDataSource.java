package net.minecraft.text;

import java.util.stream.Stream;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.util.Identifier;

/** Storage NBT text source used by the 1.21.4 text ABI. */
public record StorageNbtDataSource(Identifier id) implements NbtDataSource {
    public static final Type TYPE = new Type("storage", null);
    public static final com.mojang.serialization.MapCodec<StorageNbtDataSource> CODEC = null;

    @Override public Stream<Text> get(ServerCommandSource source) { return Stream.empty(); }
    @Override public Type getType() { return TYPE; }
}
