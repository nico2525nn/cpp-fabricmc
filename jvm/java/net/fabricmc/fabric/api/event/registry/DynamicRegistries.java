package net.fabricmc.fabric.api.event.registry;

import com.mojang.serialization.Codec;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryLoader;

/** Dynamic-registry registration facade. */
public final class DynamicRegistries {
    private static final List<RegistryLoader.Entry<?>> REGISTRIES = new CopyOnWriteArrayList<>();

    private DynamicRegistries() { }
    public static List<RegistryLoader.Entry<?>> getDynamicRegistries() { return List.copyOf(REGISTRIES); }

    public static <T> void register(RegistryKey<? extends Registry<T>> key, Codec<T> codec) {
        REGISTRIES.add(new RegistryLoader.Entry<>(key, codec));
    }

    public static <T> void registerSynced(RegistryKey<? extends Registry<T>> key, Codec<T> codec,
                                           SyncOption... options) {
        REGISTRIES.add(new RegistryLoader.Entry<>(key, codec));
    }

    public static <T> void registerSynced(RegistryKey<? extends Registry<T>> key,
                                           Codec<T> codec, Codec<T> networkCodec,
                                           SyncOption... options) {
        REGISTRIES.add(new RegistryLoader.Entry<>(key, codec));
    }

    public enum SyncOption { SKIP_WHEN_EMPTY }
}
