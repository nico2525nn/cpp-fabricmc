package net.minecraft.stat;

import java.util.HashMap;
import java.util.Map;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.registry.Registry;
import net.minecraft.text.Text;

/** Registry-backed statistic type compatible with the 1.21.4 named API. */
public class StatType<T> {
    private final Text name;
    private final Map<T, Stat<T>> stats = new HashMap<>();
    private final Registry<T> registry;
    private final PacketCodec<RegistryByteBuf, Stat<T>> packetCodec =
        PacketCodec.ofLegacy((buffer, stat) -> {}, buffer -> null);

    public StatType(Registry<T> registry, Text name) {
        this.registry = registry;
        this.name = name == null ? Text.empty() : name;
    }

    public Stat<T> getOrCreateStat(T key, StatFormatter formatter) {
        return stats.computeIfAbsent(key, value -> new Stat<>(this, value, formatter));
    }

    public Stat<T> getOrCreateStat(T key) {
        return getOrCreateStat(key, StatFormatter.DEFAULT);
    }

    public Registry<T> getRegistry() { return registry; }
    public Text getName() { return name; }
    public PacketCodec<RegistryByteBuf, Stat<T>> getPacketCodec() { return packetCodec; }
    public boolean hasStat(T key) { return stats.containsKey(key); }

    /** Named API helper retained for callers that supply formatter first. */
    public Stat<T> method_14961(StatFormatter formatter, T value) {
        return getOrCreateStat(value, formatter);
    }
}
