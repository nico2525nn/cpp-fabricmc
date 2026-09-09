package net.fabricmc.fabric.impl.networking;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry;
import net.minecraft.network.NetworkPhase;
import net.minecraft.network.NetworkSide;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/**
 * The concrete singleton registry used by Fabric's networking implementation.
 * Keeping this class as the source of truth is important: GlobalReceiverRegistry
 * receives these exact instances from its static initializer, so registrations
 * made through the public PayloadTypeRegistry accessors must be visible there.
 */
public class PayloadTypeRegistryImpl<B extends PacketByteBuf> implements PayloadTypeRegistry<B> {
    public static final PayloadTypeRegistryImpl<PacketByteBuf> CONFIGURATION_C2S =
        new PayloadTypeRegistryImpl<>(NetworkPhase.CONFIGURATION, NetworkSide.SERVERBOUND);
    public static final PayloadTypeRegistryImpl<PacketByteBuf> CONFIGURATION_S2C =
        new PayloadTypeRegistryImpl<>(NetworkPhase.CONFIGURATION, NetworkSide.CLIENTBOUND);
    public static final PayloadTypeRegistryImpl<RegistryByteBuf> PLAY_C2S =
        new PayloadTypeRegistryImpl<>(NetworkPhase.PLAY, NetworkSide.SERVERBOUND);
    public static final PayloadTypeRegistryImpl<RegistryByteBuf> PLAY_S2C =
        new PayloadTypeRegistryImpl<>(NetworkPhase.PLAY, NetworkSide.CLIENTBOUND);

    private final Map<Identifier, CustomPayload.Type<?>> packetTypes = new LinkedHashMap<>();
    private final NetworkPhase phase;
    private final NetworkSide side;

    private PayloadTypeRegistryImpl(NetworkPhase phase, NetworkSide side) {
        this.phase = phase;
        this.side = side;
    }

    @Override
    public synchronized <P extends CustomPayload> CustomPayload.Type<P> register(
            CustomPayload.Id<P> id, PacketCodec<? super B, P> codec) {
        if (id == null) throw new NullPointerException("id");
        if (codec == null) throw new NullPointerException("codec");
        if (packetTypes.containsKey(id.id()))
            throw new IllegalArgumentException("Payload type already registered: " + id);
        CustomPayload.Type<P> type = new CustomPayload.Type<>(id, codec.cast());
        packetTypes.put(id.id(), type);
        return type;
    }

    public synchronized CustomPayload.Type<?> get(Identifier id) {
        return packetTypes.get(id);
    }

    public synchronized <P extends CustomPayload> CustomPayload.Type<P> get(CustomPayload.Id<P> id) {
        if (id == null) return null;
        @SuppressWarnings("unchecked")
        CustomPayload.Type<P> type = (CustomPayload.Type<P>) packetTypes.get(id.id());
        return type;
    }

    public NetworkPhase getPhase() {
        return phase;
    }

    public NetworkSide getSide() {
        return side;
    }

    @Override
    public synchronized Set<Identifier> ids() {
        return Collections.unmodifiableSet(Set.copyOf(packetTypes.keySet()));
    }

    @Override
    public synchronized Object getCodec(CustomPayload.Id<?> id) {
        CustomPayload.Type<?> type = get(id);
        return type == null ? null : type.codec();
    }

    @Override
    public synchronized CustomPayload.Type<?> getType(CustomPayload.Id<?> id) {
        return get(id);
    }

    @Override
    public synchronized void clear() {
        packetTypes.clear();
    }
}
