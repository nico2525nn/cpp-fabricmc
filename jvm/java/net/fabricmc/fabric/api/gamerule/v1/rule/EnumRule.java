package net.fabricmc.fabric.api.gamerule.v1.rule;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.GameRules;

/** A finite enum-valued Fabric game rule. */
public final class EnumRule<E extends Enum<E>> extends GameRules.Rule<EnumRule<E>> {
    private E value;
    private final List<E> supportedValues;

    public EnumRule(GameRules.Type<EnumRule<E>> type, E initialValue, E[] supportedValues) {
        this(type, initialValue, Arrays.asList(
            Objects.requireNonNull(supportedValues, "supportedValues")));
    }

    public EnumRule(GameRules.Type<EnumRule<E>> type, E initialValue,
                    Collection<E> supportedValues) {
        super(type);
        this.supportedValues = List.copyOf(Objects.requireNonNull(supportedValues, "supportedValues"));
        if (this.supportedValues.isEmpty() || initialValue == null || !this.supportedValues.contains(initialValue))
            throw new IllegalArgumentException("initial enum value is not supported");
        this.value = initialValue;
    }

    @Override public String serialize() { return value.name().toLowerCase(Locale.ROOT); }
    @Override public int getCommandResult() { return value.ordinal(); }
    /** Intermediary names retained for the official Fabric 1.21.4 ABI. */
    public String method_20779() { return serialize(); }
    public int method_20781() { return getCommandResult(); }
    @Override public void setValue(EnumRule<E> rule, MinecraftServer server) {
        if (rule != null) set(rule.value, server);
    }
    @SuppressWarnings("unchecked")
    public Class<E> getEnumClass() { return (Class<E>) value.getDeclaringClass(); }
    @Override public String toString() { return serialize(); }
    public E get() { return value; }
    public void cycle() {
        int next = (supportedValues.indexOf(value) + 1) % supportedValues.size();
        value = supportedValues.get(next);
    }
    public boolean supports(E candidate) { return candidate != null && supportedValues.contains(candidate); }
    public void set(E value, MinecraftServer server) throws IllegalArgumentException {
        if (!supports(value)) throw new IllegalArgumentException("unsupported enum value: " + value);
        if (this.value == value) return;
        this.value = value;
        changed(server);
    }
    @Override public void deserialize(String input) {
        if (input == null) throw new IllegalArgumentException("enum value is null");
        for (E candidate : supportedValues) {
            if (candidate.name().equalsIgnoreCase(input)) {
                value = candidate;
                return;
            }
        }
        throw new IllegalArgumentException("unsupported enum value: " + input);
    }
    public void method_27337(GameRules.Rule<?> rule, MinecraftServer server) {
        if (rule instanceof EnumRule<?> other) {
            @SuppressWarnings("unchecked") E candidate = (E) other.get();
            set(candidate, server);
        }
    }
    @Override public EnumRule<E> copy() {
        return new EnumRule<>(type, value, supportedValues);
    }
}
