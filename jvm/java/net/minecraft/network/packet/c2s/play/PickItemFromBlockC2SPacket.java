package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.Packet;
import net.minecraft.util.math.BlockPos;

/**
 * Serverbound pick-block packet introduced by the 1.21.4 play protocol.
 *
 * <p>The native connection currently consumes the packet at the protocol
 * boundary.  This shadow keeps the named Fabric/Yarn listener ABI intact for
 * mixins and server-side mods.</p>
 */
public final class PickItemFromBlockC2SPacket implements Packet<Object> {
    public static final PacketCodec<PacketByteBuf, PickItemFromBlockC2SPacket> CODEC =
        PacketCodec.ofLegacy((buffer, packet) -> { }, buffer ->
            new PickItemFromBlockC2SPacket(new BlockPos(0, 0, 0), false));

    private final BlockPos pos;
    private final boolean includeData;

    public PickItemFromBlockC2SPacket(BlockPos pos, boolean includeData) {
        this.pos = pos == null ? new BlockPos(0, 0, 0) : pos;
        this.includeData = includeData;
    }

    public BlockPos pos() { return pos; }
    public boolean includeData() { return includeData; }

    @Override
    public void apply(Object listener) { }
}
