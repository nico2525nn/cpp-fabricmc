package net.minecraft.command;

import net.minecraft.text.Text;

public interface CommandSource {
    default void sendMessage(Text message) { }
    default boolean acceptsSuccess() { return true; }
    default boolean acceptsFailure() { return true; }
    default boolean shouldReceiveFeedback() { return true; }
}
