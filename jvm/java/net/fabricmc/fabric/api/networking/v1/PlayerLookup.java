package net.fabricmc.fabric.api.networking.v1;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Vec3d;

/** Player lookup helpers with the same conservative server-side semantics as Fabric. */
public final class PlayerLookup {
    private PlayerLookup() { }

    public static Collection<ServerPlayerEntity> all(MinecraftServer server) {
        return server == null ? List.of() : List.copyOf(server.getPlayerManager().getPlayerList());
    }

    public static Collection<ServerPlayerEntity> world(ServerWorld world) {
        return world == null ? List.of() : List.copyOf(world.getPlayers());
    }

    public static Collection<ServerPlayerEntity> tracking(ServerWorld world, ChunkPos chunkPos) {
        if (world == null || chunkPos == null) return List.of();
        List<ServerPlayerEntity> result = new ArrayList<>();
        for (ServerPlayerEntity player : world.getPlayers()) {
            ChunkPos playerChunk = player.getChunkPos();
            if (Math.abs(playerChunk.x - chunkPos.x) <= 1 && Math.abs(playerChunk.z - chunkPos.z) <= 1)
                result.add(player);
        }
        return List.copyOf(result);
    }

    public static Collection<ServerPlayerEntity> tracking(Entity entity) {
        if (entity == null || !(entity.getWorld() instanceof ServerWorld world)) return List.of();
        return around(world, entity.getPos(), 128.0);
    }

    public static Collection<ServerPlayerEntity> tracking(BlockEntity blockEntity) {
        if (blockEntity == null || !(blockEntity.getWorld() instanceof ServerWorld world)) return List.of();
        return around(world, Vec3d.ofCenter(blockEntity.getPos()), 128.0);
    }

    public static Collection<ServerPlayerEntity> tracking(ServerWorld world, BlockPos pos) {
        return world == null || pos == null ? List.of() : around(world, Vec3d.ofCenter(pos), 128.0);
    }

    public static Collection<ServerPlayerEntity> around(ServerWorld world, Vec3d position, double radius) {
        if (world == null || position == null || radius < 0.0) return List.of();
        double squaredRadius = radius * radius;
        List<ServerPlayerEntity> result = new ArrayList<>();
        for (ServerPlayerEntity player : world.getPlayers())
            if (player.getPos().squaredDistanceTo(position) <= squaredRadius) result.add(player);
        return List.copyOf(result);
    }

    /** Official Fabric 1.21.4 overload: measure from the integer vector. */
    public static Collection<ServerPlayerEntity> around(ServerWorld world, net.minecraft.util.math.Vec3i position,
                                                        double radius) {
        if (position == null) return List.of();
        return around(world, new Vec3d(position.getX(), position.getY(), position.getZ()), radius);
    }

    public static Collection<ServerPlayerEntity> around(ServerWorld world, Box box, double radius) {
        if (world == null || box == null || radius < 0.0) return List.of();
        Box expanded = box.expand(radius);
        List<ServerPlayerEntity> result = new ArrayList<>();
        for (ServerPlayerEntity player : world.getPlayers())
            if (expanded.contains(player.getPos())) result.add(player);
        return List.copyOf(result);
    }
}
