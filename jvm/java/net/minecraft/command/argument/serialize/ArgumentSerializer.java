package net.minecraft.command.argument.serialize;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.ArgumentType;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.network.PacketByteBuf;

/** 1.21.4 argument serializer contract used by Fabric's command API. */
public interface ArgumentSerializer<A extends ArgumentType<?>, T extends ArgumentSerializer.ArgumentTypeProperties<A>> {
    T getArgumentTypeProperties(A argumentType);
    T fromPacket(PacketByteBuf buf);
    void writeJson(T properties, JsonObject json);
    void writePacket(T properties, PacketByteBuf buf);

    interface ArgumentTypeProperties<A extends ArgumentType<?>> {
        A createType(CommandRegistryAccess commandRegistryAccess);
        ArgumentSerializer<A, ?> getSerializer();
    }
}
