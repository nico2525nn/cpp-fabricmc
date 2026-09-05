package net.minecraft.registry;

import net.minecraft.world.World;
import net.minecraft.util.Identifier;

public final class RegistryKeys {
    private RegistryKeys() {}
    @SuppressWarnings("rawtypes")
    public static final RegistryKey<Registry<World>> WORLD =
        RegistryKey.of(new RegistryKey<>(Identifier.of("minecraft", "root")), Identifier.of("minecraft", "world"));
}
