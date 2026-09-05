package net.minecraft.network;

/** Registry-aware buffer name used by the modern Fabric networking signatures. */
public class RegistryByteBuf extends PacketByteBuf {
    public RegistryByteBuf() { super(); }
    public RegistryByteBuf(byte[] bytes) { super(bytes); }
    public RegistryByteBuf(PacketByteBuf source) { super(source == null ? null : source.toByteArray()); }
}
