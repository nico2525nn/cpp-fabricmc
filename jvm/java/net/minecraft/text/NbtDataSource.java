package net.minecraft.text;

import java.util.stream.Stream;
import net.minecraft.server.command.ServerCommandSource;

/** 1.21.4 source of values used by NBT text components. */
public interface NbtDataSource {
    com.mojang.serialization.MapCodec<NbtDataSource> CODEC = null;

    Stream<Text> get(ServerCommandSource source);

    default Type getType() { return null; }

    /** The named codec descriptor used by the text-content dispatcher. */
    record Type(String id, com.mojang.serialization.MapCodec<? extends NbtDataSource> codec) { }
}
