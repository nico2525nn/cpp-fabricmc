package net.minecraft.text;

import java.util.stream.Stream;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.util.math.BlockPos;

/** Block-position NBT text source used by placeholder and command libraries. */
public record BlockNbtDataSource(String rawPos, BlockPos pos) implements NbtDataSource {
    public static final Type TYPE = new Type("block", null);
    public static final com.mojang.serialization.MapCodec<BlockNbtDataSource> CODEC = null;

    public BlockNbtDataSource(String rawPos) {
        this(rawPos, new BlockPos(0, 0, 0));
    }
    public BlockNbtDataSource {
        rawPos = rawPos == null ? "0 0 0" : rawPos;
        pos = pos == null ? new BlockPos(0, 0, 0) : pos;
    }
    @Override public Stream<Text> get(ServerCommandSource source) { return Stream.empty(); }
    @Override public Type getType() { return TYPE; }
    public static BlockPos parsePos(String string) { return new BlockPos(0, 0, 0); }
}
