package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.Collection;
import java.util.List;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.Registries;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.Identifier;

/** Registry-backed reference argument used by entity and feature commands. */
public final class RegistryEntryReferenceArgumentType<T>
        implements ArgumentType<RegistryEntry.Reference<T>> {
    private static final Collection<String> EXAMPLES = List.of("minecraft:pig", "minecraft:zombie");
    private final RegistryKey<?> registryRef;
    @SuppressWarnings("unused") private final CommandRegistryAccess registryAccess;

    public RegistryEntryReferenceArgumentType(CommandRegistryAccess registryAccess, RegistryKey<?> registryRef) {
        this.registryAccess = registryAccess;
        this.registryRef = registryRef;
    }

    public static RegistryEntryReferenceArgumentType<?> registryEntry(
            CommandRegistryAccess registryAccess, RegistryKey<?> registryRef) {
        return new RegistryEntryReferenceArgumentType<>(registryAccess, registryRef);
    }

    @Override public RegistryEntry.Reference<T> parse(StringReader reader) throws CommandSyntaxException {
        String value = reader.readString();
        Identifier id = Identifier.tryParse(value);
        if (id == null) throw new CommandSyntaxException("Invalid registry entry: " + value, reader, reader.getCursor());
        RegistryEntry.Reference<T> entry = reference(id);
        if (entry == null) throw new CommandSyntaxException("Unknown registry entry: " + value, reader, reader.getCursor());
        return entry;
    }

    @SuppressWarnings("unchecked")
    private RegistryEntry.Reference<T> reference(Identifier id) {
        Registry<?> registry = Registries.byKey(registryRef);
        if (registry == null) return null;
        Object value = registry.get(id);
        if (value == null) return null;
        RegistryKey<T> key = RegistryKey.of((RegistryKey<?>) registryRef, id);
        return new RegistryEntry.Reference<>((T) value, key);
    }

    public static RegistryEntry.Reference<?> getRegistryEntry(CommandContext<?> context, String name,
                                                               RegistryKey<?> registryRef) {
        RegistryEntry.Reference<?> value = context == null ? null
            : context.getArgument(name, RegistryEntry.Reference.class);
        if (value != null) return value;
        Object raw = context == null ? null : context.getArgumentRaw(name);
        if (raw instanceof Identifier id) {
            Registry<?> registry = Registries.byKey(registryRef);
            Object entryValue = registry == null ? null : registry.get(id);
            if (entryValue != null) return new RegistryEntry.Reference<>(entryValue,
                RegistryKey.of((RegistryKey<?>) registryRef, id));
        }
        throw new IllegalArgumentException("argument is not a registry entry: " + name);
    }

    public static RegistryEntry.Reference<?> getEntityType(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.ENTITY_TYPE);
    }
    public static RegistryEntry.Reference<?> getSummonableEntityType(CommandContext<?> context, String name) {
        return getEntityType(context, name);
    }
    public static RegistryEntry.Reference<?> getConfiguredFeature(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.FEATURE);
    }
    public static RegistryEntry.Reference<?> getStatusEffect(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.STATUS_EFFECT);
    }
    public static RegistryEntry.Reference<?> getEntityAttribute(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.ATTRIBUTE);
    }
    public static RegistryEntry.Reference<?> getEnchantment(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.ENCHANTMENT);
    }
    public static RegistryEntry.Reference<?> getStructure(CommandContext<?> context, String name) {
        return getRegistryEntry(context, name, net.minecraft.registry.RegistryKeys.STRUCTURE);
    }

    @Override public <S> java.util.concurrent.CompletableFuture<com.mojang.brigadier.suggestion.Suggestions> listSuggestions(
            CommandContext<S> context, com.mojang.brigadier.suggestion.SuggestionsBuilder builder) {
        return java.util.concurrent.CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }
}
