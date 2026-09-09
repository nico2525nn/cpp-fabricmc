package io.netty.buffer;

/** Compile-time-only allocator singleton used by PacketByteBuf. */
public final class UnpooledByteBufAllocator implements ByteBufAllocator {
    public static final UnpooledByteBufAllocator DEFAULT = new UnpooledByteBufAllocator();

    private UnpooledByteBufAllocator() {}
}
