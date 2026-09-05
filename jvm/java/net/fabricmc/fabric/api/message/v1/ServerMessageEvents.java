package net.fabricmc.fabric.api.message.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.text.Text;

/** Server chat, game-message, and command-message callbacks. */
public final class ServerMessageEvents {
    private ServerMessageEvents() { }

    @FunctionalInterface public interface AllowChatMessage {
        boolean allowChatMessage(SignedMessage message, ServerPlayerEntity sender,
                                 MessageType.Parameters params);
    }
    @FunctionalInterface public interface ChatMessage {
        void onChatMessage(SignedMessage message, ServerPlayerEntity sender,
                           MessageType.Parameters params);
    }
    @FunctionalInterface public interface AllowGameMessage {
        boolean allowGameMessage(MinecraftServer server, ServerPlayerEntity sender,
                                 Text message, boolean overlay);
    }
    @FunctionalInterface public interface GameMessage {
        void onGameMessage(MinecraftServer server, ServerPlayerEntity sender,
                           Text message, boolean overlay);
    }
    @FunctionalInterface public interface AllowCommandMessage {
        boolean allowCommandMessage(SignedMessage message, ServerPlayerEntity sender,
                                    MessageType.Parameters params);
    }
    @FunctionalInterface public interface CommandMessage {
        void onCommandMessage(SignedMessage message, ServerPlayerEntity sender,
                              MessageType.Parameters params);
    }

    public static final Event<AllowChatMessage> ALLOW_CHAT_MESSAGE = new Event<>(CppModRuntime::registerAllowChatMessage,
        AllowChatMessage.class, callbacks -> (message, sender, params) -> {
            for (AllowChatMessage callback : callbacks) if (!callback.allowChatMessage(message, sender, params)) return false;
            return true;
        });
    public static final Event<ChatMessage> CHAT_MESSAGE = new Event<>(CppModRuntime::registerChatMessage,
        ChatMessage.class, callbacks -> (message, sender, params) -> {
            for (ChatMessage callback : callbacks) callback.onChatMessage(message, sender, params);
        });
    public static final Event<AllowGameMessage> ALLOW_GAME_MESSAGE = EventFactory.createArrayBacked(
        AllowGameMessage.class, callbacks -> (server, sender, message, overlay) -> {
            for (AllowGameMessage callback : callbacks) if (!callback.allowGameMessage(server, sender, message, overlay)) return false;
            return true;
        });
    public static final Event<GameMessage> GAME_MESSAGE = EventFactory.createArrayBacked(
        GameMessage.class, callbacks -> (server, sender, message, overlay) -> {
            for (GameMessage callback : callbacks) callback.onGameMessage(server, sender, message, overlay);
        });
    public static final Event<AllowCommandMessage> ALLOW_COMMAND_MESSAGE = EventFactory.createArrayBacked(
        AllowCommandMessage.class, callbacks -> (message, sender, params) -> {
            for (AllowCommandMessage callback : callbacks) if (!callback.allowCommandMessage(message, sender, params)) return false;
            return true;
        });
    public static final Event<CommandMessage> COMMAND_MESSAGE = EventFactory.createArrayBacked(
        CommandMessage.class, callbacks -> (message, sender, params) -> {
            for (CommandMessage callback : callbacks) callback.onCommandMessage(message, sender, params);
        });

    public static void clear() {
        ALLOW_CHAT_MESSAGE.clear(); CHAT_MESSAGE.clear(); ALLOW_GAME_MESSAGE.clear();
        GAME_MESSAGE.clear(); ALLOW_COMMAND_MESSAGE.clear(); COMMAND_MESSAGE.clear();
    }
}
