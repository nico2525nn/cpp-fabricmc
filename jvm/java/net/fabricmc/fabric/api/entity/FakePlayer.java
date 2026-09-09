package net.fabricmc.fabric.api.entity;

import com.mojang.authlib.GameProfile;
import java.util.Map;
import java.util.Objects;
import java.util.OptionalInt;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.entity.SignBlockEntity;
import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.passive.AbstractHorseEntity;
import net.minecraft.inventory.Inventory;
import net.minecraft.network.packet.c2s.common.SyncedClientOptions;
import net.minecraft.scoreboard.Team;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.stat.Stat;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.TeleportTarget;

/**
 * Server-only player used by block and entity interaction code that does not
 * originate from a real client.
 *
 * <p>Fabric keeps one instance per world/profile pair.  The native server is
 * still the authority for real players; a fake player is deliberately a
 * Java-side inert object, matching Fabric's no-op interaction overrides.</p>
 */
public class FakePlayer extends ServerPlayerEntity {
    public static final UUID DEFAULT_UUID = UUID.fromString(
            "41C82C87-7AfB-4024-BA57-13D2C99CAE77");
    private static final GameProfile DEFAULT_PROFILE =
            new GameProfile(DEFAULT_UUID, "[Minecraft]");
    private static final Map<FakePlayerKey, FakePlayer> FAKE_PLAYER_MAP =
            new ConcurrentHashMap<>();

    public static FakePlayer get(ServerWorld world) {
        return get(world, DEFAULT_PROFILE);
    }

    public static FakePlayer get(ServerWorld world, GameProfile profile) {
        Objects.requireNonNull(world, "World may not be null.");
        Objects.requireNonNull(profile, "Game profile may not be null.");
        return FAKE_PLAYER_MAP.computeIfAbsent(
                new FakePlayerKey(world, profile), key -> new FakePlayer(key.world, key.profile));
    }

    protected FakePlayer(ServerWorld world, GameProfile profile) {
        super(world == null ? null : world.getServer(), world, profile, SyncedClientOptions.DEFAULT);
    }

    /** Fake players must not advance a real player tick or send packets. */
    @Override public void tick() { }

    /** Client settings are meaningless without a client connection. */
    public void setClientOptions(SyncedClientOptions options) { }

    /*
     * FakePlayer itself is a Fabric API class, so Yarn does not remap the
     * intermediary-named overrides declared by the upstream class.  Keep
     * those descriptors alongside the named source methods; a mod compiled
     * against the official 1.21.4 API can therefore link before/after the
     * normal intermediary -> named remap.
     */
    public void method_14213(SyncedClientOptions options) { setClientOptions(options); }

    /** Statistics are not recorded for an artificial player. */
    @Override public void increaseStat(Stat<?> stat, int amount) { }
    public void resetStat(Stat<?> stat) { }
    public void method_7266(Stat<?> stat) { resetStat(stat); }
    public void method_7342(Stat<?> stat, int amount) { increaseStat(stat, amount); }

    /** A fake player is immune to world damage, as in Fabric's implementation. */
    public boolean isInvulnerableTo(ServerWorld world, DamageSource source) { return true; }
    public boolean method_5679(ServerWorld world, DamageSource source) {
        return isInvulnerableTo(world, source);
    }

    /** Fake players do not belong to scoreboard teams. */
    public Team getScoreboardTeam() { return null; }
    public Team method_5781() { return getScoreboardTeam(); }

    /** Sleeping is a client-mediated action and is ignored. */
    @Override public void sleep(BlockPos position) { }
    public void method_18403(BlockPos position) { sleep(position); }

    /** Artificial players cannot ride entities. */
    public boolean startRiding(Entity entity, boolean force) { return false; }
    public boolean method_5873(Entity entity, boolean force) { return startRiding(entity, force); }

    /** Sign editing requires a client screen and is ignored. */
    public void openEditSignScreen(SignBlockEntity sign, boolean front) { }
    public void method_7311(SignBlockEntity sign, boolean front) { openEditSignScreen(sign, front); }

    /** No client exists to receive a horse inventory screen. */
    public void openHorseInventory(AbstractHorseEntity horse, Inventory inventory) { }
    public void method_7291(AbstractHorseEntity horse, Inventory inventory) {
        openHorseInventory(horse, inventory);
    }

    /** Screen opening is intentionally a no-op for a fake player. */
    @Override public OptionalInt openHandledScreen(net.minecraft.screen.NamedScreenHandlerFactory factory) {
        return OptionalInt.empty();
    }
    public OptionalInt method_17355(net.minecraft.screen.NamedScreenHandlerFactory factory) {
        return openHandledScreen(factory);
    }

    /**
     * Retain the vanilla dimension-change call shape while allowing the base
     * shadow implementation to return this object through its bridge method.
     */
    @Override public ServerPlayerEntity teleportTo(TeleportTarget target) {
        return super.teleportTo(target);
    }
    public Entity method_5731(TeleportTarget target) { return teleportTo(target); }
    public void method_5773() { tick(); }

    private record FakePlayerKey(ServerWorld world, GameProfile profile) { }
}
