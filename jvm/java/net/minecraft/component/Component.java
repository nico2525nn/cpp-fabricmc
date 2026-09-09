package net.minecraft.component;

import com.mojang.serialization.DataResult;
import java.util.Map;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;

/** A typed component value pair used by ComponentMap and ComponentChanges. */
public final class Component<T> {
    public static final PacketCodec<RegistryByteBuf, Component<?>> PACKET_CODEC =
        PacketCodec.ofLegacy((buffer, value) -> { }, buffer -> null);

    private final ComponentType<T> type;
    private final T value;

    private Component(ComponentType<T> type, T value) {
        this.type = type;
        this.value = value;
    }

    public static <T> Component<T> of(ComponentType<T> type, T value) {
        if (type == null) throw new NullPointerException("type");
        if (value == null) throw new NullPointerException("value");
        return new Component<>(type, value);
    }

    @SuppressWarnings("unchecked")
    public static <T> Component<T> of(Map.Entry<ComponentType<?>, ?> entry) {
        if (entry == null) throw new NullPointerException("entry");
        return of((ComponentType<T>) entry.getKey(), (T) entry.getValue());
    }

    public ComponentType<T> type() { return type; }
    public T value() { return value; }

    public DataResult<Object> encode(Object ops) { return DataResult.success(value); }

    public void apply(ComponentMap.Builder components) {
        if (components != null) components.add(type, value);
    }

    @Override public boolean equals(Object other) {
        return other instanceof Component<?> component
            && type.equals(component.type) && value.equals(component.value);
    }

    @Override public int hashCode() { return 31 * type.hashCode() + value.hashCode(); }
    @Override public String toString() { return type + "=" + value; }
}
