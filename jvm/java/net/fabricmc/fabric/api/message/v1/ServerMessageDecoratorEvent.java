package net.fabricmc.fabric.api.message.v1;

import java.util.Objects;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.network.message.MessageDecorator;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

/** Ordered server chat decoration phases from Fabric Message API v1. */
public final class ServerMessageDecoratorEvent {
    public static final Identifier CONTENT_PHASE = Identifier.of("fabric", "content");
    public static final Identifier STYLING_PHASE = Identifier.of("fabric", "styling");
    public static final Event<MessageDecorator> EVENT = EventFactory.createWithPhases(
        MessageDecorator.class,
        callbacks -> (sender, message) -> {
            Text current = message;
            for (MessageDecorator callback : callbacks) {
                current = Objects.requireNonNull(callback.decorate(sender, current),
                    "message decorator " + callback.getClass().getName() + " returned null");
            }
            return current;
        }, CONTENT_PHASE, Event.DEFAULT_PHASE, STYLING_PHASE);

    private ServerMessageDecoratorEvent() { }
}
