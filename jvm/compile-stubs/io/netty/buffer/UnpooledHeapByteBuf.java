package io.netty.buffer;

/** Heap-buffer constructor used by the dependency-free JVM fixture. */
public class UnpooledHeapByteBuf extends ByteBuf {
    public UnpooledHeapByteBuf(ByteBufAllocator allocator, int initialCapacity, int maxCapacity) {
        super(initialCapacity);
    }
}
