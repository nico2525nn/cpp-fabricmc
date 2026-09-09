package net.minecraft.network.message;

import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.text.Text;

/** 1.21.4 server-side chat/message decoration contract. */
@FunctionalInterface
public interface MessageDecorator {
    MessageDecorator NOOP = (sender, message) -> message;

    Text decorate(ServerPlayerEntity sender, Text message);

    default Text method_44303(ServerPlayerEntity sender, Text message) {
        return decorate(sender, message);
    }
}
