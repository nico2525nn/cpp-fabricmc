package net.minecraft.server.command;

import java.util.Objects;
import java.util.function.Consumer;
import net.minecraft.command.CommandSource;
import net.minecraft.command.EntityAnchor;
import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.text.Text;
import net.minecraft.util.math.Vec2f;
import net.minecraft.util.math.Vec3d;

public class ServerCommandSource implements CommandSource {
    private final Consumer<Text> feedbackConsumer;
    private final Vec3d position;
    private final Vec2f rotation;
    private final ServerWorld world;
    private final int level;
    private final String name;
    private final Text displayName;
    private final MinecraftServer server;
    private final Entity entity;
    private final boolean silent;
    private final ServerPlayerEntity player;
    public ServerCommandSource(ServerPlayerEntity player, MinecraftServer server) {
        this(player == null ? message -> { } : player::sendMessage,
             player == null ? Vec3d.ZERO : player.getPos(), Vec2f.ZERO,
             player == null ? null : player.getServerWorld(), 4,
             player == null ? "Console" : player.getName().getString(),
             player == null ? Text.literal("Console") : player.getName(), server, player, false);
    }
    public ServerCommandSource(Consumer<Text> feedbackConsumer, Vec3d position, Vec2f rotation,
                               ServerWorld world, int level, String name, Text displayName,
                               MinecraftServer server, Entity entity) {
        this(feedbackConsumer, position, rotation, world, level, name, displayName, server, entity, false);
    }
    private ServerCommandSource(Consumer<Text> feedbackConsumer, Vec3d position, Vec2f rotation,
                                ServerWorld world, int level, String name, Text displayName,
                                MinecraftServer server, Entity entity, boolean silent) {
        this.feedbackConsumer = feedbackConsumer == null ? message -> { } : feedbackConsumer;
        this.position = position == null ? Vec3d.ZERO : position; this.rotation = rotation == null ? Vec2f.ZERO : rotation;
        this.world = world; this.level = Math.max(0, level); this.name = name == null ? "" : name;
        this.displayName = displayName == null ? Text.literal(this.name) : displayName; this.server = server; this.entity = entity; this.silent = silent;
        this.player = entity instanceof ServerPlayerEntity value ? value : null;
    }
    public ServerPlayerEntity getPlayer() { return player; }
    public ServerPlayerEntity getPlayerOrThrow() { if (player == null) throw new IllegalStateException("source is not a player"); return player; }
    public Entity getEntity() { return entity; }
    public MinecraftServer getServer() { return server; }
    public ServerWorld getWorld() { return world; }
    public ServerWorld getServerWorld() { return world; }
    public Vec3d getPosition() { return position; }
    public Vec2f getRotation() { return rotation; }
    public EntityAnchor getEntityAnchor() { return EntityAnchor.FEET; }
    public int getLevel() { return level; }
    public String getName() { return name; }
    public Text getDisplayName() { return displayName; }
    public boolean isExecutedByPlayer() { return player != null; }
    public boolean hasPermissionLevel(int requiredLevel) { return level >= requiredLevel; }
    public boolean isSilent() { return silent; }
    @Override public void sendMessage(Text message) { if (!silent && message != null) feedbackConsumer.accept(message); }
    public void sendFeedback(Consumer<Text> messageConsumer, boolean broadcastToOps) { if (!silent && messageConsumer != null) messageConsumer.accept(displayName); }
    public void sendFeedback(java.util.function.Supplier<Text> messageSupplier, boolean broadcastToOps) { if (!silent && messageSupplier != null) sendMessage(messageSupplier.get()); }
    public void sendError(Text message) { sendMessage(message); }
    public ServerCommandSource withLevel(int value) { return copy(position, rotation, world, value, name, displayName, entity, silent); }
    public ServerCommandSource withEntity(Entity value) { return copy(value == null ? position : value.getPos(), rotation, value == null ? world : (value.getWorld() instanceof ServerWorld w ? w : world), level, value == null ? name : value.getName().getString(), value == null ? displayName : value.getDisplayName(), value, silent); }
    public ServerCommandSource withPosition(Vec3d value) { return copy(value, rotation, world, level, name, displayName, entity, silent); }
    public ServerCommandSource withRotation(Vec2f value) { return copy(position, value, world, level, name, displayName, entity, silent); }
    public ServerCommandSource withWorld(ServerWorld value) { return copy(position, rotation, value, level, name, displayName, entity, silent); }
    public ServerCommandSource withSilent() { return copy(position, rotation, world, level, name, displayName, entity, true); }
    public ServerCommandSource withLookingAt(Entity value, EntityAnchor anchor) { return withEntity(value); }
    private ServerCommandSource copy(Vec3d position, Vec2f rotation, ServerWorld world, int level, String name, Text displayName, Entity entity, boolean silent) {
        return new ServerCommandSource(feedbackConsumer, position, rotation, world, level, name, displayName, server, entity, silent);
    }
}
