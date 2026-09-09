package net.fabricmc.fabric.api.command.v2;

import com.mojang.brigadier.arguments.ArgumentType;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.command.argument.serialize.ArgumentSerializer;
import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.util.Identifier;

/** Stateful custom command-argument serializer registry. */
public final class ArgumentTypeRegistry {
    private static final Map<Class<?>, ArgumentSerializer<?, ?>> SERIALIZERS = new ConcurrentHashMap<>();
    private static final Map<Identifier, ArgumentSerializer<?, ?>> IDS = new ConcurrentHashMap<>();

    private ArgumentTypeRegistry() { }

    public static <A extends ArgumentType<?>, T extends ArgumentSerializer.ArgumentTypeProperties<A>>
            void registerArgumentType(Identifier id, Class<? extends A> argumentClass,
                                      ArgumentSerializer<A, T> serializer) {
        if (id == null || argumentClass == null || serializer == null)
            throw new NullPointerException("id/argumentClass/serializer");
        SERIALIZERS.put(argumentClass, serializer);
        IDS.put(id, serializer);
        @SuppressWarnings("unchecked") Registry<Object> registry = (Registry<Object>) (Registry<?>) Registries.COMMAND_ARGUMENT_TYPE;
        if (!registry.containsId(id)) Registry.register(registry, id, serializer);
    }

    public static ArgumentSerializer<?, ?> getSerializer(Class<?> argumentClass) {
        return SERIALIZERS.get(argumentClass);
    }

    public static ArgumentSerializer<?, ?> getSerializer(Identifier id) { return IDS.get(id); }
}
