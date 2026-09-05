package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.UUID;
import net.minecraft.entity.Entity;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

/**
 * Server-side entity selector argument covering the selectors used by common
 * commands and mods.  Selector options are intentionally bounded to the
 * 1.21.4-compatible selector forms; the resolved entities remain native-backed.
 */
public final class EntityArgumentType implements ArgumentType<EntityArgumentType.EntitySelector> {
    private final boolean single;
    private final boolean playersOnly;

    private EntityArgumentType(boolean single, boolean playersOnly) {
        this.single = single; this.playersOnly = playersOnly;
    }

    public static EntityArgumentType entity() { return new EntityArgumentType(true, false); }
    public static EntityArgumentType entities() { return new EntityArgumentType(false, false); }
    public static EntityArgumentType player() { return new EntityArgumentType(true, true); }
    public static EntityArgumentType players() { return new EntityArgumentType(false, true); }

    public static Entity getEntity(CommandContext<ServerCommandSource> context, String name)
            throws CommandSyntaxException {
        EntitySelector selector = selector(context, name);
        List<? extends Entity> result = selector.getEntities(context == null ? null : context.getSource());
        if (result.size() != 1)
            throw new CommandSyntaxException("Expected one entity, found " + result.size());
        return result.get(0);
    }

    public static Collection<? extends Entity> getEntities(CommandContext<ServerCommandSource> context,
                                                            String name) throws CommandSyntaxException {
        EntitySelector selector = selector(context, name);
        List<? extends Entity> result = selector.getEntities(context == null ? null : context.getSource());
        if (result.isEmpty()) throw new CommandSyntaxException("No entity was found");
        return result;
    }

    public static ServerPlayerEntity getPlayer(CommandContext<ServerCommandSource> context, String name)
            throws CommandSyntaxException {
        List<ServerPlayerEntity> result = playersFrom(getEntities(context, name));
        if (result.size() != 1)
            throw new CommandSyntaxException("Expected one player, found " + result.size());
        return result.get(0);
    }

    public static Collection<ServerPlayerEntity> getPlayers(CommandContext<ServerCommandSource> context,
                                                             String name) throws CommandSyntaxException {
        List<ServerPlayerEntity> result = playersFrom(getEntities(context, name));
        if (result.isEmpty()) throw new CommandSyntaxException("No player was found");
        return List.copyOf(result);
    }

    private static EntitySelector selector(CommandContext<ServerCommandSource> context, String name)
            throws CommandSyntaxException {
        if (context == null) throw new CommandSyntaxException("Missing command context");
        EntitySelector selector = context.getArgument(name, EntitySelector.class);
        if (selector == null) throw new CommandSyntaxException("Missing entity argument: " + name);
        return selector;
    }

    private static List<ServerPlayerEntity> playersFrom(Collection<? extends Entity> entities) {
        List<ServerPlayerEntity> result = new ArrayList<>();
        if (entities != null) for (Entity entity : entities)
            if (entity instanceof ServerPlayerEntity player) result.add(player);
        return result;
    }

    @Override public EntitySelector parse(StringReader reader) throws CommandSyntaxException {
        String token = reader.readString();
        if (token.isEmpty()) throw new CommandSyntaxException("Expected entity", reader, reader.getCursor());
        if (token.startsWith("@")) {
            if (!(token.equals("@s") || token.equals("@p") || token.equals("@a")
                    || token.equals("@e") || token.equals("@r")))
                throw new CommandSyntaxException("Unknown entity selector: " + token, reader, reader.getCursor());
            if (playersOnly && token.equals("@e"))
                throw new CommandSyntaxException("Selector does not select players", reader, reader.getCursor());
            boolean selectorSingle = token.equals("@s") || token.equals("@p") || token.equals("@r");
            if (single && !selectorSingle)
                throw new CommandSyntaxException("Selector matched multiple entities", reader, reader.getCursor());
            return new EntitySelector(token, selectorSingle, playersOnly);
        }
        return new EntitySelector(token, true, playersOnly);
    }

    @Override public Collection<String> getExamples() { return List.of("@s", "@p", "@a", "Steve"); }

    public boolean isSingle() { return single; }
    public boolean includesNonPlayers() { return !playersOnly; }

    public static final class EntitySelector {
        private final String token;
        private final boolean single;
        private final boolean playersOnly;

        private EntitySelector(String token, boolean single, boolean playersOnly) {
            this.token = token; this.single = single; this.playersOnly = playersOnly;
        }

        public boolean isSenderOnly() { return token.equals("@s"); }
        public boolean isSingle() { return single; }
        public String getSelector() { return token; }

        public List<? extends Entity> getEntities(ServerCommandSource source) {
            LinkedHashSet<Entity> result = new LinkedHashSet<>();
            if (source == null) return List.of();
            if (token.equals("@s")) {
                addIfAllowed(result, source.getEntity());
            } else if (token.equals("@p") || token.equals("@r")) {
                List<ServerPlayerEntity> players = onlinePlayers(source);
                if (!players.isEmpty()) addIfAllowed(result, players.get(0));
            } else if (token.equals("@a")) {
                for (ServerPlayerEntity player : onlinePlayers(source)) addIfAllowed(result, player);
            } else if (token.equals("@e")) {
                ServerWorld world = source.getWorld();
                if (world != null) for (Entity entity : world.getEntities()) addIfAllowed(result, entity);
                for (ServerPlayerEntity player : onlinePlayers(source)) addIfAllowed(result, player);
            } else {
                ServerPlayerEntity player = source.getServer() == null ? null
                    : source.getServer().getPlayerManager().getPlayer(token);
                if (player == null) {
                    try {
                        UUID uuid = UUID.fromString(token);
                        player = source.getServer() == null ? null
                            : source.getServer().getPlayerManager().getPlayer(uuid);
                    } catch (IllegalArgumentException ignored) { }
                }
                addIfAllowed(result, player);
            }
            if (single && result.size() > 1) return List.of(result.iterator().next());
            return List.copyOf(result);
        }

        private List<ServerPlayerEntity> onlinePlayers(ServerCommandSource source) {
            return source.getServer() == null ? List.of() : source.getServer().getPlayerManager().getPlayerList();
        }

        private void addIfAllowed(Collection<Entity> result, Entity entity) {
            if (entity != null && (!playersOnly || entity instanceof ServerPlayerEntity)) result.add(entity);
        }
    }
}
